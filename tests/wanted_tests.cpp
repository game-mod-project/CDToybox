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

// --- 현상금(범죄수치) 값 다루기 ---------------------------------------
//
// 화면의 "현상 수배 N.NN" 은 `pa::WantedRegionData` 의 +0x30(u64) 이고
// **2자리 고정소수**다. 실측 2026-09-18:
//   10000 = 100.00  ·  9200 = 92.00  ·  5000 = 50.00
//
// 두 번 독립으로 확인했다. poke 로 10000 -> 5000 을 쓰니 화면이 따라
// 줄었고, 그 뒤 범죄를 저지르니 게임이 스스로 9200 으로 올렸다 - 게임이
// 그 칸을 읽고 쓴다.
//
// 상한은 100.00 이다(사용자 실측: "100이 끝이다"). 넘겨 쓰면 화면과
// 게임 판정이 어긋날 수 있으므로 자른다.

using cdtb::game::bounty_from_raw;
using cdtb::game::bounty_to_raw;
using cdtb::game::kBountyMaxRaw;

TEST(bounty_raw_is_hundredths) {
    CHECK_EQ(bounty_to_raw(100.0), static_cast<std::uint64_t>(10000));
    CHECK_EQ(bounty_to_raw(92.0), static_cast<std::uint64_t>(9200));
    CHECK_EQ(bounty_to_raw(50.0), static_cast<std::uint64_t>(5000));
    CHECK_EQ(bounty_to_raw(0.0), static_cast<std::uint64_t>(0));
}

TEST(bounty_from_raw_is_the_inverse) {
    CHECK(bounty_from_raw(10000) == 100.0);
    CHECK(bounty_from_raw(9200) == 92.0);
    CHECK(bounty_from_raw(0) == 0.0);
}

TEST(bounty_keeps_two_decimals) {
    // 화면이 두 자리를 보여 준다. 0.01 이 살아야 한다.
    CHECK_EQ(bounty_to_raw(0.01), static_cast<std::uint64_t>(1));
    CHECK_EQ(bounty_to_raw(12.34), static_cast<std::uint64_t>(1234));
}

TEST(bounty_clamps_to_the_observed_cap) {
    // 100.00 이 게임의 상한이다. 넘겨 쓰면 화면과 판정이 어긋난다.
    CHECK_EQ(bounty_to_raw(150.0), kBountyMaxRaw);
    CHECK_EQ(kBountyMaxRaw, static_cast<std::uint64_t>(10000));
}

TEST(bounty_refuses_negative) {
    // 음수는 0 으로 떨어뜨린다. u64 로 넘기면 거대한 값이 된다 -
    // 부호 없는 값을 그대로 변환하는 실수는 이 레포에서 한 번 났다
    // (TROUBLESHOOTING 6.20).
    CHECK_EQ(bounty_to_raw(-1.0), static_cast<std::uint64_t>(0));
    CHECK_EQ(bounty_to_raw(-0.5), static_cast<std::uint64_t>(0));
}

// --- 수배 상태 바꾸기 --------------------------------------------------
//
// 벌금을 0 으로 써도 지도에 지역 항목이 남는다(사용자 화면 확인
// 2026-09-18: "데메니스 왕국 / 벌금 / 0"). 벌금 액수와 **수배 상태는
// 다른 것**이고, 상태는 WantedRegionData 안에 없다 - 100.00/현상수배
// 때와 0/벌금 때를 바이트로 견주면 +0x30 말고는 한 바이트도 안 다르다.
//
// 게임이 그 일을 하는 메시지를 따로 들고 있다.
//
//   TrocTrChangeWantedStateReq  ID 2848  처리기 RVA 0x2B7F020
//
// 역직렬화기(RVA 0x29A6AA0)가 읽는 폭: u32 다음 u8, 다시 u8.
// 본문 6, 전체 11 이다.

using cdtb::game::build_change_wanted_state_wire;
using cdtb::game::kChangeWantedStateId;
using cdtb::game::kChangeWantedStateWireLen;

TEST(change_wanted_state_wire_header_and_body) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_change_wanted_state_wire(0xA0100001u, 0, 0, wire, sizeof(wire),
                                         &len));
    CHECK_EQ(len, kChangeWantedStateWireLen);
    CHECK_EQ(u16_at(wire, 0), kChangeWantedStateId);   // 2848
    CHECK_EQ(wire[2], static_cast<std::uint8_t>(0));
    CHECK_EQ(u16_at(wire, 3), static_cast<std::uint16_t>(6));   // 본문 6
}

TEST(change_wanted_state_wire_carries_handle_state_and_extra) {
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_change_wanted_state_wire(0xA0100001u, 3, 7, wire, sizeof(wire),
                                         &len));
    CHECK_EQ(u32_at(wire, 5), 0xA0100001u);
    CHECK_EQ(wire[9], static_cast<std::uint8_t>(3));    // 상태
    CHECK_EQ(wire[10], static_cast<std::uint8_t>(7));   // 두 번째 u8
}

TEST(change_wanted_state_wire_body_length_matches_total_minus_header) {
    // Clear 와 같은 불변식이다. 어긋나면 게임이 메시지를 통째로 버리고
    // 화면에는 "아무 일도 안 남" 으로만 보인다.
    std::uint8_t wire[16]{};
    std::size_t len = 0;
    CHECK(build_change_wanted_state_wire(0x12345678u, 1, 0, wire, sizeof(wire),
                                         &len));
    CHECK_EQ(u16_at(wire, 3), static_cast<std::uint16_t>(len - 5));
}

TEST(change_wanted_state_wire_refuses_bad_input) {
    std::uint8_t small[10]{};   // 11 이 필요하다
    std::size_t len = 0;
    CHECK(!build_change_wanted_state_wire(0xA0100001u, 0, 0, small,
                                          sizeof(small), &len));
    std::uint8_t wire[16]{};
    CHECK(!build_change_wanted_state_wire(0, 0, 0, wire, sizeof(wire), &len));
    CHECK(!build_change_wanted_state_wire(0xA0100001u, 0, 0, nullptr, 16,
                                          &len));
}
