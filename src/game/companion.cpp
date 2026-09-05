#include "game/companion.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include "core/log.h"
#include "mem/hook.h"

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

// --------------------------------------------------- 캡처 훅

namespace {

using DeserFn = void*(__fastcall*)(void*, void*, void*, void*);

constexpr std::size_t kPacketLen = 0x10;      // u16 전체길이
constexpr std::size_t kPacketPayload = 0x18;  // 페이로드 포인터
constexpr int kMaxDumps = 80;
constexpr std::size_t kHexCap = 768;
constexpr std::uint16_t kHireToTargetId = 2338;

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
#undef CDTB_HOOK
    if (n == 0) return false;
    g_installed.store(true, std::memory_order_release);
    log::infof("동반자 캡처 준비됨 ({}경로) - 길들이기·등록·부르기를 하면 뜬다", n);
    return true;
}

}  // namespace cdtb::game
