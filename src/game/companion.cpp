#include "game/companion.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <algorithm>
#include <vector>

#include "core/crashlog.h"
#include "core/log.h"
#include "game/grant.h"
#include "game/roster.h"
#include "game/actors.h"
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

bool decode_catch(const std::uint8_t* payload, std::size_t len,
                  std::uint32_t* self_out, std::uint32_t* target_out) {
    if (payload == nullptr || len != kCatchWireLen) return false;
    std::uint16_t id = 0, body = 0;
    decode_message_header(payload, len, &id, &body);
    if (id != kCatchBySummonId || body != 8) return false;
    if (self_out != nullptr) std::memcpy(self_out, payload + 5, 4);
    if (target_out != nullptr) std::memcpy(target_out, payload + 9, 4);
    return true;
}

bool decode_hire_inv(const std::uint8_t* payload, std::size_t len,
                     std::uint16_t* a_out, std::uint16_t* b_out) {
    if (payload == nullptr || len != kHireInvWireLen) return false;
    std::uint16_t id = 0, body = 0;
    decode_message_header(payload, len, &id, &body);
    if (id != kHireFromInvId || body != 4) return false;
    if (a_out != nullptr) std::memcpy(a_out, payload + 5, 2);
    if (b_out != nullptr) std::memcpy(b_out, payload + 7, 2);
    return true;
}

bool build_hire_inv_wire(std::uint16_t a, std::uint16_t b, std::uint8_t* out,
                         std::size_t cap, std::size_t* len_out) {
    if (out == nullptr || cap < kHireInvWireLen) return false;
    const std::uint16_t id = kHireFromInvId;
    const std::uint16_t body = 4;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &a, 2);
    std::memcpy(out + 7, &b, 2);
    if (len_out != nullptr) *len_out = kHireInvWireLen;
    return true;
}

