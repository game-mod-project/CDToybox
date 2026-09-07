#include "game/companion.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/log.h"
#include "game/grant.h"
#include "mem/hook.h"

namespace {
// 이 코드가 든 모듈(DLL 또는 exe)의 디렉터리. dllmain 의 self_directory 와
// 같지만 테스트·프로브도 링크할 수 있게 여기 둔다.
std::wstring module_directory() {
    HMODULE self = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&module_directory), &self);
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(self, buf, MAX_PATH);
    std::wstring s(buf, n);
    const std::size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : s.substr(0, slash + 1);
}
}  // namespace

namespace cdtb::game {

// --------------------------------------------------- 순수 함수

bool decode_message_header(const std::uint8_t* payload, std::size_t len,
                           std::uint16_t* id_out, std::uint16_t* body_len_out) {
    if (payload == nullptr || len < 5) return false;
    std::uint16_t id = 0, body = 0;
    std::memcpy(&id, payload + 0, sizeof(id));
    std::memcpy(&body, payload + 3, sizeof(body));
    if (id_out) *id_out = id;
    if (body_len_out) *body_len_out = body;
    return true;
}

bool decode_hire_to_target(const std::uint8_t* payload, std::size_t len,
                           std::uint32_t* handle_out, std::uint8_t* flag_out) {
    std::uint16_t id = 0, body = 0;
    if (!decode_message_header(payload, len, &id, &body)) return false;
    if (body != 5 || len < 5 + 5) return false;
    std::uint32_t handle = 0;
    std::memcpy(&handle, payload + 5, sizeof(handle));
    if (handle_out) *handle_out = handle;
    if (flag_out) *flag_out = payload[9];
    return true;
}

std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t cap) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string s;
    const std::size_t m = (n < cap) ? n : cap;
    s.reserve(m * 3 + 4);
    for (std::size_t i = 0; i < m; ++i) {
        if (i) s.push_back(' ');
        s.push_back(kDigits[(p[i] >> 4) & 0xF]);
        s.push_back(kDigits[p[i] & 0xF]);
    }
    if (n > cap) s += " \xE2\x80\xA6";  // …
    return s;
}

bool build_use_item_wire(std::uint32_t item_key, std::uint32_t b, std::uint8_t c,
                         std::uint32_t d, std::uint8_t* out, std::size_t cap,
                         std::size_t* len_out) {
    if (out == nullptr || cap < kUseItemWireLen) return false;
    const std::uint16_t id = kUseItemByInfoId;
    const std::uint16_t body = 13;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &item_key, 4);
    std::memcpy(out + 9, &b, 4);
    out[13] = c;
    std::memcpy(out + 14, &d, 4);
    if (len_out) *len_out = kUseItemWireLen;
    return true;
}

// --------------------------------------------------- 캡처 훅

namespace {

using DeserFn = void*(__fastcall*)(void*, void*, void*, void*);

constexpr std::size_t kPacketLen = 0x10;      // u16 전체길이
constexpr std::size_t kPacketPayload = 0x18;  // 페이로드 포인터
constexpr int kMaxDumps = 80;
constexpr std::size_t kHexCap = 768;

std::atomic<int> g_dumps{0};
std::atomic<bool> g_installed{false};
std::mutex g_last_mutex;
HireTargetCapture g_last_hire;

void dump_payload(void* packet, const char* tag) {
    if (packet == nullptr) return;
    if (g_dumps.load(std::memory_order_relaxed) >= kMaxDumps) return;
    auto* p = reinterpret_cast<const std::uint8_t*>(packet);
    std::uint16_t len = 0;
    std::uint64_t payload = 0;
    std::memcpy(&len, p + kPacketLen, sizeof(len));
    std::memcpy(&payload, p + kPacketPayload, sizeof(payload));
    if (payload == 0 || len == 0 || len > 8192) return;
    g_dumps.fetch_add(1, std::memory_order_relaxed);
    auto* pl = reinterpret_cast<const std::uint8_t*>(payload);
    std::uint16_t id = 0, body = 0;
    decode_message_header(pl, len, &id, &body);
    log::infof("동반자 캡처 [{}] ID {} 본문 {}바이트 (전체 {}): {}", tag, id,
               body, len, hex_bytes(pl, len, kHexCap));
    if (id == kHireToTargetId) {
        std::uint32_t handle = 0;
        std::uint8_t flag = 0;
        if (decode_hire_to_target(pl, len, &handle, &flag)) {
            std::lock_guard<std::mutex> lock(g_last_mutex);
            g_last_hire.valid = true;
            g_last_hire.handle = handle;
            g_last_hire.flag = flag;
            log::infof("획득 대상: 액터 핸들 0x{:08X} 플래그 {}", handle, flag);
        } else {
            log::warnf("획득 대상: 본문이 5바이트가 아니다 ({}). 정적 분석과 다름",
                       body);
        }
    }
}

// 메시지마다 detour·원본이 따로 있어야 해서 매크로로 찍어 낸다.
#define CDTB_COMP_DETOUR(id, tag)                                            \
    DeserFn g_orig_##id = nullptr;                                          \
    void* __fastcall det_##id(void* a, void* b, void* p, void* d) {         \
        dump_payload(p, tag);                                                \
        return g_orig_##id(a, b, p, d);                                      \
    }
