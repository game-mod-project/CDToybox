#include "game/grant.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstddef>
#include <cstring>

#include "core/log.h"
#include "mem/hook.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 표 조회 함수. 아이템 표를 비롯해 82개 표가 이걸 쓴다.
//   rcx = 표 + 0x68,  rdx = &키
constexpr const char* kTableLookupPattern =
    "48 83 EC 08 83 79 04 00 4C 8B D1 75";

// 함수 앞머리 그대로다. 주소를 박아 두면 패치마다 밀리므로 바이트로
// 찾는다. 둘 다 349MB 이미지 안에서 유일한 것을 확인했다.
constexpr const char* kActorGetterPattern =
    "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41";

// TrItemValue 생성자. 기본값을 우리가 흉내내지 않고 게임에 맡긴다.
constexpr const char* kItemValueCtorPattern =
    "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 C7 01 FF FF FF FF 48 8D 41 40 89";

// 스레드의 작업 디스패처. 12바이트만으로 이미지 안에서 유일하다.
constexpr const char* kTaskDispatcherPattern =
    "48 83 EC 28 48 8B 41 78 48 8B 50 08";

constexpr const char* kSpawnGroundPattern =
    "4C 8B DC 49 89 5B 08 49 89 6B 10 56 57 41 54 41 56 41 57 48 81 EC 50 01";

// 메시지 펌프 (RVA 0x2369170, 2.00.01). 앞머리 40바이트. 24바이트는
// 10곳, 32바이트는 2곳이 겹친다 - 이 프롤로그 모양이 흔하다.
//
//   mov rax,rsp / mov [rax+0x10],rbx / ... / push rdi r12-r15
//   sub rsp,0x50 / mov r15,r9 / mov r12,r8 / mov r13,rdx
//
// 인자는 다섯이다: rcx, rdx, r8(큐), r9, [rsp+0x28](TLS+0x4300 컨텍스트).
constexpr const char* kMessagePumpPattern =
    "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 48 89 48 08 57 41 54 41 "
    "55 41 56 41 57 48 83 EC 50 4D 8B F9 4D 8B E0 4C 8B";

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
CheatMessage g_stat_msg;
CheatMessage g_endur_msg;

// 표 조회 후킹. 찾는 키가 들어올 때만 남긴다.
using TableLookupFn = void*(__fastcall*)(void*, const std::uint32_t*);
TableLookupFn g_orig_lookup = nullptr;
void* g_lookup_target = nullptr;
bool g_lookup_installed = false;
std::atomic<std::uint32_t> g_watch_key{0};
std::atomic<int> g_watch_left{0};

// 내구도 처리기. 인자 넷뿐이다.
using EndurFn = void(__fastcall*)(void*, void*, const std::uint16_t*,
                                  const std::uint16_t*);      // VaryStat - 대상 ID 조회 함수를 여기서 유도한다

// 엔티티 조회. (매니저, 결과, u32 ID)
using EntityLookupFn = void*(__fastcall*)(void*, void*, std::uint32_t);
EntityLookupFn g_orig_entity = nullptr;
void* g_entity_target = nullptr;
bool g_entity_installed = false;

constexpr int kEntCap = 32;
std::uint32_t g_ent[kEntCap]{};
std::uint32_t g_ent_hits[kEntCap]{};
std::atomic<int> g_ent_count{0};

// TrItemValue 생성자와 인벤토리 직행 처리기.
using CtorFn = void*(__fastcall*)(void*);
using GiveFn = void(__fastcall*)(void*, void*, void*);
CtorFn g_item_value_ctor = nullptr;
const mem::Reader* g_reader = nullptr;

// 걸어 둔 요청. 렌더 스레드가 채우고, TLS 가 준비된 게임 스레드가
// 집어 간다.
enum class Kind { Ground, Inventory, Endurance };

struct Pending {
    Kind kind = Kind::Inventory;
    bool to_inventory = false;   // true 면 바닥이 아니라 인벤토리
    std::uintptr_t session = 0;
    std::uint32_t key = 0;
    std::int64_t count = 0;
    float pos[3]{};
    GiveExtras extras;           // 담금질·소켓 (TrItemValue 칸들)
    std::uint16_t a = 0;         // 내구도 인자
    std::uint16_t b = 0;
};
// 아래에서 정의한다. 후킹이 먼저 나온다.
void run_spawn(std::uintptr_t session, std::uint32_t item_key,
               std::int64_t count, const float pos[3], SpawnOutcome* out);
