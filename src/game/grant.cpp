#include "game/grant.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "core/log.h"
#include "mem/hook.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 함수 앞머리 그대로다. 주소를 박아 두면 패치마다 밀리므로 바이트로
// 찾는다. 둘 다 349MB 이미지 안에서 유일한 것을 확인했다.
constexpr const char* kActorGetterPattern =
    "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41";

// TrItemValue 생성자. 기본값을 우리가 흉내내지 않고 게임에 맡긴다.
constexpr const char* kItemValueCtorPattern =
    "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 C7 01 FF FF FF FF 48 8D 41 40 89";

constexpr const char* kSpawnGroundPattern =
    "4C 8B DC 49 89 5B 08 49 89 6B 10 56 57 41 54 41 56 41 57 48 81 EC 50 01";

using ActorGetterFn = std::uintptr_t(__fastcall*)(void*);

ActorGetterFn g_orig_actor_getter = nullptr;
void* g_actor_getter_target = nullptr;
std::atomic<std::uintptr_t> g_last_actor{0};
bool g_installed = false;

// 서로 다른 값을 모은다. 훅 안에서 RTTI 를 푸는 것은 너무 비싸므로
// 주소만 적어 두고 나중에 분석 스레드가 클래스를 붙인다.
constexpr int kSeenCap = 16;
std::uintptr_t g_seen[kSeenCap]{};
std::uint32_t g_seen_hits[kSeenCap]{};
char g_seen_class[kSeenCap][96]{};
bool g_seen_server[kSeenCap]{};

// 세션은 조회 함수의 인자다. 처리기에 넘길 것은 이쪽이다.
std::uintptr_t g_sess[kSeenCap]{};
std::uint32_t g_sess_hits[kSeenCap]{};
std::uintptr_t g_sess_actor[kSeenCap]{};
char g_sess_class[kSeenCap][96]{};
bool g_sess_server[kSeenCap]{};
std::atomic<int> g_sess_count{0};
std::atomic<int> g_seen_count{0};

// 바닥 스폰. 인자는 전부 포인터다 - 디스어셈블에서 확인했다.
//   rcx 액터  rdx 결과  r8 아이템키  r9 개수  [+0x20] 필드3  [+0x28] 위치
using SpawnFn = void*(__fastcall*)(void*, std::uint32_t*, const std::uint32_t*,
                                   const std::int64_t*, const std::uint16_t*,
                                   const float*);
SpawnFn g_spawn = nullptr;
SpawnFn g_orig_spawn = nullptr;
bool g_trace = false;

// 처리기. 역직렬화가 파싱을 마치고 부르는 그 함수다. 값이 아니라
// 포인터를 받는다.
//   rcx 서술자  rdx 패킷  r8 아이템키  r9 개수  arg5 필드3  arg6 위치
using HandlerFn = void(__fastcall*)(void*, void*, const std::uint32_t*,
                                    const std::int64_t*, const std::uint16_t*,
                                    const float*);
CheatMessage g_spawn_msg;
CheatMessage g_give_msg;

// TrItemValue 생성자와 인벤토리 직행 처리기.
using CtorFn = void*(__fastcall*)(void*);
using GiveFn = void(__fastcall*)(void*, void*, void*);
CtorFn g_item_value_ctor = nullptr;
const mem::Reader* g_reader = nullptr;

// 걸어 둔 요청. 렌더 스레드가 채우고, TLS 가 준비된 게임 스레드가
// 집어 간다.
struct Pending {
    bool to_inventory = false;   // true 면 바닥이 아니라 인벤토리
    std::uintptr_t session = 0;
    std::uint32_t key = 0;
    std::int64_t count = 0;
    float pos[3]{};
};
// 아래에서 정의한다. 후킹이 먼저 나온다.
void run_spawn(std::uintptr_t session, std::uint32_t item_key,
               std::int64_t count, const float pos[3], SpawnOutcome* out);
void run_give(std::uintptr_t session, std::uint32_t item_key,
              std::int64_t count, SpawnOutcome* out);

Pending g_pending;
std::atomic<bool> g_has_pending{false};

