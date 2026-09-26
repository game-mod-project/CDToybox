// 행동 제한 목록 읽기(`actionlimit`)의 순수 부분.
//
// 항목 0x38바이트 배치를 못박아 둔다 — exe 1.0.0.2944 에서 뜬 값이다
// (specs/2026-09-21-boss-room-action-limit.md §4). 한 칸만 어긋나도 화면과 로그가
// 엉뚱한 제한을 보여 준다(보스룸 한 판의 판정이 이 줄들로 났다).
#include <cstdint>
#include <cstring>
#include <string>

#include "game/actionlimit.h"
#include "harness.h"

namespace {

using cdtb::game::action_limit_decode;
using cdtb::game::action_limit_text;
using cdtb::game::ActionLimit;
using cdtb::game::kLimitEntrySize;

void put_u32(std::uint8_t* b, std::size_t off, std::uint32_t v) {
    std::memcpy(b + off, &v, 4);
}
void put_u64(std::uint8_t* b, std::size_t off, std::uint64_t v) {
    std::memcpy(b + off, &v, 8);
}

TEST(actionlimit_constants_are_the_measured_ones) {
    // 2949 0x55B53C8 -> 2976 0x55B5460 (RTTI 로 다시 짚음).
    CHECK_EQ(static_cast<long long>(cdtb::game::kCtlVtRva), 0x55B5460LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kCtlLimitOff), 0x120LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitArrayOff), 0xD8LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitCountOff), 0xE0LL);
    CHECK_EQ(static_cast<long long>(kLimitEntrySize), 0x38LL);
    // 0x362DF0 이 허용 목록을 읽는 두 명령: cmp dword [rbx+0x30],0 · mov r9,[rbx+0x28]
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitAllowCountOff), 0x30LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitAllowPtrOff), 0x28LL);
}

TEST(actionlimit_decodes_an_entry_by_the_measured_layout) {
    std::uint8_t raw[kLimitEntrySize] = {};
    raw[0x00] = 6;                        // 걸어 둔 쪽 종류 (RideLimit 버프)
    put_u64(raw, 0x08, 0x1122334455ULL);  // 번호
    raw[0x10] = 3;                        // _moveLvLimit
    raw[0x11] = 1;                        // _weaponOutLimit
    raw[0x12] = 1;                        // _rideLimit
    raw[0x13] = 0;                        // _rideLimitByIndoor
    raw[0x14] = 1;                        // _rideOffLimit
    raw[0x15] = 1;                        // _unsetLimitOnSequencerControl
    put_u64(raw, 0x18, 0xAAAA0000ULL);    // 금지 목록
    put_u32(raw, 0x20, 2);
    put_u64(raw, 0x28, 0xBBBB0000ULL);    // 허용 목록
    put_u32(raw, 0x30, 4);

    const ActionLimit e = action_limit_decode(raw);
    CHECK_EQ(static_cast<int>(e.source), 6);
    CHECK_EQ(static_cast<long long>(e.id), 0x1122334455LL);
    CHECK_EQ(static_cast<int>(e.move_lv), 3);
    CHECK(e.weapon_out);
    CHECK(e.ride);
    CHECK(!e.ride_indoor);
    CHECK(e.ride_off);
    CHECK(e.unset_on_seq);
    CHECK_EQ(static_cast<long long>(e.limit_ptr), 0xAAAA0000LL);
    CHECK_EQ(static_cast<long long>(e.limit_n), 2LL);
    CHECK_EQ(static_cast<long long>(e.allow_ptr), 0xBBBB0000LL);
    CHECK_EQ(static_cast<long long>(e.allow_n), 4LL);
}

TEST(actionlimit_flags_are_booleans_not_the_raw_byte) {
    // 0/1 이 아닌 값이 와도(패딩 쓰레기) "켜짐" 으로만 읽는다.
    std::uint8_t raw[kLimitEntrySize] = {};
    raw[0x12] = 0x80;
    const ActionLimit e = action_limit_decode(raw);
    CHECK(e.ride);
    CHECK(!e.ride_off);
}

TEST(actionlimit_decode_of_null_is_empty) {
    const ActionLimit e = action_limit_decode(nullptr);
    CHECK(!e.ride);
    CHECK_EQ(static_cast<long long>(e.allow_n), 0LL);
}

TEST(actionlimit_text_names_every_flag_that_is_on) {
    ActionLimit e;
    e.source = 6;
    e.ride = true;
    e.ride_off = true;
    e.allow_n = 2;
    char buf[256] = {};
    const std::size_t n = action_limit_text(e, buf, sizeof(buf));
    const std::string s(buf, n);
    CHECK(s.find("탑승") != std::string::npos);
    CHECK(s.find("하차") != std::string::npos);
    CHECK(s.find("허용 2") != std::string::npos);
    // 꺼진 것은 안 적는다.
    CHECK(s.find("무기") == std::string::npos);
}

TEST(actionlimit_text_is_bounded_by_the_buffer) {
    ActionLimit e;
    e.weapon_out = e.ride = e.ride_indoor = e.ride_off = e.unset_on_seq = true;
    e.move_lv = 9;
    e.limit_n = 3;
    e.allow_n = 4;
    char buf[8];
    std::memset(buf, 'x', sizeof(buf));
    const std::size_t n = action_limit_text(e, buf, sizeof(buf));
    CHECK(n < sizeof(buf));
    CHECK_EQ(buf[n], '\0');
}

}  // namespace