void run_give(std::uintptr_t session, std::uint32_t item_key,
              std::int64_t count, const GiveExtras& extras, SpawnOutcome* out);
void run_endurance(std::uintptr_t session, std::uint16_t a, std::uint16_t b,
                   SpawnOutcome* out);

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

// 작업 디스패처. 여기 진입점이 안전한 실행 지점이다 - 스택이 얕고
// 아직 아무 작업도 시작하지 않았다.
using TaskDispatchFn = void(__fastcall*)(void*);
TaskDispatchFn g_orig_dispatch = nullptr;
void* g_dispatch_target = nullptr;
bool g_tick_installed = false;

// 메시지 펌프. 다섯 인자를 그대로 넘긴다 - 다섯째는 스택이다.
using MessagePumpFn = void(__fastcall*)(void*, void*, void*, void*, void*);
MessagePumpFn g_orig_pump = nullptr;
void* g_pump_target = nullptr;
std::atomic<bool> g_pump_installed{false};
// 펌프가 어느 스레드·어느 작업 컨텍스트에서 도는지 처음 몇 번만
// 남긴다. 세션마다 펌프가 따로 돌 수 있어 실행 지점을 가리는 근거다.
constexpr int kPumpSeenCap = 8;
std::uint32_t g_pump_tid[kPumpSeenCap]{};
std::uintptr_t g_pump_ctx[kPumpSeenCap]{};
std::atomic<int> g_pump_seen{0};
std::atomic<std::uint32_t> g_pump_calls{0};
bool safe_deref(std::uintptr_t at, std::uintptr_t* out);

// 걸어 둔 요청이 있으면 여기서 실행한다. 조건을 한 곳에 모은다.
// 실제로 하나를 실행했으면 true.
bool run_pending_if_any() {
    if (!g_has_pending.load(std::memory_order_acquire)) return false;
    if (!thread_ready_for_spawn()) return false;
    if (g_running.exchange(true, std::memory_order_acq_rel)) return false;
    bool ran = false;
    if (g_has_pending.exchange(false, std::memory_order_acq_rel)) {
        ran = true;
        const Pending req = g_pending;
        switch (req.kind) {
            case Kind::Inventory:
                run_give(req.session, req.key, req.count, req.extras,
                         &g_outcome);
                break;
            case Kind::Endurance:
                run_endurance(req.session, req.a, req.b, &g_outcome);
                break;
            case Kind::Ground:
                run_spawn(req.session, req.key, req.count, req.pos, &g_outcome);
                break;
        }
    }
    g_last_done.store(GetTickCount64(), std::memory_order_release);
    g_running.store(false, std::memory_order_release);
    return ran;
}

// 메시지 펌프가 **돌아온 뒤에** 우리 일을 한다. 그 틱의 메시지는 전부
// 처리됐고 처리기의 락은 놓였다. TLS+0x250 은 작업 실행기가 작업
// 시작에 세우고 끝에 지우므로 여기서는 서 있다 - 디스패처 자리에서
// 서 있지 않던 것과 다른 점이다.
//
// 펌프는 세션마다 따로 돌 수 있다. 처음 몇 번은 스레드와 작업
// 컨텍스트를 남겨 어느 펌프에서 실행됐는지 알 수 있게 한다.
void note_pump_context() {
    const std::uint32_t tid = GetCurrentThreadId();
    std::uintptr_t ctx = 0;
    const std::uintptr_t tls = static_cast<std::uintptr_t>(__readgsqword(0x58));
    std::uintptr_t slot0 = 0;
    if (tls != 0 && safe_deref(tls, &slot0) && slot0 != 0) {
        safe_deref(slot0 + 0x250, &ctx);
    }
    const int n = g_pump_seen.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_pump_tid[i] == tid && g_pump_ctx[i] == ctx) return;
    }
    if (n >= kPumpSeenCap) return;
    g_pump_tid[n] = tid;
    g_pump_ctx[n] = ctx;
    g_pump_seen.store(n + 1, std::memory_order_release);
    log::infof("메시지 펌프 경계: 스레드 {} 작업 컨텍스트 0x{:X}", tid, ctx);
}

// 디스패처를 지나는 작업들. 콜백마다 한 번씩 "어느 콜백이, 어떤
// 컨텍스트로, 그때 TLS+0x250 이 서 있는지" 를 남긴다. 매 틱 돌면서
// TLS 가 서 있는 작업이 있으면 그것이 프레임 경계 후보다.
constexpr int kTaskSeenCap = 24;
std::uintptr_t g_task_cb[kTaskSeenCap]{};
std::uint32_t g_task_hits[kTaskSeenCap]{};
std::atomic<int> g_task_seen{0};