// 게임 함수를 후킹 안에서 부르면, 그 후킹이 걸린 자리가 이미 락을
// 쥐고 있을 때 교착한다 - 실측에서 세 번째 호출이 돌아오지 않고
// 게임 조작이 통째로 멈췄다. 다음으로 줄인다.
//
//   1. 우리 후킹 안에 이미 들어와 있으면 실행하지 않는다 (중첩 금지)
//   2. 한 번에 하나만, 끝날 때까지 다음 요청을 받지 않는다
//   3. 연속 호출 사이에 간격을 둔다
thread_local int g_detour_depth = 0;
std::atomic<bool> g_running{false};
std::atomic<unsigned long long> g_last_done{0};
constexpr unsigned long long kCooldownMs = 2000;

// 안전한 실행 지점을 찾으려고 호출 스택을 한 번만 뜬다. 지금은
// 후킹이 걸린 자리에서 게임 함수를 부르는데, 그 자리가 락을 쥐고
// 있으면 교착한다. 스택의 바깥쪽 프레임이 곧 그 스레드의 루프
// 뿌리이고, 거기가 안전한 지점이다.
std::atomic<bool> g_traced{false};

void log_call_stack() {
    void* frames[40]{};
    const USHORT n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    const std::uintptr_t base = g_reader->module_base();
    const std::size_t size = g_reader->module_size();
    log::infof("실행 지점 호출 스택 {}단", n);
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) {
            log::infof("  [{}] 모듈+0x{:X}", i, a - base);
        } else {
            log::infof("  [{}] 0x{:X} (모듈 밖)", i, a);
        }
    }
}
SpawnOutcome g_outcome;

// 게임의 여러 스레드에서 불린다. 하는 일은 값을 적어 두는 것뿐이다.
std::uintptr_t __fastcall det_actor_getter(void* session) {
    // 우리가 부른 게임 함수가 이 후킹을 다시 밟는다. 진입할 때마다
    // 깊이를 세어, 중첩된 자리에서는 아무것도 실행하지 않는다.
    ++g_detour_depth;
    const std::uintptr_t actor = g_orig_actor_getter(session);

    if (session != nullptr) {
        const auto s = reinterpret_cast<std::uintptr_t>(session);
        const int m = g_sess_count.load(std::memory_order_relaxed);
        const int now = note_actor(g_sess, g_sess_hits, m, kSeenCap, s);
        // 이 세션이 어떤 액터를 내는지 같이 적어 둔다. 나중에 분석
        // 스레드가 클래스를 붙여 서버 쪽인지 가린다.
        for (int i = 0; i < now; ++i) {
            if (g_sess[i] == s) {
                g_sess_actor[i] = actor;
                break;
            }
        }
        if (now != m) g_sess_count.store(now, std::memory_order_release);
    }
    if (actor != 0) {
        g_last_actor.store(actor, std::memory_order_relaxed);
        const int n = g_seen_count.load(std::memory_order_relaxed);
        const int now = note_actor(g_seen, g_seen_hits, n, kSeenCap, actor);
        if (now != n) g_seen_count.store(now, std::memory_order_release);
    }

    // 걸어 둔 요청을 여기서 집어 간다. 게임의 여러 스레드가 이
    // 함수를 부르므로 TLS 가 선 스레드도 곧 지나간다.
    //
    // 깊이가 1일 때만 - 즉 게임 코드에 중첩되지 않은 자리에서만 -
    // 실행한다. 중첩된 자리는 이미 락을 쥐고 있을 수 있고, 실측에서
    // 그렇게 교착해 게임 조작이 통째로 멈췄다.
    // TLS 검사를 요청을 집어 간 뒤에 하면, 준비 안 된 스레드가 요청을
    // 먹고 버린다 - 실측에서 요청만 쌓이고 아무것도 실행되지 않았다.
    // 반드시 소비하기 전에 본다.
    // 안전 지점을 찾기 위한 일회성 채집. 요청과 무관하게 한 번만.
    if (g_detour_depth == 1 && g_reader != nullptr &&
        !g_traced.load(std::memory_order_acquire) && thread_ready_for_spawn()) {
        if (!g_traced.exchange(true, std::memory_order_acq_rel)) {
            log_call_stack();
        }
    }

    if (g_detour_depth == 1 && g_has_pending.load(std::memory_order_acquire) &&
        thread_ready_for_spawn() &&
        !g_running.exchange(true, std::memory_order_acq_rel)) {
        if (g_has_pending.exchange(false, std::memory_order_acq_rel)) {
            const Pending req = g_pending;
            if (req.to_inventory) {
                run_give(req.session, req.key, req.count, &g_outcome);
            } else {
                run_spawn(req.session, req.key, req.count, req.pos, &g_outcome);
            }
        }
        g_last_done.store(GetTickCount64(), std::memory_order_release);
        g_running.store(false, std::memory_order_release);
    }

    --g_detour_depth;
    return actor;
}

