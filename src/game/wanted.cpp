#include "game/wanted.h"

#include <cstring>

namespace cdtb::game {

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

}  // namespace cdtb::game
