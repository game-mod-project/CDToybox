#include "game/wanted.h"

#include <cstring>

#include "core/log.h"
#include "game/grant.h"

namespace cdtb::game {
namespace {

constexpr const char* kClearClass = "TrocTrClearWantedReq";

MessageDesc g_clear_msg;

}  // namespace

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

// --- 게임에 붙는 배관 --------------------------------------------------

bool wanted_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
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

}  // namespace cdtb::game