// 게임 함수를 부르다 죽으면 오버레이가 통째로 내려간다 - 실측에서
// 그렇게 됐다. 예외를 여기서 막는다. 이 함수 안에는 소멸자를 가진
// 객체를 두지 않는다(__try 가 허용하지 않는다).
// TLS 사슬을 따라가다 죽지 않게 감싼다.
bool safe_deref(std::uintptr_t at, std::uintptr_t* out) {
    __try {
        *out = *reinterpret_cast<std::uintptr_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool call_spawn_guarded(SpawnFn fn, void* actor, std::uint32_t* result,
                        const std::uint32_t* key, const std::int64_t* count,
                        const std::uint16_t* f3, const float* pos,
                        std::uint32_t* seh_out) {
    __try {
        fn(actor, result, key, count, f3, pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *seh_out = static_cast<std::uint32_t>(GetExceptionCode());
        return false;
    }
}

// 예외 코드만으로는 어디서 죽었는지 알 수 없다. 터진 주소까지
// 받아 둔다 - 그 주소를 파일에서 디스어셈블하면 무엇을 참조하다
// 죽었는지 바로 보인다.
int seh_filter(EXCEPTION_POINTERS* ep, std::uint32_t* code,
               std::uintptr_t* addr) {
    *code = static_cast<std::uint32_t>(ep->ExceptionRecord->ExceptionCode);
    *addr = reinterpret_cast<std::uintptr_t>(
        ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

bool call_give_guarded(GiveFn fn, void* self, void* packet, void* value,
                       std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, value);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool call_ctor_guarded(CtorFn fn, void* obj, std::uint32_t* seh_out,
                       std::uintptr_t* addr_out) {
    __try {
        fn(obj);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool call_handler_guarded(HandlerFn fn, void* self, void* packet,
                          const std::uint32_t* key, const std::int64_t* count,
                          const std::uint16_t* f3, const float* pos,
                          std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, key, count, f3, pos);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

// 실제 작업 함수가 불릴 때마다 인자를 남긴다. 원본을 그대로 부른다.
void* __fastcall det_spawn(void* actor, std::uint32_t* result,
                           const std::uint32_t* key, const std::int64_t* count,
                           const std::uint16_t* f3, const float* pos) {
    log::infof("[추적] 바닥 떨구기 액터 0x{:X} 키 {} 개수 {} 필드3 {} "
               "위치 {:.1f},{:.1f},{:.1f}",
               reinterpret_cast<std::uintptr_t>(actor),
               key != nullptr ? *key : 0,
               count != nullptr ? *count : 0,
               f3 != nullptr ? *f3 : 0,
               pos != nullptr ? pos[0] : 0.0f,
               pos != nullptr ? pos[1] : 0.0f,
               pos != nullptr ? pos[2] : 0.0f);
    void* r = g_orig_spawn(actor, result, key, count, f3, pos);
    log::infof("[추적] 바닥 떨구기 결과 0x{:X}",
               result != nullptr ? *result : 0);
    return r;
}

bool find_one(const std::vector<std::uint8_t>& image, const char* pattern,
              std::uint64_t* rva_out) {
    if (rva_out == nullptr || image.empty()) return false;
    const auto parsed = mem::parse_pattern(pattern);
    if (!parsed) return false;

    // 두 개까지만 모은다. 하나를 넘으면 어차피 고를 수 없다.
    const mem::Range range{image.data(), image.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) return false;

    *rva_out = static_cast<std::uint64_t>(hits[0] - image.data());
    return true;
}

}  // namespace

bool find_actor_getter_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kActorGetterPattern, rva_out);
}

bool find_spawn_ground_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kSpawnGroundPattern, rva_out);
}

bool actor_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed) return true;

    std::uint64_t rva = 0;
    if (!find_actor_getter_rva(rtti.image(), &rva)) {
        log::warnf("액터 조회 함수를 찾지 못했다 - 패치로 밀렸을 수 있다");
        return false;
    }
    if (!mem::hook_init()) return false;

    g_actor_getter_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_actor_getter_target, &det_actor_getter,
                           reinterpret_cast<void**>(&g_orig_actor_getter))) {
        log::errorf("액터 조회 후킹 실패 (RVA 0x{:X})", rva);
        g_actor_getter_target = nullptr;
        return false;
    }
    g_installed = true;
    log::infof("액터 조회 후킹 설치 (RVA 0x{:X})", rva);
    return true;
}