void note_task(void* self) {
    if (g_reader == nullptr) return;
    const auto t = reinterpret_cast<std::uintptr_t>(self);
    std::uintptr_t desc = 0, cb = 0, ctx = 0, tls_ctx = 0;
    if (!safe_deref(t + 0x78, &desc) || desc == 0) return;
    safe_deref(desc + 8, &cb);
    safe_deref(t + 0x80, &ctx);
    const std::uintptr_t tls = static_cast<std::uintptr_t>(__readgsqword(0x58));
    std::uintptr_t slot0 = 0;
    if (tls != 0 && safe_deref(tls, &slot0) && slot0 != 0) {
        safe_deref(slot0 + 0x250, &tls_ctx);
    }
    const int n = g_task_seen.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_task_cb[i] == cb) {
            ++g_task_hits[i];
            return;
        }
    }
    if (n >= kTaskSeenCap) return;
    g_task_cb[n] = cb;
    g_task_hits[n] = 1;
    g_task_seen.store(n + 1, std::memory_order_release);
    const std::uintptr_t base = g_reader->module_base();
    log::infof("작업 #{}: 콜백 모듈+0x{:X} 컨텍스트 0x{:X} TLS+0x250 0x{:X} "
               "스레드 {}",
               n, cb >= base ? cb - base : cb, ctx, tls_ctx,
               GetCurrentThreadId());
}

void __fastcall det_message_pump(void* a, void* b, void* c, void* d, void* e) {
    ++g_detour_depth;
    g_orig_pump(a, b, c, d, e);
    const std::uint32_t calls =
        g_pump_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (g_detour_depth == 1) {
        // 펌프가 실제로 도는지, 얼마나 자주 도는지. 처음 셋과 그 뒤
        // 1000번마다 한 줄.
        if (calls <= 3 || calls % 1000 == 0) {
            log::infof("메시지 펌프 호출 {}회 (스레드 {})", calls,
                       GetCurrentThreadId());
        }
        if (g_has_pending.load(std::memory_order_acquire)) note_pump_context();
        if (run_pending_if_any()) {
            log::infof("메시지 펌프 경계에서 요청을 실행했다 (스레드 {})",
                       GetCurrentThreadId());
        }
    }
    --g_detour_depth;
}

// 작업 콜백이 **끝난 뒤에** 우리 일을 한다.
//
// 진입 시점에 해 봤더니 요청이 실행되지 않았다 - 그때는 TLS 블록이
// 아직 서 있지 않다. 작업이 돌면서 늦게 잡히는 구조다. 콜백이
// 돌아온 자리는 그 작업이 쥐었던 락을 이미 놓았고 TLS 는 서 있다.
// 스택도 여전히 얕다(스레드 본체 -> 이 함수).
void __fastcall det_task_dispatch(void* self) {
    g_orig_dispatch(self);
    if (g_detour_depth == 0) note_task(self);
    if (g_detour_depth == 0) {
        ++g_detour_depth;
        run_pending_if_any();
        --g_detour_depth;
    }
}

// 지나가는 엔티티 ID 를 모은다. 원본을 그대로 부른다.
void* __fastcall det_entity_lookup(void* mgr, void* out, std::uint32_t id) {
    if (id != 0) {
        const int n = g_ent_count.load(std::memory_order_relaxed);
        int i = 0;
        for (; i < n; ++i) {
            if (g_ent[i] == id) {
                ++g_ent_hits[i];
                break;
            }
        }
        if (i == n && n < kEntCap) {
            g_ent[n] = id;
            g_ent_hits[n] = 1;
            g_ent_count.store(n + 1, std::memory_order_release);
        }
    }
    return g_orig_entity(mgr, out, id);
}