CDTB_COMP_DETOUR(hire_target, "획득/대상")
CDTB_COMP_DETOUR(hire_item, "획득/아이템")
CDTB_COMP_DETOUR(catch_summon, "붙잡기")
CDTB_COMP_DETOUR(regist_event, "등록이벤트")
CDTB_COMP_DETOUR(select_spawn, "스폰선택")
CDTB_COMP_DETOUR(call_quick, "부르기")
CDTB_COMP_DETOUR(data_list, "소유목록")
CDTB_COMP_DETOUR(after_regist, "등록후소환")
CDTB_COMP_DETOUR(hire_response, "획득응답")
CDTB_COMP_DETOUR(use_item, "아이템사용")
CDTB_COMP_DETOUR(use_item_info, "아이템사용/정보")
#undef CDTB_COMP_DETOUR

// 클래스 이름 -> 서술자 vtable[2] (역직렬화). 부분일치가 여럿이면
// 정확히 그 이름인 것만 고른다.
bool resolve_deser(const mem::Rtti& rtti, const mem::Reader& reader,
                   const char* cls, std::uintptr_t* deser_out) {
    const std::string want = std::string(".?AV") + cls + "@pa@@";
    std::uintptr_t descriptor = 0;
    for (const auto& t : rtti.find_types(cls, 8)) {
        if (t.name == want) {
            descriptor = t.descriptor;
            break;
        }
    }
    if (descriptor == 0) return false;
    const auto vts = rtti.vtables_for(descriptor);
    if (vts.empty()) return false;
    std::uintptr_t deser = 0;
    if (!reader.read(vts[0] + 0x10, &deser, sizeof(deser)) || deser == 0) {
        return false;
    }
    *deser_out = deser;
    return true;
}

bool hook_one(const mem::Rtti& rtti, const mem::Reader& reader, const char* cls,
              void* detour, void** orig, const char* tag) {
    std::uintptr_t deser = 0;
    if (!resolve_deser(rtti, reader, cls, &deser)) {
        log::warnf("동반자 캡처: {} 역직렬화를 못 찾음", cls);
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(deser), detour, orig)) {
        log::warnf("동반자 캡처: {} 후킹 실패 (RVA 0x{:X})", tag,
                   deser - reader.module_base());
        return false;
    }
    log::infof("동반자 캡처: {} 후킹 (RVA 0x{:X})", tag,
               deser - reader.module_base());
    return true;
}

}  // namespace

HireTargetCapture last_hire_target() {
    std::lock_guard<std::mutex> lock(g_last_mutex);
    return g_last_hire;
}

int companion_capture_count() { return g_dumps.load(std::memory_order_relaxed); }

bool companion_capture_installed() {
    return g_installed.load(std::memory_order_acquire);
}