void actor_hook_remove() {
    if (!g_installed) return;
    mem::hook_remove(g_actor_getter_target);
    g_actor_getter_target = nullptr;
    g_orig_actor_getter = nullptr;
    g_installed = false;
    log::infof("액터 조회 후킹 원복");
}

bool actor_hook_installed() { return g_installed; }

int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value) {
    if (slots == nullptr || hits == nullptr) return count;
    for (int i = 0; i < count; ++i) {
        if (slots[i] == value) {
            ++hits[i];
            return count;
        }
    }
    if (count >= cap) return count;
    slots[count] = value;
    hits[count] = 1;
    return count + 1;
}

int seen_sessions(std::uintptr_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_sess_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_sess[i];
        if (hits_out != nullptr) hits_out[i] = g_sess_hits[i];
    }
    return take;
}

int seen_actors(std::uintptr_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_seen_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_seen[i];
        if (hits_out != nullptr) hits_out[i] = g_seen_hits[i];
    }
    return take;
}

std::uintptr_t last_actor() {
    return g_last_actor.load(std::memory_order_relaxed);
}

int best_actor_index(const std::uint32_t* hits, const bool* is_server, int n) {
    if (hits == nullptr || is_server == nullptr) return -1;
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (!is_server[i]) continue;
        if (best < 0 || hits[i] > hits[best]) best = i;
    }
    return best;
}

std::uintptr_t session_actor(int index) {
    if (index < 0 || index >= kSeenCap) return 0;
    return g_sess_actor[index];
}

void set_session_class(int index, const char* name) {
    if (index < 0 || index >= kSeenCap || name == nullptr) return;
    std::size_t i = 0;
    for (; i + 1 < sizeof(g_sess_class[0]) && name[i] != 0; ++i) {
        g_sess_class[index][i] = name[i];
    }
    g_sess_class[index][i] = 0;
    // 이름 안에 Server 가 들어 있으면 서버 쪽이다.
    g_sess_server[index] = std::strstr(name, "Server") != nullptr;
}

const char* session_class(int index) {
    if (index < 0 || index >= kSeenCap) return "";
    return g_sess_class[index];
}

bool session_is_server(int index) {
    if (index < 0 || index >= kSeenCap) return false;
    return g_sess_server[index];
}