void* __fastcall det_table_lookup(void* table, const std::uint32_t* key) {
    // 정확히 그 키 하나가 아니라 근처 범위를 본다. 인벤토리
    // 식별자(5915)는 이 함수로 조회되지 않았다 - 변환이 먼저
    // 일어나고 그 결과가 여기로 온다면 아이템 키 자리에서 잡힌다.
    const std::uint32_t want = g_watch_key.load(std::memory_order_relaxed);
    const bool in_range =
        want != 0 && key != nullptr &&
        (*key == want || (*key > want - 5000 && *key < want + 5000));
    if (in_range && g_watch_left.load(std::memory_order_relaxed) > 0) {
        g_watch_left.fetch_sub(1, std::memory_order_relaxed);
        void* ret = _ReturnAddress();
        const std::uintptr_t base = (g_reader != nullptr)
                                        ? g_reader->module_base()
                                        : 0;
        log::infof("표 조회: 키 {} 표 0x{:X} 부른 곳 모듈+0x{:X}", *key,
                   reinterpret_cast<std::uintptr_t>(table),
                   reinterpret_cast<std::uintptr_t>(ret) - base);
    }
    return g_orig_lookup(table, key);
}

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

    // 실행 지점이 둘이다. 작업 디스패처가 더 안전하지만 그 자리에는
    // TLS 가 서 있지 않아 - 두 번 실측했다 - 요청이 실행되지 않았다.
    // 작업이 도는 동안에만 잡히고 끝나면 정리되는 모양이다.
    //
    // 그래서 여기서도 집어 간다. 이 자리는 게임 코드 한복판이라
    // 위험하지만 실제로 동작이 확인된 유일한 자리다. 깊이가 1일
    // 때만, 한 번에 하나만, 2초 간격으로 - 그 셋을 넣은 뒤로는
    // 교착이 재발하지 않았다.
    //
    // 이 자리가 실측으로 확인된 유일한 실행 지점이다. 펌프·디스패처
    // 자리는 메시지 처리 작업 전용이라 싱글플레이 유휴 상태에서는
    // 돌지 않는다(2026-09-04 실측). 그래서 여기서 실행한다.
    if (g_detour_depth == 1) run_pending_if_any();

    if (g_detour_depth == 1 && g_reader != nullptr &&
        !g_traced.load(std::memory_order_acquire) && thread_ready_for_spawn()) {
        if (!g_traced.exchange(true, std::memory_order_acq_rel)) {
            log_call_stack();
        }
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

bool call_endur_guarded(EndurFn fn, void* self, void* packet,
                        const std::uint16_t* a, const std::uint16_t* b,
                        std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, a, b);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
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

bool find_entity_lookup(const std::uint8_t* body, std::size_t n,
                        std::uint64_t body_rva, std::uint64_t* fn_rva) {
    if (body == nullptr || fn_rva == nullptr) return false;
    static const std::uint8_t kAnchor[] = {0x44, 0x8B, 0x03, 0x48, 0x8D, 0x54,
                                           0x24, 0x60, 0x48, 0x8B, 0x08, 0xE8};
    std::uint64_t found = 0;
    int hits = 0;
    if (n < sizeof(kAnchor) + 4) return false;
    for (std::size_t i = 0; i + sizeof(kAnchor) + 4 <= n; ++i) {
        if (std::memcmp(body + i, kAnchor, sizeof(kAnchor)) != 0) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, body + i + sizeof(kAnchor), sizeof(rel));
        found = body_rva + i + sizeof(kAnchor) + 4 +
                static_cast<std::uint64_t>(static_cast<std::int64_t>(rel));
        if (++hits > 1) return false;
    }
    if (hits != 1) return false;
    *fn_rva = found;
    return true;
}

bool entity_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_entity_installed) return true;
    if (g_stat_msg.handler == 0 &&
        !resolve_cheat_message(rtti, reader, "VaryStatCheatReq", &g_stat_msg)) {
        return false;
    }
    const auto& img = rtti.image();
    const std::uint64_t hrva = g_stat_msg.handler - reader.module_base();
    if (hrva + 0x900 > img.size()) return false;

    std::uint64_t fn = 0;
    if (!find_entity_lookup(img.data() + hrva, 0x900, hrva, &fn)) {
        log::warnf("엔티티 조회 함수를 못 찾았다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_entity_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(fn));
    if (!mem::hook_install(g_entity_target, &det_entity_lookup,
                           reinterpret_cast<void**>(&g_orig_entity))) {
        log::errorf("엔티티 조회 후킹 실패 (RVA 0x{:X})", fn);
        g_entity_target = nullptr;
        return false;
    }
    g_entity_installed = true;
    log::infof("엔티티 조회 후킹 설치 (RVA 0x{:X})", fn);
    return true;
}

int seen_entities(std::uint32_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_ent_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_ent[i];
        if (hits_out != nullptr) hits_out[i] = g_ent_hits[i];
    }
    return take;
}