bool companion_capture_install(const mem::Rtti& rtti,
                               const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;

    int n = 0;
#define CDTB_HOOK(cls, id, tag)                                              \
    n += hook_one(rtti, reader, cls, &det_##id,                             \
                  reinterpret_cast<void**>(&g_orig_##id), tag)
    CDTB_HOOK("TrocTrHireMercenaryToTargetReq", hire_target, "획득/대상");
    CDTB_HOOK("TrocTrHireMercenaryFromInventoryReq", hire_item, "획득/아이템");
    CDTB_HOOK("TrocTrCatchBySummonReq", catch_summon, "붙잡기");
    CDTB_HOOK("TrocTrFrameEventRegistMercenaryReq", regist_event, "등록이벤트");
    CDTB_HOOK("TrocTrSelectMercenarySpawnReq", select_spawn, "스폰선택");
    CDTB_HOOK("TrocTrCallSpecialVehicleByQuickSlotReq", call_quick, "부르기");
    CDTB_HOOK("TrocTrMercenaryDataListAck", data_list, "소유목록");
    CDTB_HOOK("TrocTrSummonMercenaryAfterRegistAck", after_regist, "등록후소환");
    CDTB_HOOK("TrocTrResponseHiredMercenaryToTargetAck", hire_response,
              "획득응답");
    // 아이템 사용 두 경로. 부적을 게임이 어떻게 쓰는지(본문 A·B·C·D) 배운다.
    CDTB_HOOK("TrocTrUseItemReq", use_item, "아이템사용");
    CDTB_HOOK("TrocTrUseItemByItemInfoReq", use_item_info, "아이템사용/정보");
#undef CDTB_HOOK
    if (n == 0) return false;
    g_installed.store(true, std::memory_order_release);
    log::infof("동반자 캡처 준비됨 ({}경로) - 길들이기·등록·부르기·아이템 사용을 하면 뜬다", n);
    return true;
}


// --------------------------------------------------- 고용 작업 추적

namespace {

// (컴포넌트, &결과, &핸들, 0, 플래그) - 스택 인자 1개까지 그대로 넘긴다.
using HireWorkFn = void*(__fastcall*)(void*, std::uint32_t*, std::uint32_t*,
                                      std::uint32_t, std::uint8_t);
HireWorkFn g_orig_hire_work = nullptr;
std::atomic<bool> g_hire_trace{false};
std::atomic<int> g_hire_logs{0};
std::mutex g_hire_mutex;
HireWorkResult g_last_hire_work;
constexpr int kHireLogMax = 60;

void* __fastcall det_hire_work(void* comp, std::uint32_t* result,
                               std::uint32_t* handle, std::uint32_t z,
                               std::uint8_t flag) {
    void* r = g_orig_hire_work(comp, result, handle, z, flag);
    if (g_hire_logs.load(std::memory_order_relaxed) < kHireLogMax) {
        g_hire_logs.fetch_add(1, std::memory_order_relaxed);
        std::uint32_t hv = 0, code = 0;
        if (handle != nullptr) hv = *handle;
        if (result != nullptr) code = *result;
        log::infof("고용 작업: 핸들 0x{:08X} 플래그 {} -> 코드 {} ({})", hv, flag,
                   code, code == 0 ? "성공" : "거부");
        std::lock_guard<std::mutex> lock(g_hire_mutex);
        g_last_hire_work.valid = true;
        g_last_hire_work.handle = hv;
        g_last_hire_work.flag = flag;
        g_last_hire_work.code = code;
    }
    return r;
}

}  // namespace

HireWorkResult last_hire_work() {
    std::lock_guard<std::mutex> lock(g_hire_mutex);
    return g_last_hire_work;
}

bool companion_hire_trace_installed() {
    return g_hire_trace.load(std::memory_order_acquire);
}

bool companion_hire_trace_install(const mem::Reader& reader) {
    if (g_hire_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;
    const std::uintptr_t fn = reader.module_base() + kHireWorkRva;
    // 프롤로그가 기대와 다르면(패치로 밀렸으면) 걸지 않는다.
    // 0x2ADE280: mov [rsp+0x10],rbx / mov [rsp+0x18],rsi / mov [rsp+0x20],rdi
    std::uint8_t head[8]{};
    if (!reader.read(fn, head, sizeof(head))) return false;
    if (!(head[0] == 0x48 && head[1] == 0x89 && head[2] == 0x5C &&
          head[3] == 0x24 && head[4] == 0x10)) {
        log::warnf("고용 작업 추적: RVA 0x{:X} 프롤로그가 다르다 ({:02X} {:02X} "
                   "{:02X} {:02X} {:02X}) - 걸지 않는다",
                   kHireWorkRva, head[0], head[1], head[2], head[3], head[4]);
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(fn), &det_hire_work,
                           reinterpret_cast<void**>(&g_orig_hire_work))) {
        log::warnf("고용 작업 추적: 후킹 실패");
        return false;
    }
    g_hire_trace.store(true, std::memory_order_release);
    log::infof("고용 작업 추적 설치 (RVA 0x{:X}) - 거부 코드를 찍는다", kHireWorkRva);
    return true;
}

// --------------------------------------------------- 소환 작업 추적

namespace {

using SpawnWorkFn = void*(__fastcall*)(void*, std::uint32_t*, std::uint64_t,
                                       float*);
SpawnWorkFn g_orig_spawn_work = nullptr;
std::atomic<bool> g_spawn_trace{false};
std::atomic<int> g_spawn_logs{0};
constexpr int kSpawnLogMax = 60;
std::mutex g_spawn_mutex;
SpawnWorkResult g_last_spawn;

void* __fastcall det_spawn_work(void* gate, std::uint32_t* result,
                                std::uint64_t merc_no, float* pos) {
    void* r = g_orig_spawn_work(gate, result, merc_no, pos);
    if (g_spawn_logs.load(std::memory_order_relaxed) < kSpawnLogMax) {
        g_spawn_logs.fetch_add(1, std::memory_order_relaxed);
        const std::uint32_t code = (result != nullptr) ? *result : 0xFFFFFFFFu;
        {
            std::lock_guard<std::mutex> lock(g_spawn_mutex);
            g_last_spawn.valid = true;
            g_last_spawn.merc_no = merc_no;
            g_last_spawn.code = code;
        }
        if (pos != nullptr) {
            log::infof("소환 작업: 번호 {} 좌표 ({:.1f}, {:.1f}, {:.1f}) "
                       "-> 코드 {} ({})",
                       merc_no, pos[0], pos[1], pos[2], code,
                       code == 0 ? "성공" : "거부");
        } else {
            log::infof("소환 작업: 번호 {} 좌표 없음 -> 코드 {} ({})", merc_no,
                       code, code == 0 ? "성공" : "거부");
        }
    }
    return r;
}

}  // namespace

SpawnWorkResult last_spawn_work() {
    std::lock_guard<std::mutex> lock(g_spawn_mutex);
    return g_last_spawn;
}

bool companion_spawn_trace_installed() {
    return g_spawn_trace.load(std::memory_order_acquire);
}

bool companion_spawn_trace_install(const mem::Reader& reader) {
    if (g_spawn_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;
    const std::uintptr_t fn = reader.module_base() + kSpawnWorkRva;
    // 프롤로그가 기대와 다르면 걸지 않는다.
    // 0x2ACF600: mov rax,rsp / mov [rax+0x20],r9 / mov [rax+0x18],r8
    std::uint8_t head[8]{};
    if (!reader.read(fn, head, sizeof(head))) return false;
    if (!(head[0] == 0x48 && head[1] == 0x8B && head[2] == 0xC4 &&
          head[3] == 0x4C && head[4] == 0x89)) {
        log::warnf("소환 작업 추적: RVA 0x{:X} 프롤로그가 다르다 ({:02X} {:02X} "
                   "{:02X} {:02X} {:02X}) - 걸지 않는다",
                   kSpawnWorkRva, head[0], head[1], head[2], head[3], head[4]);
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(fn), &det_spawn_work,
                           reinterpret_cast<void**>(&g_orig_spawn_work))) {
        log::warnf("소환 작업 추적: 후킹 실패");
        return false;
    }
    g_spawn_trace.store(true, std::memory_order_release);
    log::infof("소환 작업 추적 설치 (RVA 0x{:X}) - 소환이 어디서 갈리는지 찍는다",
               kSpawnWorkRva);
    return true;
}

// ------------------------------------------- 캐릭터 소환 치트 관문 측정

namespace {

using CharCheatWorkFn = void*(__fastcall*)(void*, void*, void*, void*);
using SpawnContextFn = void*(__fastcall*)(void*, void*);

CharCheatWorkFn g_orig_char_cheat_work = nullptr;
SpawnContextFn g_orig_spawn_context = nullptr;
std::atomic<bool> g_char_cheat_trace{false};
std::atomic<bool> g_char_cheat_force{false};
std::mutex g_gate_mutex;
CharCheatGate g_last_gate;

// 작업 함수 안에 있는 동안만 켜진다. 관문 함수는 157 곳에서 불리는데
// 그 호출은 작업 함수와 같은 스레드의 직통 호출이라 이것으로 정확히
// 우리 것만 고른다.
thread_local bool t_in_char_cheat = false;
thread_local std::uint32_t t_char_cheat_key = 0;

// 죽지 않고 읽는다. 관문 함수는 게임의 어느 스레드에서든 불린다.
bool read_ptr_guarded(std::uint64_t at, std::uint64_t* out) {
    __try {
        *out = *reinterpret_cast<volatile std::uint64_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* __fastcall det_spawn_context(void* spawner, void* out) {
    void* r = g_orig_spawn_context(spawner, out);
    if (!t_in_char_cheat) return r;

    std::uint64_t context = 0;
    const bool ctx_ok =
        spawner != nullptr &&
        read_ptr_guarded(reinterpret_cast<std::uint64_t>(spawner) + 0xD8,
                         &context);

    std::uint8_t allowed = 0;
    std::uint8_t* gate = nullptr;
    if (out != nullptr) {
        gate = reinterpret_cast<std::uint8_t*>(out) + 0x10;
        allowed = *gate;
    }

    bool forced = false;
    if (allowed == 0 && ctx_ok && context != 0 && gate != nullptr &&
        g_char_cheat_force.load(std::memory_order_acquire)) {
        // 컨텍스트가 살아 있을 때만 민다. 널이면 관문 뒤에서 죽는다.
        *gate = 1;
        allowed = 1;
        forced = true;
    }

    {
        std::lock_guard<std::mutex> lock(g_gate_mutex);
        g_last_gate.valid = true;
        g_last_gate.key = t_char_cheat_key;
        g_last_gate.spawner = reinterpret_cast<std::uint64_t>(spawner);
        g_last_gate.context = ctx_ok ? context : 0;
        g_last_gate.allowed = allowed;
        g_last_gate.forced = forced;
    }

    log::infof("소환 치트 관문: 스포너 0x{:X} 컨텍스트 0x{:X} -> {}{}",
               reinterpret_cast<std::uint64_t>(spawner),
               ctx_ok ? context : 0,
               allowed != 0 ? "열림" : "닫힘",
               forced ? " (우리가 밀었다)" : "");
    if (allowed == 0) {
        log::infof("소환 치트 관문: {} - {}",
                   (ctx_ok && context != 0) ? "컨텍스트는 있는데 거절당했다"
                                            : "컨텍스트가 비었다",
                   (ctx_ok && context != 0)
                       ? "vtable[0xC0](4, 0x10) 이 false 다"
                       : "스포너+0xD8 이 0 이다 - 밀면 죽으니 밀지 않는다");
    }
    return r;
}

using SpawnContextSetFn = void*(__fastcall*)(void*, void*);
SpawnContextSetFn g_orig_spawn_context_set = nullptr;
std::atomic<int> g_ctx_set_logs{0};
constexpr int kCtxSetLogMax = 12;

// 정상 경로가 컨텍스트에 무엇을 넣는지 본다. vtable 을 찍어 두면
// 나중에 probe 로 클래스 이름을 뽑을 수 있다.
void* __fastcall det_spawn_context_set(void* spawner, void* value) {
    if (g_ctx_set_logs.load(std::memory_order_relaxed) < kCtxSetLogMax) {
        g_ctx_set_logs.fetch_add(1, std::memory_order_relaxed);
        std::uint64_t vtbl = 0;
        if (value != nullptr) {
            read_ptr_guarded(reinterpret_cast<std::uint64_t>(value), &vtbl);
        }
        log::infof("소환 컨텍스트 설정: 스포너 0x{:X} <- 0x{:X} (vtable 0x{:X})",
                   reinterpret_cast<std::uint64_t>(spawner),
                   reinterpret_cast<std::uint64_t>(value), vtbl);
    }
    return g_orig_spawn_context_set(spawner, value);
}

void* __fastcall det_char_cheat_work(void* a, void* b, void* key_ptr, void* d) {
    const bool outer = t_in_char_cheat;
    t_in_char_cheat = true;
    t_char_cheat_key = 0;
    if (key_ptr != nullptr) {
        std::uint64_t k = 0;
        if (read_ptr_guarded(reinterpret_cast<std::uint64_t>(key_ptr), &k)) {
            t_char_cheat_key = static_cast<std::uint32_t>(k & 0xFFFFFFFFu);
        }
    }
    log::infof("소환 치트 작업: 키 {} 진입", t_char_cheat_key);
    void* r = g_orig_char_cheat_work(a, b, key_ptr, d);
    t_in_char_cheat = outer;
    return r;
}

}  // namespace

CharCheatGate last_char_cheat_gate() {
    std::lock_guard<std::mutex> lock(g_gate_mutex);
    return g_last_gate;
}

void companion_char_cheat_set_force(bool on) {
    g_char_cheat_force.store(on, std::memory_order_release);
    log::infof("소환 치트 관문 밀기: {}", on ? "켬" : "끔");
}

bool companion_char_cheat_force() {
    return g_char_cheat_force.load(std::memory_order_acquire);
}

bool companion_char_cheat_trace_installed() {
    return g_char_cheat_trace.load(std::memory_order_acquire);
}

bool companion_char_cheat_trace_install(const mem::Reader& reader) {
    if (g_char_cheat_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;

    // 프롤로그가 기대와 다르면 걸지 않는다. 패치마다 밀릴 수 있다.
    // 0x2B6E530: mov [rsp+8],rbx / mov [rsp+0x20],r9
    const std::uintptr_t work = reader.module_base() + kCharCheatWorkRva;
    std::uint8_t wh[10]{};
    if (!reader.read(work, wh, sizeof(wh))) return false;
    if (!(wh[0] == 0x48 && wh[1] == 0x89 && wh[2] == 0x5C && wh[3] == 0x24 &&
          wh[4] == 0x08 && wh[5] == 0x4C && wh[6] == 0x89 && wh[7] == 0x4C)) {
        log::warnf("소환 치트 관문: 작업 RVA 0x{:X} 프롤로그가 다르다 "
                   "({:02X} {:02X} {:02X} {:02X} {:02X}) - 걸지 않는다",
                   kCharCheatWorkRva, wh[0], wh[1], wh[2], wh[3], wh[4]);
        return false;
    }

    // 0x1FB5B60: mov [rsp+8],rbx / mov [rsp+0x10],rdx / push rdi
    const std::uintptr_t ctx = reader.module_base() + kSpawnContextRva;
    std::uint8_t ch[11]{};
    if (!reader.read(ctx, ch, sizeof(ch))) return false;
    if (!(ch[0] == 0x48 && ch[1] == 0x89 && ch[2] == 0x5C && ch[3] == 0x24 &&
          ch[4] == 0x08 && ch[5] == 0x48 && ch[6] == 0x89 && ch[7] == 0x54 &&
          ch[8] == 0x24 && ch[9] == 0x10 && ch[10] == 0x57)) {
        log::warnf("소환 치트 관문: 관문 RVA 0x{:X} 프롤로그가 다르다 "
                   "({:02X} {:02X} {:02X} {:02X} {:02X}) - 걸지 않는다",
                   kSpawnContextRva, ch[0], ch[1], ch[2], ch[3], ch[4]);
        return false;
    }

    if (!mem::hook_install(reinterpret_cast<void*>(work), &det_char_cheat_work,
                           reinterpret_cast<void**>(&g_orig_char_cheat_work))) {
        log::warnf("소환 치트 관문: 작업 후킹 실패");
        return false;
    }
    if (!mem::hook_install(reinterpret_cast<void*>(ctx), &det_spawn_context,
                           reinterpret_cast<void**>(&g_orig_spawn_context))) {
        log::warnf("소환 치트 관문: 관문 후킹 실패");
        return false;
    }
    // 설정자는 실패해도 측정을 포기하지 않는다 - 곁가지다.
    const std::uintptr_t set = reader.module_base() + kSpawnContextSetRva;
    std::uint8_t sh[5]{};
    if (reader.read(set, sh, sizeof(sh)) && sh[0] == 0x48 && sh[1] == 0x89 &&
        sh[2] == 0x5C && sh[3] == 0x24 && sh[4] == 0x10) {
        mem::hook_install(reinterpret_cast<void*>(set), &det_spawn_context_set,
                          reinterpret_cast<void**>(&g_orig_spawn_context_set));
    } else {
        log::warnf("소환 컨텍스트 설정자 0x{:X} 프롤로그가 다르다 - 건너뛴다",
                   kSpawnContextSetRva);
    }

    g_char_cheat_trace.store(true, std::memory_order_release);
    log::infof("소환 치트 관문 추적 설치 (작업 0x{:X}, 관문 0x{:X}) - "
               "전체 목록 소환이 어디서 막히는지 찍는다",
               kCharCheatWorkRva, kSpawnContextRva);
    return true;
}

// --------------------------------------------------- 2976 구동

namespace {

MessageDesc g_use_item_msg;
std::atomic<bool> g_use_item_ready{false};

}  // namespace

bool companion_use_item_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_use_item_ready.load(std::memory_order_acquire)) return true;
    MessageDesc m;
    if (!resolve_message(rtti, reader, "TrocTrUseItemByItemInfoReq", &m)) {
        return false;
    }
    if (m.id != kUseItemByInfoId) {
        log::warnf("아이템 사용 메시지: ID 가 {} (기대 {})", m.id, kUseItemByInfoId);
    }
    g_use_item_msg = m;
    g_use_item_ready.store(true, std::memory_order_release);
    return true;
}

bool companion_use_item_ready() {
    return g_use_item_ready.load(std::memory_order_acquire);
}

bool request_use_item(std::uintptr_t session, std::uint32_t item_key,
                      std::uint32_t b, std::uint8_t c, std::uint32_t d) {
    if (!companion_use_item_ready() || session == 0 || item_key == 0) return false;
    std::uint8_t wire[32]{};
    std::size_t len = 0;
    if (!build_use_item_wire(item_key, b, c, d, wire, sizeof(wire), &len)) {
        return false;
    }
    log::infof("아이템 사용 구동(2976) 요청: 키 {} B {} C 0x{:X} D {}", item_key, b,
               c, d);
    return request_message(session, g_use_item_msg, wire, len);
}


// --------------------------------------------------- 실험용 메시지 표

namespace {

// 이 이름들만 미리 해석해 둔다. 명령 파일의 `msg` 가 와이어 머리의 ID 로 고른다.
const char* const kMessageClasses[] = {
    "TrocTrUseItemReq",                    // 2676 아이템 사용 (부적이 이것)
    "TrocTrUseItemByItemInfoReq",          // 2976
    "TrocTrHireMercenaryToTargetReq",      // 2338 대상 고용(획득)
    "TrocTrHireMercenaryFromInventoryReq", // 2454 인벤 고용
    "TrocTrCatchBySummonReq",              // 2386 붙잡기
    "TrocTrSelectMercenarySpawnReq",       // 2894 스폰 선택
    // 등록 뒤 상태를 바로잡거나 되돌리는 것들. 획득이 "소환된 상태"로
    // 들어가 다른 개체 소환까지 막는 문제를 풀려고 넣었다
    // (실측 2026-09-06: 목록에는 뜨는데 주위에 없고 해제도 안 됨).
    "TrocTrRequestSwitchMercenarySummonStateReq",   // 2992 u64 번호 + u8 상태
    "TrocTrUnSetMainPetAndUnSpawnReq",             // 3022 u64 번호
    "TrocTrCompleteCalculateSummonAfterRegistReq", // 2962 u64 번호 + float3
    "TrocTrFireMercenaryReq",                      // 2465 u64 번호
    "TrocTrDisbandMercenaryReq",                   // 2248 u32
};
constexpr int kMessageMax = 24;
MessageDesc g_msgs[kMessageMax];
int g_msg_count = 0;

const MessageDesc* find_message(std::uint16_t id) {
    for (int i = 0; i < g_msg_count; ++i) {
        if (g_msgs[i].id == id) return &g_msgs[i];
    }
    return nullptr;
}

}  // namespace

bool parse_hex_bytes(const std::string& text, std::uint8_t* out, std::size_t cap,
                     std::size_t* len_out) {
    if (out == nullptr) return false;
    std::size_t n = 0;
    int hi = -1;
    for (char ch : text) {
        int v;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'a' && ch <= 'f') v = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        else if (ch == ' ' || ch == '\t' || ch == ',') continue;
        else return false;
        if (hi < 0) { hi = v; continue; }
        if (n >= cap) return false;
        out[n++] = static_cast<std::uint8_t>((hi << 4) | v);
        hi = -1;
    }
    if (hi >= 0) return false;   // 홀수 자릿수
    if (len_out) *len_out = n;
    return n > 0;
}

int companion_message_count() { return g_msg_count; }

bool companion_resolve_messages(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_msg_count > 0) return true;
    for (const char* cls : kMessageClasses) {
        if (g_msg_count >= kMessageMax) break;
        MessageDesc m;
        if (!resolve_message(rtti, reader, cls, &m)) continue;
        g_msgs[g_msg_count++] = m;
    }
    log::infof("실험용 메시지 {}개 해석됨 - 명령 파일 `msg <16진 와이어>` 로 구동",
               g_msg_count);
    return g_msg_count > 0;
}


// --------------------------------------------------- 획득 (2338)

bool build_hire_wire(std::uint32_t handle, std::uint8_t flag, std::uint8_t* out,
                     std::size_t cap, std::size_t* len_out) {
    if (out == nullptr || cap < kHireWireLen) return false;
    const std::uint16_t id = kHireToTargetId;
    const std::uint16_t body = 5;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = flag;
    if (len_out) *len_out = kHireWireLen;
    return true;
}

bool hire_target_ready() { return find_message(kHireToTargetId) != nullptr; }

namespace {
PositionFn g_position_fn = nullptr;
}  // namespace

void companion_set_position_source(PositionFn fn) { g_position_fn = fn; }

bool complete_summon_ready() {
    return find_message(kCompleteSummonId) != nullptr;
}

bool build_complete_summon_wire(std::uint64_t merc_no, const float pos[3],
                                std::uint8_t* out, std::size_t cap,
                                std::size_t* len_out) {
    constexpr std::size_t kLen = 5 + 8 + 12;
    if (out == nullptr || pos == nullptr || cap < kLen) return false;
    const std::uint16_t id = kCompleteSummonId;
    const std::uint16_t body = 8 + 12;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &merc_no, 8);
    std::memcpy(out + 13, pos, 12);
    if (len_out != nullptr) *len_out = kLen;
    return true;
}