bool find_handler_call(const std::uint8_t* body, std::size_t n,
                       std::uint64_t body_rva, std::uint64_t* handler_rva) {
    if (body == nullptr || handler_rva == nullptr || n < 11) return false;

    // 함수 끝에서 멈춘다. 넘어가면 옆 함수에서도 맞아 둘이 된다 -
    // 실측에서 그렇게 실패했다. MSVC 는 함수 사이를 int3 로 채운다.
    for (std::size_t i = 0; i + 4 <= n; ++i) {
        if (body[i] == 0xCC && body[i + 1] == 0xCC && body[i + 2] == 0xCC &&
            body[i + 3] == 0xCC) {
            n = i;
            break;
        }
    }
    if (n < 11) return false;

    // "call rel32" 5바이트 + "C7 03 00 00 00 00" 6바이트
    static const std::uint8_t kMark[] = {0xC7, 0x03, 0x00, 0x00, 0x00, 0x00};
    std::uint64_t found = 0;
    int hits = 0;
    for (std::size_t i = 5; i + sizeof(kMark) <= n; ++i) {
        if (body[i - 5] != 0xE8) continue;
        if (std::memcmp(body + i, kMark, sizeof(kMark)) != 0) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, body + i - 4, sizeof(rel));
        found = body_rva + i + static_cast<std::uint64_t>(
                                   static_cast<std::int64_t>(rel));
        if (++hits > 1) return false;
    }
    if (hits != 1) return false;
    *handler_rva = found;
    return true;
}

bool spawn_args_ok(std::uint32_t item_key, std::int64_t count) {
    return item_key != 0 && count > 0;
}

bool gate_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out) {
    if (out == nullptr || session == 0) return false;
    std::uintptr_t p = 0;
    if (!reader.read(session + 0xA0, &p, sizeof(p)) || p == 0) return false;
    if (!reader.read(p + 0x68, &p, sizeof(p)) || p == 0) return false;
    if (!reader.read(p + 0x130, &p, sizeof(p)) || p == 0) return false;
    *out = p;
    return true;
}

void log_gate(const mem::Rtti& rtti, const mem::Reader& reader,
              std::uintptr_t session) {
    std::uintptr_t obj = 0;
    if (!gate_object(reader, session, &obj)) {
        log::warnf("문: 세션 0x{:X} 에서 사슬이 끊겼다", session);
        return;
    }
    std::uintptr_t vtable = 0;
    if (!reader.read(obj, &vtable, sizeof(vtable))) return;
    log::infof("문 객체 0x{:X} ({}) vtable 0x{:X}", obj,
               rtti.class_of_object(obj), vtable);
    // 무리별 슬롯. 이 셋만 보면 35개가 다 덮인다.
    for (const std::uint32_t slot : {0xD0u, 0x140u, 0x160u}) {
        std::uintptr_t fn = 0;
        if (!reader.read(vtable + slot, &fn, sizeof(fn)) || fn == 0) continue;
        log::infof("  +0x{:X} -> 0x{:X} (RVA 0x{:X})", slot, fn,
                   fn - reader.module_base());
    }
}