bool table_probe_install(const mem::Rtti& rtti, const mem::Reader& reader,
                         std::uint32_t watch_key) {
    g_watch_key.store(watch_key, std::memory_order_relaxed);
    g_watch_left.store(8, std::memory_order_relaxed);
    if (g_lookup_installed) return true;

    std::uint64_t rva = 0;
    if (!find_one(rtti.image(), kTableLookupPattern, &rva)) {
        log::warnf("표 조회 함수를 못 찾았다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_lookup_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_lookup_target, &det_table_lookup,
                           reinterpret_cast<void**>(&g_orig_lookup))) {
        log::errorf("표 조회 후킹 실패 (RVA 0x{:X})", rva);
        g_lookup_target = nullptr;
        return false;
    }
    g_lookup_installed = true;
    log::infof("표 조회 후킹 설치 (RVA 0x{:X}) - 키 {} 을 지켜본다", rva,
               watch_key);
    return true;
}

bool find_task_dispatcher_rva(const std::vector<std::uint8_t>& image,
                              std::uint64_t* rva_out) {
    return find_one(image, kTaskDispatcherPattern, rva_out);
}

bool tick_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_tick_installed) return true;
    std::uint64_t rva = 0;
    if (!find_task_dispatcher_rva(rtti.image(), &rva)) {
        log::warnf("작업 디스패처를 찾지 못했다 - 안전 지점 없이 돈다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_dispatch_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_dispatch_target, &det_task_dispatch,
                           reinterpret_cast<void**>(&g_orig_dispatch))) {
        log::errorf("작업 디스패처 후킹 실패 (RVA 0x{:X})", rva);
        g_dispatch_target = nullptr;
        return false;
    }
    g_tick_installed = true;
    log::infof("작업 디스패처 후킹 설치 (RVA 0x{:X}) - 여기서만 실행한다", rva);
    return true;
}

void tick_hook_remove() {
    if (!g_tick_installed) return;
    mem::hook_remove(g_dispatch_target);
    g_dispatch_target = nullptr;
    g_orig_dispatch = nullptr;
    g_tick_installed = false;
}

bool tick_hook_installed() { return g_tick_installed; }

bool find_message_pump_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kMessagePumpPattern, rva_out);
}

bool pump_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_pump_installed.load(std::memory_order_acquire)) return true;
    std::uint64_t rva = 0;
    if (!find_message_pump_rva(rtti.image(), &rva)) {
        log::warnf("메시지 펌프를 찾지 못했다 - 액터 조회 자리에서 실행한다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_pump_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_pump_target, &det_message_pump,
                           reinterpret_cast<void**>(&g_orig_pump))) {
        log::errorf("메시지 펌프 후킹 실패 (RVA 0x{:X})", rva);
        g_pump_target = nullptr;
        return false;
    }
    g_pump_installed.store(true, std::memory_order_release);
    log::infof("메시지 펌프 후킹 설치 (RVA 0x{:X}) - 돌아온 자리에서 실행한다",
               rva);
    return true;
}

void pump_hook_remove() {
    if (!g_pump_installed.load(std::memory_order_acquire)) return;
    g_pump_installed.store(false, std::memory_order_release);
    mem::hook_remove(g_pump_target);
    g_pump_target = nullptr;
    g_orig_pump = nullptr;
    log::infof("메시지 펌프 후킹 원복 (호출 {}회)",
               g_pump_calls.load(std::memory_order_relaxed));
}

bool pump_hook_installed() {
    return g_pump_installed.load(std::memory_order_acquire);
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

    if (g_endur_msg.handler == 0) {
        resolve_cheat_message(rtti, reader, "VaryEnduranceItemByCheatReq",
                              &g_endur_msg);
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
              std::int64_t count, const GiveExtras& extras,
              SpawnOutcome* out) {
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
    if (!fill_item_value(value, sizeof(value), item_key, count, extras)) {
        if (out != nullptr) *out = o;
        return;
    }

    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);

    log::infof("인벤토리 지급: 세션 0x{:X} 키 {} 개수 {} 담금질 {} 소켓 {}"
               " 내구도 {} 연마 {}",
               session, item_key, count, extras.temper,
               static_cast<int>(extras.socket_count), extras.endurance,
               extras.sharpness);
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

// 내구도. 문이 세션 자체의 vtable +0x160 이라 관리자 사슬 검사가
// 필요 없다. 그래도 세션은 서버 쪽이어야 한다.
void run_endurance(std::uintptr_t session, std::uint16_t a, std::uint16_t b,
                   SpawnOutcome* out) {
    SpawnOutcome o;
    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);
    std::uint16_t va = a;
    std::uint16_t vb = b;

    log::infof("내구도: 세션 0x{:X} a={} b={}", session, a, b);
    o.called = true;
    o.crashed = !call_endur_guarded(
        reinterpret_cast<EndurFn>(g_endur_msg.handler),
        reinterpret_cast<void*>(g_endur_msg.descriptor), packet, &va, &vb,
        &o.seh, &o.fault);
    if (o.crashed) {
        log::errorf("내구도가 게임 안에서 죽었다: 0x{:X} (RVA 0x{:X})", o.seh,
                    o.fault - g_reader->module_base());
    } else {
        log::infof("내구도 끝");
    }
    if (out != nullptr) *out = o;
}

}  // namespace