bool request_complete_summon(std::uintptr_t session, std::uint64_t merc_no,
                             const float pos[3]) {
    const MessageDesc* m = find_message(kCompleteSummonId);
    if (m == nullptr || session == 0 || merc_no == 0 || pos == nullptr) {
        return false;
    }
    std::uint8_t wire[32]{};
    std::size_t len = 0;
    if (!build_complete_summon_wire(merc_no, pos, wire, sizeof(wire), &len)) {
        return false;
    }
    log::infof("등록 후 소환: 번호 {} 좌표 ({:.1f}, {:.1f}, {:.1f})", merc_no,
               pos[0], pos[1], pos[2]);
    return request_message(session, *m, wire, len);
}

bool request_hire_target(std::uintptr_t session, std::uint32_t handle,
                         std::uint8_t flag) {
    const MessageDesc* m = find_message(kHireToTargetId);
    if (m == nullptr || session == 0 || handle == 0) return false;
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    if (!build_hire_wire(handle, flag, wire, sizeof(wire), &len)) return false;
    log::infof("획득 요청: 대상 핸들 0x{:08X} 플래그 {}", handle, flag);
    return request_message(session, *m, wire, len);
}

// --------------------------------------------------- 명령 파일

namespace {

std::atomic<bool> g_cmd_stop{false};
std::thread g_cmd_thread;
const mem::Reader* g_cmd_reader = nullptr;

// 그란트 패널과 같은 규칙: 서버 세션 중 가장 유력한 것.
std::uintptr_t pick_server_session_impl() {
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = seen_sessions(seen, hits, 16);
    if (n == 0) return 0;
    bool server[16]{};
    std::uint64_t last[16]{};
    for (int i = 0; i < n; ++i) {
        server[i] = session_is_server(i);
        last[i] = session_last_seen(i);
    }
    // 호출 횟수만 보면 안 된다. 표는 지워지지 않으므로 접속이 다시
    // 맺어진 뒤에도 옛 세션이 누적 횟수 1위로 남아 계속 뽑히고,
    // 그 풀린 포인터로 구동하면 게임 안에서 죽는다 - 실측 2026-09-06.
    const int pick = best_live_session_index(hits, server, last, n,
                                             ::GetTickCount64(),
                                             kSessionFreshMs);
    if (pick < 0 || pick >= n) {
        // 못 골랐으면 왜 못 골랐는지 표를 그대로 남긴다. 문턱을
        // 추측으로 정하지 않으려면 실제 간격이 보여야 한다.
        const std::uint64_t now = ::GetTickCount64();
        log::warnf("살아 있는 서버 세션 없음 - 후보 {}개", n);
        for (int i = 0; i < n; ++i) {
            log::warnf("  [{}] 0x{:X} {} 호출 {} 마지막 {}ms 전", i, seen[i],
                       server[i] ? "서버" : "클라", hits[i],
                       last[i] == 0 ? 0 : now - last[i]);
        }
        return 0;
    }
    const std::uintptr_t session = seen[pick];
    // 시각만으로는 멈춘 게임과 죽은 세션이 구별되지 않는다. 처리기가
    // 만지는 자리를 직접 읽어 본다.
    if (g_cmd_reader != nullptr && !session_looks_live(*g_cmd_reader, session)) {
        log::warnf("세션 0x{:X} 는 살아 있지 않다 - 구동하지 않는다", session);
        return 0;
    }
    // 새 세션을 잡았으면 지난 고장 잠금은 의미가 없다.
    if (session != 0 && session != drive_fault_session()) clear_drive_fault();
    return session;
}

std::uint32_t parse_u32(const std::string& s, std::uint32_t dflt) {
    if (s.empty()) return dflt;
    return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 0));
}

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : line) {
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::wstring command_path() { return module_directory() + L"cdtoybox_cmd.txt"; }

std::string read_and_delete(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::string text;
    LARGE_INTEGER size{};
    if (::GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < 65536) {
        text.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        if (!::ReadFile(h, text.data(), static_cast<DWORD>(text.size()), &got,
                        nullptr)) {
            text.clear();
        } else {
            text.resize(got);
        }
    }
    ::CloseHandle(h);
    ::DeleteFileW(path.c_str());
    return text;
}

void command_loop() {
    const std::wstring path = command_path();
    while (!g_cmd_stop.load(std::memory_order_acquire)) {
        const std::string text = read_and_delete(path);
        if (!text.empty()) {
            std::size_t pos = 0;
            while (pos < text.size()) {
                std::size_t nl = text.find('\n', pos);
                if (nl == std::string::npos) nl = text.size();
                const std::string line = text.substr(pos, nl - pos);
                pos = nl + 1;
                if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
                if (line[0] == '#') continue;
                std::string reply;
                const bool ok = companion_run_command(line, &reply);
                log::infof("명령 [{}] -> {}{}", line, ok ? "실행" : "거부",
                           reply.empty() ? "" : (": " + reply));
                // 대기열은 한 번에 하나뿐이다. 다음 줄은 쿨다운 뒤에.
                for (int i = 0; i < 25 && !g_cmd_stop.load(); ++i) ::Sleep(100);
            }
        }
        for (int i = 0; i < 5 && !g_cmd_stop.load(); ++i) ::Sleep(100);
    }
}

}  // namespace