bool resolve_cheat_message(const mem::Rtti& rtti, const mem::Reader& reader,
                           const char* class_name, CheatMessage* out) {
    if (out == nullptr || class_name == nullptr) return false;

    const auto types = rtti.find_types(class_name, 2);
    if (types.size() != 1) {
        log::warnf("치트 메시지 {}: 클래스가 {}개 - 고를 수 없다", class_name,
                   types.size());
        return false;
    }
    const auto vts = rtti.vtables_for(types[0].descriptor);
    if (vts.empty()) return false;
    const std::uintptr_t vtable = vts[0];
    const std::uint64_t vtable_rva = vtable - reader.module_base();

    // 정적 초기화가 vtable 을 넣는 전역이 곧 메시지 서술자다.
    //   48 8D 05 <-vtable>   lea rax, [rip+..]
    //   48 89 05 <-서술자>   mov [rip+..], rax
    const auto& img = rtti.image();
    std::uint64_t desc_rva = 0;
    int hits = 0;
    for (std::size_t i = 0; i + 14 <= img.size(); ++i) {
        if (img[i] != 0x48 || img[i + 1] != 0x8D || img[i + 2] != 0x05) continue;
        if (img[i + 7] != 0x48 || img[i + 8] != 0x89 || img[i + 9] != 0x05) {
            continue;
        }
        std::int32_t d1 = 0, d2 = 0;
        std::memcpy(&d1, img.data() + i + 3, 4);
        std::memcpy(&d2, img.data() + i + 10, 4);
        if (static_cast<std::uint64_t>(i + 7 + d1) != vtable_rva) continue;
        desc_rva = static_cast<std::uint64_t>(i + 14 + d2);
        if (++hits > 1) break;
    }
    if (hits != 1) {
        log::warnf("치트 메시지 {}: 서술자를 {}곳에서 찾았다", class_name, hits);
        return false;
    }

    CheatMessage m;
    m.descriptor = reader.module_base() + static_cast<std::uintptr_t>(desc_rva);

    // 서술자의 vptr 이 그 vtable 이어야 한다. 아니면 해석이 틀렸다.
    std::uintptr_t vptr = 0;
    if (!reader.read(m.descriptor, &vptr, sizeof(vptr)) || vptr != vtable) {
        log::warnf("치트 메시지 {}: 서술자 vptr 이 다르다", class_name);
        return false;
    }
    reader.read(m.descriptor + 0x0C, &m.id, sizeof(m.id));

    // vtable[2] 가 역직렬화 함수다.
    std::uintptr_t deser = 0;
    if (!reader.read(vtable + 0x10, &deser, sizeof(deser))) return false;
    const std::uint64_t deser_rva = deser - reader.module_base();
    if (deser_rva + 0x600 > img.size()) return false;

    std::uint64_t handler_rva = 0;
    if (!find_handler_call(img.data() + deser_rva, 0x600, deser_rva,
                           &handler_rva)) {
        log::warnf("치트 메시지 {}: 처리기 호출을 못 찾았다", class_name);
        return false;
    }
    m.handler = reader.module_base() + static_cast<std::uintptr_t>(handler_rva);

    log::infof("치트 메시지 {}: ID {} 서술자 0x{:X} 처리기 0x{:X}", class_name,
               m.id, m.descriptor, m.handler);
    *out = m;
    return true;
}

bool spawn_resolve_message(const mem::Rtti& rtti, const mem::Reader& reader) {
    g_reader = &reader;

    // 인벤토리 직행도 같이 해석한다.
    if (g_give_msg.handler == 0) {
        resolve_cheat_message(rtti, reader,
                              "CreateItemFromTrItemValueCheatReq", &g_give_msg);
    }
    if (g_item_value_ctor == nullptr) {
        std::uint64_t rva = 0;
        if (find_one(rtti.image(), kItemValueCtorPattern, &rva)) {
            g_item_value_ctor = reinterpret_cast<CtorFn>(
                reader.module_base() + static_cast<std::uintptr_t>(rva));
            log::infof("TrItemValue 생성자 확보 (RVA 0x{:X})", rva);
        } else {
            log::warnf("TrItemValue 생성자를 찾지 못했다");
        }
    }

    if (g_spawn_msg.handler != 0) return true;
    return resolve_cheat_message(rtti, reader, "SpawnItemToGroundByCheatReq",
                                 &g_spawn_msg);
}

const CheatMessage& spawn_message() { return g_spawn_msg; }

bool spawn_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_spawn != nullptr) return true;
    std::uint64_t rva = 0;
    if (!find_spawn_ground_rva(rtti.image(), &rva)) {
        log::warnf("바닥 스폰 함수를 찾지 못했다 - 패치로 밀렸을 수 있다");
        return false;
    }
    g_spawn = reinterpret_cast<SpawnFn>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    log::infof("바닥 스폰 함수 확보 (RVA 0x{:X})", rva);
    return true;
}

bool spawn_trace_install() {
    if (g_trace) return true;
    if (g_spawn == nullptr) return false;
    if (!mem::hook_init()) return false;
    if (!mem::hook_install(reinterpret_cast<void*>(g_spawn), &det_spawn,
                           reinterpret_cast<void**>(&g_orig_spawn))) {
        log::errorf("바닥 떨구기 추적 설치 실패");
        return false;
    }
    g_trace = true;
    log::infof("바닥 떨구기 추적 설치 - 인벤토리에서 아이템을 버려 보세요");
    return true;
}

void spawn_trace_remove() {
    if (!g_trace) return;
    mem::hook_remove(reinterpret_cast<void*>(g_spawn));
    g_orig_spawn = nullptr;
    g_trace = false;
}

