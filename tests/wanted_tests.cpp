#include <cstdint>
#include <cstring>

#include "game/wanted.h"
#include "harness.h"

// 수배(범죄수치) 치트 메시지의 wire 형식.
//
// 머리 5바이트는 이 게임의 모든 요청 메시지가 공유한다 (grant.h):
//   [ID u16][0 u8][본문길이 u16][본문]
//
// 역직렬화기가 `패킷+0x10 전체길이 - 5 == wire[3..4] 본문길이` 를
// 검사하고 어긋나면 그대로 거부한다(RVA 0x29A7C50 을 디스어셈블해
// 확인, 2026-09-17). 그래서 길이를 틀리면 조용히 아무 일도 안 난다.
//
// 본문 폭도 같은 자리에서 읽었다 - 읽기 함수를 부르기 직전의 `r8d` 다.
//   ClearWantedReq      (ID 2646) : u32 · u8   -> 본문 5, 전체 10
//   SetWantedForDevReq  (ID 2328) : u32 · u16  -> 본문 6, 전체 11

using cdtb::game::build_clear_wanted_wire;
using cdtb::game::kClearWantedId;
using cdtb::game::kClearWantedWireLen;

namespace {

std::uint16_t u16_at(const std::uint8_t* p, std::size_t off) {
    std::uint16_t v = 0;
    std::memcpy(&v, p + off, 2);
    return v;
}

std::uint32_t u32_at(const std::uint8_t* p, std::size_t off) {
    std::uint32_t v = 0;
    std::memcpy(&v, p + off, 4);
    return v;
}

}  // namespace

TEST(clear_wanted_wire_has_the_shared_five_byte_header) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_clear_wanted_wire(0xA0100001u, 0, wire, sizeof(wire), &len));
    CHECK_EQ(len, kClearWantedWireLen);
    CHECK_EQ(u16_at(wire, 0), kClearWantedId);   // ID 2646
    CHECK_EQ(wire[2], static_cast<std::uint8_t>(0));
    CHECK_EQ(u16_at(wire, 3), static_cast<std::uint16_t>(5));   // 본문 5
}

TEST(clear_wanted_wire_carries_the_actor_handle_and_flag) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_clear_wanted_wire(0xA0100001u, 1, wire, sizeof(wire), &len));
    CHECK_EQ(u32_at(wire, 5), 0xA0100001u);
    CHECK_EQ(wire[9], static_cast<std::uint8_t>(1));
}

TEST(clear_wanted_wire_body_length_matches_total_minus_header) {
    // 역직렬화기가 이 불변식을 직접 검사한다. 여기서 깨지면 게임이
    // 메시지를 통째로 버린다 - 화면에는 "아무 일도 안 남" 으로 보인다.
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_clear_wanted_wire(0x12345678u, 0, wire, sizeof(wire), &len));
    CHECK_EQ(u16_at(wire, 3), static_cast<std::uint16_t>(len - 5));
}

TEST(clear_wanted_wire_refuses_a_small_buffer) {
    std::uint8_t wire[9]{};   // 10 바이트가 필요하다
    std::size_t len = 123;
    CHECK(!build_clear_wanted_wire(0xA0100001u, 0, wire, sizeof(wire), &len));
}

TEST(clear_wanted_wire_refuses_a_null_buffer) {
    std::size_t len = 0;
    CHECK(!build_clear_wanted_wire(0xA0100001u, 0, nullptr, 16, &len));
}

TEST(clear_wanted_wire_refuses_a_zero_handle) {
    // 핸들 0 은 "대상 없음" 이다. 게임에 보내 봐야 의미가 없고,
    // 세션이 아직 안 잡힌 상태를 조용히 넘기면 원인을 못 찾는다.
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(!build_clear_wanted_wire(0, 0, wire, sizeof(wire), &len));
}