bool build_catch_wire(std::uint32_t self, std::uint32_t target,
                      std::uint8_t* out, std::size_t cap,
                      std::size_t* len_out) {
    if (out == nullptr || cap < kCatchWireLen) return false;
    const std::uint16_t id = kCatchBySummonId;
    const std::uint16_t body = 8;
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &self, 4);
    std::memcpy(out + 9, &target, 4);
    if (len_out != nullptr) *len_out = kCatchWireLen;
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
HireAck g_acks[kHireAckSlots];
int g_ack_next = 0;
CatchCapture g_last_catch;

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
    if (id == kHireFromInvId) {
        std::uint16_t fa = 0, fb = 0;
        if (decode_hire_inv(pl, len, &fa, &fb)) {
            log::infof("부적 등록 본문: A {} B {}", fa, fb);
        }
    }
    if (id == kCatchBySummonId) {
        std::uint32_t self = 0, target = 0;
        if (decode_catch(pl, len, &self, &target)) {
            std::lock_guard<std::mutex> lock(g_last_mutex);
            g_last_catch.valid = true;
            g_last_catch.self = self;
            g_last_catch.target = target;
            log::infof("붙잡기: 잡는쪽 0x{:08X} 대상 0x{:08X}", self, target);
        } else {
            log::warnf("붙잡기: 본문이 8바이트가 아니다 ({}). 정적 분석과 다름",
                       body);
        }
    }
    if (id == kHireAckId && body >= 20 && len >= 5 + 20) {
        // 본문 +12 의 u64 가 새로 생긴 동반자 번호다(companion.h 설명).
        std::uint64_t no = 0;
        std::memcpy(&no, pl + 5 + 12, sizeof(no));
        if (no != 0) {
            std::lock_guard<std::mutex> lock(g_last_mutex);
            HireAck& a = g_acks[g_ack_next];
            g_ack_next = (g_ack_next + 1) % kHireAckSlots;
            a.valid = true;
            a.merc_no = no;
            a.at_ms = GetTickCount64();
            a.handled = false;
            log::infof("획득 응답: 새 동반자 번호 {}", no);
        }
    }
    if (id == kHireToTargetId) {
        std::uint32_t handle = 0;
        std::uint8_t flag = 0;
        if (decode_hire_to_target(pl, len, &handle, &flag)) {
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
// 휠 소환이 보내는 메시지는 **2742** 다(0x292B040 이 송신, `mov word ptr
// [r15], 0xAB6`). 그런데 `부르기`(CallSpecialVehicleByQuickSlotReq) 캡처가
// 휠 클릭에서 한 줄도 안 찍혔다 - 말이 실제로 나올 때도 안 찍혔다. 즉 2742
// 는 그 클래스가 아니다. 남은 퀵슬롯 후보를 같이 걸어 가린다.
CDTB_COMP_DETOUR(call_hyosi, "부르기/효시")
// **휠 소환의 진짜 요청 클래스** (2026-09-16 확정). 메시지 번호 2742 로
// 좁힌 뒤, 그 번호를 버퍼에 박는 송신 함수(0x292B040)에서 서버 쪽으로
// 거슬러 올라가 vtable 을 맞춰 이름을 얻었다:
//
//   응답 2406 을 박는 코드 0x28B2F50 -> 부르는 자리 둘
//     -> 0x2ADDAE0 -> 0x2ADC010 -> **0x29656C0**
//   0x29656C0 은 vtable 0x5A09A30 의 +0x10, 즉 vtable[2](역직렬화)이고
//   그 vtable 의 RTTI 가 TrocTrFrameEventCallMercenaryReq 다.
CDTB_COMP_DETOUR(call_frame, "부르기/프레임")
CDTB_COMP_DETOUR(call_mercenary, "부르기/용병")
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

int pending_hire_acks(HireAck* out, int cap) {
    if (out == nullptr || cap <= 0) return 0;
    std::lock_guard<std::mutex> lock(g_last_mutex);
    int n = 0;
    for (const auto& a : g_acks) {
        if (!a.valid || a.handled || a.merc_no == 0) continue;
        out[n++] = a;
        if (n >= cap) break;
    }
    return n;
}

void mark_hire_ack_handled(std::uint64_t merc_no) {
    std::lock_guard<std::mutex> lock(g_last_mutex);
    for (auto& a : g_acks) {
        if (a.valid && a.merc_no == merc_no) a.handled = true;
    }
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
    CDTB_HOOK("TrocTrCallHyosiByQuickSlotReq", call_hyosi, "부르기/효시");
    CDTB_HOOK("TrocTrFrameEventCallMercenaryReq", call_frame, "부르기/프레임");
    CDTB_HOOK("TrocTrCallVehicleMercenaryAck", call_mercenary, "부르기/용병");
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

bool companion_hire_trace_install(const mem::Reader& reader) {
    if (g_hire_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;
    const std::uintptr_t fn = reader.module_base() + kHireWorkRva;
    // 프롤로그가 기대와 다르면(패치로 밀렸으면) 걸지 않는다.
    // 0x2AE02C0(2850): mov [rsp+0x10],rbx / mov [rsp+0x18],r8 ... - 2760 의
    // 0x2ADE280 은 rsi/rdi 를 저장했다. 첫 다섯 바이트는 같다.
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

// 코드 0 은 성공이지만, **0 이 아니라고 실패가 아니다.** 2026-09-09
// 실측: 종을 바꾼 까마귀·앵무새를 소환하니 코드 0xFAABC030 이 나왔는데
// 둘 다 실제로 소환됐고 탑승까지 됐다. 그 값은 게임 안 121곳이 쓰는
// 범용 코드(전역 RVA 0x6BB7EB8)라 판정에 쓸 수 없다. 그래서 코드를
// 16진수로 그대로 남기고 뜻을 붙이지 않는다.
void* __fastcall det_spawn_work(void* clan, std::uint32_t* result,
                                std::uint64_t merc_no, float* pos) {
    // 이 호출이 안 돌아오는 것이 지금 쫓는 문제다(2026-09-09: 종을 바꾼
    // 까마귀를 소환하니 예외도 없이 여기서 멈췄다). 감시에 걸어 두면
    // 12초 뒤 모든 스레드의 스택이 CDToybox.crash.txt 에 남는다.
    crashlog::watch_begin("소환작업", merc_no);
    void* r = g_orig_spawn_work(clan, result, merc_no, pos);
    crashlog::watch_end();
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
                       "-> 코드 0x{:X}{}",
                       merc_no, pos[0], pos[1], pos[2], code,
                       code == 0 ? " (성공)" : "");
        } else {
            log::infof("소환 작업: 번호 {} 좌표 없음 -> 코드 0x{:X}{}", merc_no,
                       code, code == 0 ? " (성공)" : "");
        }
    }
    return r;
}

}  // namespace

SpawnWorkResult last_spawn_work() {
    std::lock_guard<std::mutex> lock(g_spawn_mutex);
    return g_last_spawn;
}

bool companion_spawn_trace_install(const mem::Reader& reader) {
    if (g_spawn_trace.load(std::memory_order_acquire)) return true;
    if (!mem::hook_init()) return false;
    const std::uintptr_t fn = reader.module_base() + kSpawnWorkRva;
    // 프롤로그가 기대와 다르면 걸지 않는다.
    // 0x2AD1640(2850; 2760 은 0x2ACF600): mov rax,rsp / mov [rax+0x20],r9 /
    // mov [rax+0x18],r8
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
    "TrocTrCallSpecialVehicleByQuickSlotReq", // buruki: drive call-special-vehicle handler directly (body: u32 no + float3 + u32 + u8)
    // 등록 뒤 상태를 바로잡거나 되돌리는 것들. 획득이 "소환된 상태"로
    // 들어가 다른 개체 소환까지 막는 문제를 풀려고 넣었다
    // (실측 2026-09-06: 목록에는 뜨는데 주위에 없고 해제도 안 됨).
    "TrocTrRequestSwitchMercenarySummonStateReq",   // 2992 u64 번호 + u8 상태
    "TrocTrUnSetMainPetAndUnSpawnReq",             // 3022 u64 번호
    "TrocTrCompleteCalculateSummonAfterRegistReq", // 2962 u64 번호 + float3
    "TrocTrFireMercenaryReq",                      // 2465 u64 번호
    "TrocTrDisbandMercenaryReq",                   // 2248 u32
    // 탑승 트리거(스폰된 드래곤을 실제로 태우기 - 2026-09-15)
    "TrocTrReserveSummonAndRideAck",               // 소환+탑승 원자흐름(서버->클라 핸들러)
    "TrocTrRideOnVehicleReq",                      // 탈것 직접 탑승
    "TrocTrNotifySummonCharacterAck",              // 소환 캐릭터 통지
    "TrocTrAttachLinkVehicleReq",                  // 와이어/링크 탈것
    "TrocTrChangeVehiclePhysicsStateReq",          // 탈것 물리상태(정지 해제)
    "TrocTrUpdateSeatDataReq",                     // 좌석 데이터
    "TrocTrCallVehicleMercenaryAck",               // 탈것 용병 호출
    // 조종 캐릭터 전환(휠 선택 = 드래곤 조종 시도 - 2026-09-15)
    "TrocTrChangePlayerbleCharacterReq",           // 조종 캐릭터 전환
    "TrocTrChangeFocusActorReq",                   // 포커스 액터 전환
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
        log::infof("  메시지 {} ID {} 역직렬화 RVA 0x{:X}", cls, m.id,
                   m.deser - reader.module_base());
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

CatchCapture last_catch() {
    std::lock_guard<std::mutex> lock(g_last_mutex);
    return g_last_catch;
}

bool catch_ready() { return find_message(kCatchBySummonId) != nullptr; }

bool hire_from_inventory_ready() {
    return find_message(kHireFromInvId) != nullptr;
}

bool request_hire_from_inventory(std::uintptr_t session, std::uint16_t a,
                                 std::uint16_t b) {
    const MessageDesc* m = find_message(kHireFromInvId);
    if (m == nullptr || session == 0) return false;
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    if (!build_hire_inv_wire(a, b, wire, sizeof(wire), &len)) return false;
    log::infof("부적 등록 요청: A {} B {}", a, b);
    return request_message(session, *m, wire, len);
}

bool request_catch(std::uintptr_t session, std::uint32_t target,
                   std::uint32_t self) {
    const MessageDesc* m = find_message(kCatchBySummonId);
    if (m == nullptr || session == 0 || target == 0) return false;
    if (self == 0) {
        const CatchCapture seen = last_catch();
        self = seen.valid ? seen.self : kCatchSelfDefault;
    }
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    if (!build_catch_wire(self, target, wire, sizeof(wire), &len)) return false;
    log::infof("붙잡기 요청: 잡는쪽 0x{:08X} 대상 0x{:08X}", self, target);
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
std::vector<std::uintptr_t> g_actor_snapshot;  // actordiff 기준

// 지급 패널과 같은 규칙: 서버 쪽 + 게이트가 풀리는 세션.
std::uintptr_t pick_server_session_impl() {
    // 고르는 규칙은 grant.cpp 하나에 있다 - 서버 + 게이트 통과.
    //
    // 예전에는 여기서 freshness 로 따로 골랐다. 인플레이스 로드 뒤에는
    // 표의 모든 칸이 똑같이 오래돼 '가장 최근 것 대비' 비교가 의미를
    // 잃고, 죽은 세션이 그대로 뽑혔다. session_looks_live 도 그 세션에
    // "예" 를 줘서 못 걸렀다(실측 2026-09-10).
    const mem::LocalReader local;
    const mem::Reader& rd = (g_cmd_reader != nullptr) ? *g_cmd_reader : local;
    const std::uintptr_t session = pick_drive_session(rd);
    if (session == 0) {
        // 못 골랐으면 왜 못 골랐는지 표를 그대로 남긴다.
        std::uintptr_t seen[16]{};
        std::uint32_t hits[16]{};
        const int n = seen_sessions(seen, hits, 16);
        const std::uint64_t now = ::GetTickCount64();
        log::warnf("게이트가 열린 서버 세션 없음 - 후보 {}개", n);
        for (int i = 0; i < n; ++i) {
            const std::uint64_t last = session_last_seen(i);
            log::warnf("  [{}] 0x{:X} {} 호출 {} 마지막 {}ms 전", i, seen[i],
                       session_is_server(i) ? "서버" : "클라", hits[i],
                       last == 0 ? 0 : now - last);
        }
    }
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
    if (cmd == "hireitem") {
        // 부적이 쓰는 등록 경로를 직접 구동한다. A 가 캐릭터 키로 보인다.
        if (args.size() < 2) { say("hireitem <A> [B]"); return false; }
        const std::uint16_t fa = static_cast<std::uint16_t>(parse_u32(args[1], 0));
        const std::uint16_t fb = static_cast<std::uint16_t>(
            args.size() > 2 ? parse_u32(args[2], 0) : 0);
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!hire_from_inventory_ready()) { say("2454 미해석"); return false; }
        const bool ok = request_hire_from_inventory(session, fa, fb);
        say(ok ? "부적 등록 요청" : "거부(대기열/쿨다운/세션잠김)");
        return ok;
    }
    if (cmd == "catch") {
        // 게임이 야생 개체를 잡을 때 쓰는 경로를 그대로 흉내낸다.
        // 대상은 근처 목록의 액터 핸들이다.
        if (args.size() < 2) { say("catch <대상핸들> [잡는쪽핸들]"); return false; }
        const std::uint32_t target = parse_u32(args[1], 0);
        if (target == 0) { say("대상 핸들이 0이다"); return false; }
        const std::uint32_t self = args.size() > 2 ? parse_u32(args[2], 0) : 0;
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!catch_ready()) { say("2386 미해석"); return false; }
        const bool ok = request_catch(session, target, self);
        say(ok ? "붙잡기 요청" : "거부(대기열/쿨다운/세션잠김)");
        return ok;
    }
    if (cmd == "actordiff") {
        // 두 번 불러 그 사이에 생기고 사라진 액터를 낸다.
        // 살아 있는 월드는 액터가 늘 드나들어 총수만으로는 판정이 안 된다
        // (실측 2026-09-07: 1703 -> 1704 -> 1703, 잡음과 구별 불가).
        if (!actor_manager_ready()) { say("액터 매니저 미확보"); return false; }
        if (g_cmd_reader == nullptr) { say("리더 없음"); return false; }
        // 명령 스레드는 직접 걷지 않는다 - 렌더 스레드에 부탁하고 다음 프레임을
        // 기다린 뒤 복사본을 받는다(Codex 지적 2026-09-11: 직접 갈아 끼우면 그리는
        // 쪽의 포인터가 매달린다).
        live_actors_request_refresh();
        const std::uint64_t gen0 = live_actors_generation();
        for (int i = 0; i < 30 && live_actors_generation() == gen0; ++i) Sleep(50);
        if (live_actors_generation() == gen0) say("갱신 대기 초과 - 이전 목록으로");
        const std::vector<LiveActor> live = live_actors_copy();

        std::vector<std::uintptr_t> now;
        now.reserve(live.size());
        for (const LiveActor& a : live) now.push_back(a.actor);
        std::sort(now.begin(), now.end());

        if (g_actor_snapshot.empty()) {
            g_actor_snapshot = now;
            char buf[80];
            std::snprintf(buf, sizeof(buf), "기준 잡음 (액터 %zu)", now.size());
            say(buf);
            return true;
        }

        std::size_t added = 0, gone = 0;
        for (const LiveActor& a : live) {
            if (std::binary_search(g_actor_snapshot.begin(),
                                   g_actor_snapshot.end(), a.actor)) {
                continue;
            }
            ++added;
            log::infof("생김: 액터 0x{:X} 핸들 {} 키 {} 행 {} '{}' ({})", a.actor,
                       a.handle, a.key, a.row, a.display(), a.name);
        }
        for (std::uintptr_t old : g_actor_snapshot) {
            if (!std::binary_search(now.begin(), now.end(), old)) ++gone;
        }
        g_actor_snapshot = now;
        char buf[112];
        std::snprintf(buf, sizeof(buf), "생김 %zu · 사라짐 %zu (액터 %zu)", added,
                      gone, now.size());
        say(buf);
        return true;
    }
    if (cmd == "hirespecies") {
        // 그 행이 등록 가능한지 게임에게 묻는다. 등록은 되지 않는다.
        if (args.size() < 2) { say("hirespecies <캐릭터표 행번호>"); return false; }
        const std::uint16_t key = static_cast<std::uint16_t>(parse_u32(args[1], 0));
        if (key == 0) { say("키가 0이다"); return false; }
        const std::uintptr_t session = companion_pick_session();
        if (session == 0) { say("서버 세션 없음"); return false; }
        if (!hire_species_ready()) { say("준비 안 됨"); return false; }
        const bool ok = request_hire_species(session, key);
        say(ok ? "등록 검사 요청" : "거부(대기열/쿨다운/세션잠김)");
        return ok;
    }
    if (cmd == "drive") {
        // 구동 게이트 상태를 본다. `drive reset` 이면 오래 물린 것을 푼다.
        const DriveGate g = drive_gate_state(DriveLane::Companion);
        log::infof("구동 게이트: 대기 {}({}ms) 실행 {}({}ms) 쿨다운 {}ms "
                   "잠긴세션 0x{:X}",
                   g.pending ? "예" : "아니오", g.pending_age_ms,
                   g.running ? "예" : "아니오", g.running_age_ms,
                   g.cooldown_left_ms, g.fault_session);
        if (args.size() > 1 && args[1] == "reset") {
            const bool did = drive_gate_reset();
            say(did ? "게이트를 풀었다" : "풀 만큼 오래 물리지 않았다");
            return did;
        }
        say("게이트 상태를 로그에 냈다");
        return true;
    }
    if (cmd == "unlock") {
        // 구동이 게임 안에서 죽으면 그 세션을 잠근다(안전장치). 인자를
        // 실험하는 동안에는 그때마다 게임을 재시작해야 해서 비싸다.
        // 죽은 원인이 인자라는 것을 아는 경우에만 손으로 푼다.
        const std::uintptr_t locked = drive_fault_session();
        if (locked == 0) { say("잠긴 세션 없음"); return false; }
        clear_drive_fault();
        char buf[96];
        std::snprintf(buf, sizeof(buf), "세션 0x%llX 잠금 해제",
                      static_cast<unsigned long long>(locked));
        say(buf);
        return true;
    }
    if (cmd == "sessions") {
        // 세션 표를 있는 그대로 찍는다. 아무것도 구동하지 않는다.
        //
        // 세이브/로드 뒤 지급이 먹통이 되는 까닭을 가리려면 표 자체를
        // 봐야 하는데, 이 표는 훅이 모으는 모드 안쪽 값이라 밖에서
        // probe 로는 못 본다. 로드 전후로 한 번씩 찍으면 셋 중 무엇인지
        // 갈린다 - 칸이 차서 새 세션이 못 들어왔는가(포화), 옛 이름표가
        // 새 세션을 가렸는가, 아니면 정말 게이트가 안 열리는가.
        //
        // 읽기는 전부 안전 읽기(LocalReader = SEH)라 풀린 세션을 만나도
        // 죽지 않고 실패로 돌아온다.
        if (g_cmd_reader == nullptr) { say("리더 없음"); return false; }
        const mem::Reader& rd = *g_cmd_reader;
        std::uintptr_t seen[16]{};
        std::uint32_t hits[16]{};
        const int n = seen_sessions(seen, hits, 16);
        const int cap = session_capacity();
        bool server[16]{};
        bool gate_ok[16]{};
        std::uint64_t last[16]{};
        std::uintptr_t gates[16]{};
        int servers = 0;
        int opens = 0;
        const std::uint64_t now = ::GetTickCount64();
        log::infof("세션표 {}/{}{}", n, cap,
                   n >= cap ? "  ** 포화 - 새 세션은 조용히 버려진다 **" : "");
        for (int i = 0; i < n; ++i) {
            server[i] = session_is_server(i);
            last[i] = session_last_seen(i);
            gate_ok[i] = gate_object(rd, seen[i], &gates[i]);
            if (server[i]) ++servers;
            if (server[i] && gate_ok[i]) ++opens;
            const char* cls = session_class(i);
            log::infof(
                "  [{}] 0x{:X} {} 호출 {} 마지막 {}ms 전 게이트 {} 0x{:X} "
                "생존 {} 액터 0x{:X} {}",
                i, seen[i], server[i] ? "서버" : "클라", hits[i],
                last[i] == 0 ? 0 : now - last[i], gate_ok[i] ? "열림" : "끊김",
                gates[i], session_looks_live(rd, seen[i]) ? "예" : "아니오",
                session_actor(i), cls[0] == 0 ? "(이름표 없음)" : cls);
        }
        // 세 경로가 각각 무엇을 고르는지 나란히 낸다. 기준이 서로 달라
        // 한쪽만 먹통이 되는 일이 실제로 있었다(2026-09-07).
        auto pick_line = [&](const char* who, int idx) {
            if (idx < 0 || idx >= n) {
                log::infof("  선택 {}: 없음", who);
            } else {
                log::infof("  선택 {}: [{}] 0x{:X}", who, idx, seen[idx]);
            }
        };
        // 지금 규칙은 하나다 - pick_drive_session(서버 + 게이트). 세
        // 경로가 전부 그것을 쓴다. 나머지 두 줄은 **옛 기준이 무엇을
        // 골랐을지**를 나란히 두는 것으로, 로드 뒤 옛 기준이 죽은
        // 세션을 잡는 모습이 그대로 보인다(실측 2026-09-10).
        pick_line("지금 규칙(서버+게이트)",
                  best_gate_session_index(gate_ok, hits, server, n));
        pick_line("옛 기준·참고(서버+호출최다)",
                  best_actor_index(hits, server, n));
        pick_line("옛 기준·참고(서버+freshness)",
                  best_live_session_index(hits, server, last, n, now,
                                          kSessionFreshMs));
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "표 %d/%d 서버 %d 게이트열림 %d%s - 로그를 보라", n, cap,
                      servers, opens, n >= cap ? " (포화)" : "");
        say(buf);
        return true;
    }
    if (cmd == "actordump") {
        // 지금 월드에 살아 있는 액터를 이름 조각으로 찾아 로그에 낸다.
        // 소환한 개체가 실제로 생겼는지 화면을 보지 않고 확인한다.
        if (!actor_manager_ready()) { say("액터 매니저 미확보"); return false; }
        if (g_cmd_reader == nullptr) { say("리더 없음"); return false; }
        // 명령 스레드는 직접 걷지 않는다 - 렌더 스레드에 부탁하고 다음 프레임을
        // 기다린 뒤 복사본을 받는다(Codex 지적 2026-09-11: 직접 갈아 끼우면 그리는
        // 쪽의 포인터가 매달린다).
        live_actors_request_refresh();
        const std::uint64_t gen0 = live_actors_generation();
        for (int i = 0; i < 30 && live_actors_generation() == gen0; ++i) Sleep(50);
        if (live_actors_generation() == gen0) say("갱신 대기 초과 - 이전 목록으로");
        const std::vector<LiveActor> live = live_actors_copy();
        const std::string frag = args.size() > 1 ? args[1] : std::string();
        const std::size_t limit =
            args.size() > 2 ? static_cast<std::size_t>(parse_u32(args[2], 30)) : 30;
        std::size_t hits = 0;
        for (const LiveActor& a : live) {
            if (hits >= limit) break;
            if (!frag.empty() && a.name.find(frag) == std::string::npos &&
                a.label.find(frag) == std::string::npos) {
                continue;
            }
            ++hits;
            log::infof("액터: 0x{:X} 핸들 {} 키 {} {} ({}) 동반자 {}", a.actor,
                       a.handle, a.key, a.display(), a.name,
                       a.is_companion() ? "예" : "아니오");
        }
        char buf[112];
        std::snprintf(buf, sizeof(buf), "%zu개 찾음 (살아있는 액터 %zu) - 로그를 보라",
                      hits, live.size());
        say(buf);
        return true;
    }
    if (cmd == "chardump") {
        // 캐릭터 표에서 이름 조각으로 찾아 키를 로그에 낸다.
        // 조사용이다. 소환 경로는 걷어냈다(grant.h 의 경고 참조).
        if (args.size() < 2) { say("chardump <이름조각> [개수]"); return false; }
        if (!roster_ready()) { say("카탈로그가 아직 안 읽혔다"); return false; }
        const std::size_t limit =
            args.size() > 2 ? static_cast<std::size_t>(parse_u32(args[2], 20)) : 20;
        const std::string& frag = args[1];
        const std::vector<RosterEntry>& cat = character_catalog();
        std::size_t hits = 0;
        for (const RosterEntry& e : cat) {
            if (hits >= limit) break;
            if (e.name.find(frag) == std::string::npos &&
                e.label.find(frag) == std::string::npos) {
                continue;
            }
            ++hits;
            log::infof("캐릭터: 키 {} 행 {} {} ({}) 동반자 {} 고용 {}", e.key,
                       e.row, e.display(), e.name,
                       e.is_companion() ? "예" : "아니오",
                       e.hirable ? "예" : "아니오");
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%zu개 찾음 (총 %zu행) - 로그를 보라",
                      hits, cat.size());
        say(buf);
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