std::uintptr_t companion_pick_session() { return pick_server_session_impl(); }

bool companion_run_command(const std::string& line, std::string* reply) {
    const auto args = split_ws(line);
    if (args.empty()) return false;
    const std::string& cmd = args[0];
    auto say = [&](const std::string& s) {
        if (reply) *reply = s;
    };
    if (cmd == "give") {
        if (args.size() < 2) { say("give <키> [개수]"); return false; }
        const std::uint32_t key = parse_u32(args[1], 0);
        const std::int64_t count = args.size() > 2 ? static_cast<std::int64_t>(parse_u32(args[2], 1)) : 1;
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!give_ready()) { say("지급 준비 안 됨"); return false; }
        const bool ok = request_give(session, key, count, GiveExtras{});
        say(ok ? "지급 요청" : "지급 거부(대기열/쿨다운)");
        return ok;
    }
    if (cmd == "useitem") {
        if (args.size() < 2) { say("useitem <키> [B] [C] [D]"); return false; }
        const std::uint32_t key = parse_u32(args[1], 0);
        const std::uint32_t b = args.size() > 2 ? parse_u32(args[2], 0) : 0;
        const std::uint8_t c = static_cast<std::uint8_t>(
            args.size() > 3 ? parse_u32(args[3], kUseItemByInfoKindC) : kUseItemByInfoKindC);
        const std::uint32_t d = args.size() > 4 ? parse_u32(args[4], 0) : 0;
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!companion_use_item_ready()) { say("2976 미해석"); return false; }
        const bool ok = request_use_item(session, key, b, c, d);
        say(ok ? "사용 요청" : "사용 거부(대기열/쿨다운)");
        return ok;
    }
    if (cmd == "spawnchar") {
        // 캐릭터 키로 개체를 내 앞에 스폰한다(SpawnCharacterCheatReq,
        // ID 2510). 근처에 없는 종을 획득하려면 먼저 불러와야 한다.
        //
        // 이 치트는 몸통이 살아 있다 - 역직렬화(RVA 0x28F1530)가
        // 본문을 읽은 뒤 0x2B6E530 을 부른다. 용병 치트 3종이
        // 비어 있던 것과 다르다(2026-09-06 확인).
        if (args.size() < 2) { say("spawnchar <캐릭터키> [B] [플래그]"); return false; }
        const std::uint32_t key = parse_u32(args[1], 0);
        if (key == 0) { say("키가 0이다"); return false; }
        const std::uint32_t b = args.size() > 2 ? parse_u32(args[2], 0) : 0;
        const std::uint8_t flag = static_cast<std::uint8_t>(
            args.size() > 3 ? parse_u32(args[3], 0) : 0);
        float pos[3]{};
        if (g_position_fn == nullptr || !g_position_fn(pos)) {
            say("좌표를 못 읽었다 - 월드에 들어가 있어야 한다");
            return false;
        }
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!char_spawn_ready()) { say("2510 미해석"); return false; }
        log::infof("캐릭터 소환: 키 {} B {} 플래그 {} 좌표 ({:.1f}, {:.1f}, {:.1f})",
                   key, b, flag, pos[0], pos[1], pos[2]);
        const bool ok = request_char_spawn(session, key, b, flag, pos);
        say(ok ? "소환 요청" : "거부(대기열/쿨다운/세션잠김)");
        return ok;
    }
    if (cmd == "charforce") {
        // 소환 치트 관문을 강제로 연다. 컨텍스트가 살아 있을 때만
        // 실제로 밀린다 - 널이면 관문 뒤에서 죽기 때문이다.
        if (args.size() < 2) {
            say(companion_char_cheat_force() ? "charforce on (켜져 있음)"
                                             : "charforce on|off (꺼져 있음)");
            return false;
        }
        const bool on = args[1] == "on" || args[1] == "1";
        companion_char_cheat_set_force(on);
        say(on ? "관문 밀기 켬 - spawnchar 를 다시 해보라"
               : "관문 밀기 끔");
        return true;
    }
    if (cmd == "summon") {
        if (args.size() < 2) { say("summon <용병번호> [x y z]"); return false; }
        const std::uint64_t no = std::strtoull(args[1].c_str(), nullptr, 0);
        if (no == 0) { say("번호가 0이다"); return false; }
        float pos[3]{};
        if (args.size() >= 5) {
            for (int i = 0; i < 3; ++i) {
                pos[i] = std::strtof(args[static_cast<std::size_t>(2 + i)].c_str(),
                                     nullptr);
            }
        } else {
            // 바닥 스폰이 쓰는 것과 같은 자리 - 카메라 초점이다.
            if (g_position_fn == nullptr || !g_position_fn(pos)) {
                say("좌표를 못 읽었다 - 월드에 들어가 있어야 한다");
                return false;
            }
        }
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!complete_summon_ready()) { say("2962 미해석"); return false; }
        const bool ok = request_complete_summon(session, no, pos);
        say(ok ? "등록 후 소환 요청" : "거부(대기열/쿨다운/세션잠김)");
        return ok;
    }
    if (cmd == "msg") {
        if (args.size() < 2) { say("msg <16진 와이어(머리 포함)>"); return false; }
        std::string hex;
        for (std::size_t i = 1; i < args.size(); ++i) hex += args[i];
        std::uint8_t wire[kMessageWireMax]{};
        std::size_t len = 0;
        if (!parse_hex_bytes(hex, wire, sizeof(wire), &len) || len < 5) {
            say("16진 파싱 실패"); return false;
        }
        std::uint16_t id = 0, body = 0;
        decode_message_header(wire, len, &id, &body);
        if (body + 5u != len) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "머리 본문길이 %u 인데 실제 %zu", body,
                          len - 5);
            say(buf);
            return false;
        }
        const MessageDesc* m = find_message(id);
        if (m == nullptr) { say("그 ID 는 해석돼 있지 않다"); return false; }
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        const bool ok = request_message(session, *m, wire, len);
        say(ok ? "구동 요청" : "구동 거부(대기열/쿨다운)");
        return ok;
    }
    say("모르는 명령");
    return false;
}

void companion_command_start(const mem::Reader& reader) {
    if (g_cmd_thread.joinable()) return;
    g_cmd_reader = &reader;
    g_cmd_stop.store(false, std::memory_order_release);
    g_cmd_thread = std::thread(command_loop);
    log::infof("명령 파일 감시 시작: cdtoybox_cmd.txt (give / useitem / msg)");
}

void companion_command_stop() {
    g_cmd_stop.store(true, std::memory_order_release);
    if (g_cmd_thread.joinable()) g_cmd_thread.join();
}

}  // namespace cdtb::game