bool thread_ready_for_spawn() {
    // gs:[0x58] 는 TEB 의 ThreadLocalStoragePointer 다. 작업 함수는
    // 거기서 꺼낸 블록의 +0x250 를 다시 따라가 쓴다. 그 사슬이
    // 서 있지 않은 스레드에서 부르면 널을 참조하고 죽는다.
    const std::uintptr_t tls = static_cast<std::uintptr_t>(__readgsqword(0x58));
    if (tls == 0) return false;
    std::uintptr_t slot0 = 0;
    if (!safe_deref(tls, &slot0) || slot0 == 0) return false;
    std::uintptr_t block = 0;
    if (!safe_deref(slot0 + 0x250, &block) || block == 0) return false;
    return true;
}

bool spawn_ready() {
    return g_spawn_msg.handler != 0 && g_orig_actor_getter != nullptr;
}

namespace {

// TLS 가 준비된 스레드에서만 부른다.
void run_spawn(std::uintptr_t session, std::uint32_t item_key,
               std::int64_t count, const float pos[3],
               SpawnOutcome* out) {
    SpawnOutcome o;
    if (out != nullptr) *out = o;

    std::uint32_t key = item_key;
    std::int64_t n = count;
    std::uint16_t field3 = 0;      // 뜻을 아직 모른다. 0 으로 둔다
    float where[3] = {pos[0], pos[1], pos[2]};

    // 무엇을 넣고 불렀는지 먼저 남긴다. 죽으면 이 줄이 마지막 단서다.
    log::infof("바닥 스폰: 세션 0x{:X} 키 {} 개수 {} 위치 {:.1f},{:.1f},{:.1f}",
               session, item_key, count, where[0], where[1], where[2]);

    // 처리기를 그대로 부른다. 앞단의 문(vtable +0x140)은 `mov al,1;
    // ret` 이라 늘 열려 있다 - 막고 있던 것은 권한이 아니라 세션이었다.
    // 클라이언트 세션은 [[[세션+0xA0]+0x68]+0x130] 이 비어 처리기가
    // 조용히 되돌아간다. 서버 세션이어야 한다.
    //
    // 작업 함수를 직접 부르는 것도 해 봤지만 올바른 서버 액터로도
    // 죽었다. 처리기가 하는 준비를 우리가 못 맞춘 것이다 - 그냥
    // 처리기에 맡긴다.
    std::uintptr_t gate = 0;
    if (!gate_object(*g_reader, session, &gate)) {
        o.no_actor = true;
        log::warnf("바닥 스폰: 세션 0x{:X} 는 사슬이 끊겼다 (클라이언트 세션)",
                   session);
        if (out != nullptr) *out = o;
        return;
    }
    o.actor = g_orig_actor_getter(reinterpret_cast<void*>(session));
    std::uintptr_t avt = 0;
    g_reader->read(o.actor, &avt, sizeof(avt));
    log::infof("바닥 스폰: 문 0x{:X} 액터 0x{:X} (vtable 0x{:X})", gate, o.actor,
               avt);

    // 처리기는 패킷에서 세션만 꺼낸다 ([패킷+0]). 나머지는 건드리지
    // 않지만 넉넉히 0으로 채워 둔다.
    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);

    o.called = true;
    o.crashed = !call_handler_guarded(
        reinterpret_cast<HandlerFn>(g_spawn_msg.handler),
        reinterpret_cast<void*>(g_spawn_msg.descriptor), packet, &key, &n,
        &field3, where, &o.seh, &o.fault);
    if (o.crashed) {
        log::errorf("바닥 스폰이 게임 안에서 죽었다: 0x{:X} at 0x{:X} (RVA 0x{:X})",
                    o.seh, o.fault,
                    o.fault - g_reader->module_base());
    } else {
        log::infof("바닥 스폰 끝 (처리기 경로)");
    }
    if (out != nullptr) *out = o;
}