bool endurance_ready() {
    return g_endur_msg.handler != 0 && g_reader != nullptr;
}

bool request_endurance(std::uintptr_t session, std::uint16_t a,
                       std::uint16_t b) {
    if (!endurance_ready() || session == 0) return false;
    if (g_has_pending.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - g_last_done.load(std::memory_order_acquire) <
        kCooldownMs) {
        return false;
    }
    g_pending = Pending{};
    g_pending.kind = Kind::Endurance;
    g_pending.session = session;
    g_pending.a = a;
    g_pending.b = b;
    g_outcome = SpawnOutcome{};
    g_has_pending.store(true, std::memory_order_release);
    log::infof("내구도 요청을 걸었다");
    return true;
}

int clamp_count_to_stack(int count, std::uint32_t max_stack) {
    if (count < 1) count = 1;
    if (max_stack == 0) return count;
    if (static_cast<std::int64_t>(count) >
        static_cast<std::int64_t>(max_stack)) {
        // 여기 왔다면 max_stack < count <= INT_MAX 이므로 좁혀도 된다.
        return static_cast<int>(max_stack);
    }
    return count;
}

bool fill_item_value(void* buf, std::size_t n, std::uint32_t item_key,
                     std::int64_t count, const GiveExtras& extras) {
    // 장비 연마 칸이 +0x1AE 까지 가므로 그만큼은 있어야 한다.
    // (소켓만 보고 0x60 으로 잡았다가 그 뒤를 쓰게 됐다.)
    if (buf == nullptr || n < 0x1B0) return false;
    if (extras.socket_count > kGiveMaxSockets) return false;

    auto* p = static_cast<std::uint8_t*>(buf);
    std::memcpy(p + 0x08, &item_key, sizeof(item_key));
    std::memcpy(p + 0x0C, &extras.temper, sizeof(extras.temper));
    std::memcpy(p + 0x10, &count, sizeof(count));
    std::memcpy(p + 0x2A, &extras.endurance, sizeof(extras.endurance));

    // 개수만큼만 옮긴다. 나머지 칸은 게임 생성자가 채운 빈 값
    // (FF FF 00 00 FF) 그대로 두어야 한다.
    for (int i = 0; i < extras.socket_count; ++i) {
        std::memcpy(p + 0x40 + i * kGiveSocketBytes, extras.sockets[i].raw,
                    kGiveSocketBytes);
    }
    p[0x5E] = extras.socket_count;
    std::memcpy(p + 0x1AE, &extras.sharpness, sizeof(extras.sharpness));
    return true;
}

bool give_ready() {
    return g_give_msg.handler != 0 && g_item_value_ctor != nullptr &&
           g_orig_actor_getter != nullptr && g_reader != nullptr;
}

bool request_give(std::uintptr_t session, std::uint32_t item_key,
                  std::int64_t count, const GiveExtras& extras) {
    if (!give_ready()) return false;
    if (!spawn_args_ok(item_key, count) || session == 0) return false;
    if (g_has_pending.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - g_last_done.load(std::memory_order_acquire) <
        kCooldownMs) {
        return false;
    }

    g_pending = Pending{};
    g_pending.kind = Kind::Inventory;
    g_pending.to_inventory = true;
    g_pending.session = session;
    g_pending.key = item_key;
    g_pending.count = count;
    g_pending.extras = extras;
    g_outcome = SpawnOutcome{};
    g_has_pending.store(true, std::memory_order_release);
    log::infof("인벤토리 지급 요청을 걸었다 (담금질 {} 소켓 {}) -"
               " 게임 스레드를 기다린다",
               extras.temper, static_cast<int>(extras.socket_count));
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
    g_pending.kind = Kind::Ground;
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
