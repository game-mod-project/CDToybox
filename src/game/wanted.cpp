#include "game/wanted.h"

#include <cstring>

#include "core/log.h"
#include "game/grant.h"

namespace cdtb::game {
namespace {

constexpr const char* kClearClass = "TrocTrClearWantedReq";
constexpr const char* kStateClass = "TrocTrChangeWantedStateReq";

MessageDesc g_clear_msg;
MessageDesc g_state_msg;

}  // namespace

std::uint64_t bounty_to_raw(double shown) {
    // 음수를 u64 로 그냥 변환하면 거대한 값이 된다. 부호를 안 보고
    // 넘긴 실수가 이 레포에서 한 번 났다(TROUBLESHOOTING 6.20).
    if (!(shown > 0.0)) return 0;   // NaN 도 여기서 0 으로 떨어진다
    // 0.005 를 더해 반올림한다. 12.34 * 100 이 1233.9999 로 나오는
    // 부동소수 오차 때문에 그냥 자르면 한 칸씩 모자란다.
    const double raw = shown * 100.0 + 0.5;
    if (raw >= static_cast<double>(kBountyMaxRaw)) return kBountyMaxRaw;
    return static_cast<std::uint64_t>(raw);
}

double bounty_from_raw(std::uint64_t raw) {
    return static_cast<double>(raw) / 100.0;
}

bool build_clear_wanted_wire(std::uint32_t handle, std::uint8_t flag,
                             std::uint8_t* out, std::size_t cap,
                             std::size_t* len_out) {
    if (out == nullptr || cap < kClearWantedWireLen) return false;
    if (handle == 0) return false;   // 대상 없음

    // 본문 길이는 전체에서 머리 5 를 뺀 값이어야 한다. 상수로 박지
    // 않고 빼서 구한다 - 둘이 갈리면 게임이 메시지를 조용히 버린다.
    const std::uint16_t id = kClearWantedId;
    const std::uint16_t body =
        static_cast<std::uint16_t>(kClearWantedWireLen - 5);
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = flag;
    if (len_out != nullptr) *len_out = kClearWantedWireLen;
    return true;
}

bool build_change_wanted_state_wire(std::uint32_t handle, std::uint8_t state,
                                    std::uint8_t extra, std::uint8_t* out,
                                    std::size_t cap, std::size_t* len_out) {
    if (out == nullptr || cap < kChangeWantedStateWireLen) return false;
    if (handle == 0) return false;   // 대상 없음

    const std::uint16_t id = kChangeWantedStateId;
    const std::uint16_t body =
        static_cast<std::uint16_t>(kChangeWantedStateWireLen - 5);
    std::memcpy(out + 0, &id, 2);
    out[2] = 0;
    std::memcpy(out + 3, &body, 2);
    std::memcpy(out + 5, &handle, 4);
    out[9] = state;
    out[10] = extra;
    if (len_out != nullptr) *len_out = kChangeWantedStateWireLen;
    return true;
}

// --- 게임에 붙는 배관 --------------------------------------------------

bool wanted_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_state_msg.descriptor == 0 &&
        resolve_message(rtti, reader, kStateClass, &g_state_msg)) {
        log::infof("수배 상태 준비: {} ID {}", kStateClass, g_state_msg.id);
    }
    if (g_clear_msg.descriptor != 0) return true;
    if (!resolve_message(rtti, reader, kClearClass, &g_clear_msg)) {
        log::warnf("수배 해제: {} 를 해석하지 못했다", kClearClass);
        return false;
    }
    // 해석된 ID 를 남긴다. 게임이 갱신되면 여기가 먼저 달라진다 -
    // 값이 바뀐 것을 로그로 알 수 있어야 한다(실측 2026-09-17: 2646).
    log::infof("수배 해제 준비: {} ID {} 서술자 0x{:X}", kClearClass,
               g_clear_msg.id, g_clear_msg.descriptor);
    return true;
}

bool wanted_ready() { return g_clear_msg.descriptor != 0; }

std::uint32_t wanted_clear_message_id() { return g_clear_msg.id; }

bool request_clear_wanted(const mem::Reader& reader, std::uint32_t handle,
                          std::uint8_t flag) {
    if (!wanted_ready()) {
        log::warnf("수배 해제: 메시지가 아직 준비되지 않았다");
        return false;
    }
    std::uint8_t wire[kClearWantedWireLen]{};
    std::size_t len = 0;
    if (!build_clear_wanted_wire(handle, flag, wire, sizeof(wire), &len)) {
        log::warnf("수배 해제: wire 를 못 만들었다 (핸들 0x{:08X})", handle);
        return false;
    }
    // 세션 고르기는 한 곳에만 둔다. 로드 직후 죽은 세션이 뽑히던
    // 일이 있어 다른 경로도 전부 이 함수를 쓴다(grant.h).
    const std::uintptr_t session = pick_drive_session(reader);
    if (session == 0) {
        log::warnf("수배 해제: 구동할 세션을 못 골랐다 (월드 안입니까?)");
        return false;
    }
    log::infof("수배 해제 요청: 핸들 0x{:08X} 플래그 {} 세션 0x{:X}", handle,
               flag, session);
    return request_message(session, g_clear_msg, wire, len);
}

bool request_change_wanted_state(const mem::Reader& reader,
                                 std::uint32_t handle, std::uint8_t state,
                                 std::uint8_t extra) {
    if (g_state_msg.descriptor == 0) {
        log::warnf("수배 상태: 메시지가 아직 준비되지 않았다");
        return false;
    }
    std::uint8_t wire[kChangeWantedStateWireLen]{};
    std::size_t len = 0;
    if (!build_change_wanted_state_wire(handle, state, extra, wire,
                                        sizeof(wire), &len)) {
        log::warnf("수배 상태: wire 를 못 만들었다 (핸들 0x{:08X})", handle);
        return false;
    }
    const std::uintptr_t session = pick_drive_session(reader);
    if (session == 0) {
        log::warnf("수배 상태: 구동할 세션을 못 골랐다 (월드 안입니까?)");
        return false;
    }
    // 보낸 값을 그대로 남긴다 - 뜻을 모르는 채 쓸어 보는 중이라,
    // 어느 조합에서 화면이 바뀌었는지 나중에 짝지을 수 있어야 한다.
    log::infof("수배 상태 요청: 핸들 0x{:08X} 상태 {} extra {} 세션 0x{:X}",
               handle, state, extra, session);
    return request_message(session, g_state_msg, wire, len);
}

}  // namespace cdtb::game