// 인벤토리로 바로 넣는다. TLS 가 준비된 스레드에서만 부른다.
void run_give(std::uintptr_t session, std::uint32_t item_key,
              std::int64_t count, SpawnOutcome* out) {
    SpawnOutcome o;
    std::uintptr_t gate = 0;
    if (!gate_object(*g_reader, session, &gate)) {
        o.no_actor = true;
        log::warnf("인벤토리 지급: 세션 0x{:X} 는 사슬이 끊겼다", session);
        if (out != nullptr) *out = o;
        return;
    }

    // 구조체는 게임 생성자에게 맡긴다. 기본값을 흉내내지 않는다.
    alignas(16) std::uint8_t value[kItemValueSize]{};
    o.called = true;
    if (!call_ctor_guarded(g_item_value_ctor, value, &o.seh, &o.fault)) {
        o.crashed = true;
        log::errorf("인벤토리 지급: TrItemValue 생성자에서 죽었다 0x{:X}", o.seh);
        if (out != nullptr) *out = o;
        return;
    }
    if (!fill_item_value(value, sizeof(value), item_key, count)) {
        if (out != nullptr) *out = o;
        return;
    }

    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);

    log::infof("인벤토리 지급: 세션 0x{:X} 키 {} 개수 {}", session, item_key,
               count);
    o.crashed = !call_give_guarded(
        reinterpret_cast<GiveFn>(g_give_msg.handler),
        reinterpret_cast<void*>(g_give_msg.descriptor), packet, value, &o.seh,
        &o.fault);
    if (o.crashed) {
        log::errorf("인벤토리 지급이 게임 안에서 죽었다: 0x{:X} (RVA 0x{:X})",
                    o.seh, o.fault - g_reader->module_base());
    } else {
        log::infof("인벤토리 지급 끝");
    }
    if (out != nullptr) *out = o;
}

}  // namespace

bool fill_item_value(void* buf, std::size_t n, std::uint32_t item_key,
                     std::int64_t count) {
    if (buf == nullptr || n < 0x18) return false;
    auto* p = static_cast<std::uint8_t*>(buf);
    std::memcpy(p + 0x08, &item_key, sizeof(item_key));
    std::memcpy(p + 0x10, &count, sizeof(count));
    return true;
}

bool give_ready() {
    return g_give_msg.handler != 0 && g_item_value_ctor != nullptr &&
           g_orig_actor_getter != nullptr && g_reader != nullptr;
}

bool request_give(std::uintptr_t session, std::uint32_t item_key,
                  std::int64_t count) {
    if (!give_ready()) return false;
    if (!spawn_args_ok(item_key, count) || session == 0) return false;
    if (g_has_pending.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - g_last_done.load(std::memory_order_acquire) <
        kCooldownMs) {
        return false;
    }

    g_pending = Pending{};
    g_pending.to_inventory = true;
    g_pending.session = session;
    g_pending.key = item_key;
    g_pending.count = count;
    g_outcome = SpawnOutcome{};
    g_has_pending.store(true, std::memory_order_release);
    log::infof("인벤토리 지급 요청을 걸었다 - 게임 스레드를 기다린다");
    return true;
}

bool request_spawn(std::uintptr_t session, std::uint32_t item_key,
                   std::int64_t count, const float pos[3]) {
    if (!spawn_ready() || pos == nullptr || g_reader == nullptr) {
        return false;
    }
    if (!spawn_args_ok(item_key, count) || session == 0) return false;
    if (g_has_pending.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - g_last_done.load(std::memory_order_acquire) <
        kCooldownMs) {
        return false;
    }

    g_pending = Pending{};
    g_pending.session = session;
    g_pending.key = item_key;
    g_pending.count = count;
    g_pending.pos[0] = pos[0];
    g_pending.pos[1] = pos[1];
    g_pending.pos[2] = pos[2];
    g_outcome = SpawnOutcome{};
    g_has_pending.store(true, std::memory_order_release);
    log::infof("바닥 스폰 요청을 걸었다 - 게임 스레드를 기다린다");
    return true;
}

bool spawn_pending() {
    return g_has_pending.load(std::memory_order_acquire);
}

const SpawnOutcome& last_outcome() { return g_outcome; }


}  // namespace cdtb::game
