// 보스룸 탈것 호출(`bosscall`)의 순수 부분.
//
// 여기서 틀리면 두 가지가 무너진다: (1) 엉뚱한 조건을 넘겨 게임 판정을 바꾸거나
// (2) 되돌릴 때 **남의 항목에 개수를 써 넣어** 게임이 목록 밖을 읽는다. 그래서
// 배치(항목 0x38바이트)와 되돌리기 조건을 못박아 둔다 — 전부 exe 1.0.0.2944 에서
// 뜬 값이다(specs/2026-09-21-boss-room-action-limit.md).
#include <cstdint>
#include <cstring>
#include <string>

#include "game/bosscall.h"
#include "harness.h"

namespace {

using cdtb::game::action_limit_decode;
using cdtb::game::action_limit_text;
using cdtb::game::ActionLimit;
using cdtb::game::bosscall_can_restore;
using cdtb::game::bosscall_hide_plan;
using cdtb::game::bosscall_targets;
using cdtb::game::kLimitEntrySize;
using cdtb::game::kMaxLimitEntries;
using cdtb::game::LimitShape;

void put_u32(std::uint8_t* b, std::size_t off, std::uint32_t v) {
    std::memcpy(b + off, &v, 4);
}
void put_u64(std::uint8_t* b, std::size_t off, std::uint64_t v) {
    std::memcpy(b + off, &v, 8);
}

TEST(bosscall_constants_are_the_measured_ones) {
    // 조건 0xED 의 평가 함수와 그 안의 대조 자리. 갱신 때 먼저 빨개져야 한다.
    CHECK_EQ(static_cast<long long>(cdtb::game::kSkillCheckFnRva), 0x362DF0LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kSkillCheckCondType), 0xEDLL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kCallVehicleSkillKey), 1505LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kCallDragonSkillKey), 1506LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kCtlVtRva), 0x55B53C8LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kCtlLimitOff), 0x120LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitArrayOff), 0xD8LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitCountOff), 0xE0LL);
    CHECK_EQ(static_cast<long long>(kLimitEntrySize), 0x38LL);
    // 0x362DF0 이 가리는 칸을 읽는 두 명령: cmp dword [rbx+0x30],0 · mov r9,[rbx+0x28]
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitAllowCountOff), 0x30LL);
    CHECK_EQ(static_cast<long long>(cdtb::game::kLimitAllowPtrOff), 0x28LL);
}

TEST(bosscall_decodes_an_entry_by_the_measured_layout) {
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

TEST(bosscall_flags_are_booleans_not_the_raw_byte) {
    // 0/1 이 아닌 값이 와도(패딩 쓰레기) "켜짐" 으로만 읽는다.
    std::uint8_t raw[kLimitEntrySize] = {};
    raw[0x12] = 0x80;
    const ActionLimit e = action_limit_decode(raw);
    CHECK(e.ride);
    CHECK(!e.ride_off);
}

TEST(bosscall_targets_only_the_two_call_skills_under_condition_0xED) {
    CHECK(bosscall_targets(0xED, 1505));
    CHECK(bosscall_targets(0xED, 1506));
    CHECK(!bosscall_targets(0xED, 1504));
    CHECK(!bosscall_targets(0xED, 1507));
    // 같은 스킬 키라도 다른 조건 종류면 우리 것이 아니다.
    CHECK(!bosscall_targets(0xEC, 1505));
    CHECK(!bosscall_targets(0x24A, 1505));
}

TEST(bosscall_hides_only_entries_that_carry_an_allow_list) {
    ActionLimit e[4];
    e[0].allow_n = 0;
    e[1].allow_n = 2;
    e[2].allow_n = 0;
    e[3].allow_n = 1;
    int out[kMaxLimitEntries] = {};
    const int n = bosscall_hide_plan(e, 4, out, kMaxLimitEntries);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 3);
}

TEST(bosscall_touches_nothing_when_the_list_is_empty_or_too_long) {
    ActionLimit e[kMaxLimitEntries + 1];
    for (auto& x : e) x.allow_n = 1;
    int out[kMaxLimitEntries + 1] = {};
    CHECK_EQ(bosscall_hide_plan(e, 0, out, kMaxLimitEntries), 0);
    // 평소보다 훨씬 많으면 우리가 모르는 상태다 - 손대지 않는다.
    CHECK_EQ(bosscall_hide_plan(e, kMaxLimitEntries + 1, out, kMaxLimitEntries + 1),
             0);
    CHECK_EQ(bosscall_hide_plan(nullptr, 3, out, kMaxLimitEntries), 0);
}

TEST(bosscall_hide_plan_never_writes_past_the_output) {
    ActionLimit e[4];
    for (auto& x : e) x.allow_n = 1;
    int out[2] = {-1, -1};
    CHECK_EQ(bosscall_hide_plan(e, 4, out, 2), 2);
    CHECK_EQ(out[0], 0);
    CHECK_EQ(out[1], 1);
}

LimitShape shape(std::uintptr_t array, std::uint32_t count) {
    LimitShape s;
    s.array = array;
    s.count = count;
    for (std::uint32_t i = 0; i < count && i < kMaxLimitEntries; ++i) {
        s.allow_ptr[i] = 0x5000 + i * 0x10;
    }
    return s;
}

TEST(bosscall_restores_only_when_nothing_moved) {
    const LimitShape before = shape(0x1000, 3);
    CHECK(bosscall_can_restore(before, shape(0x1000, 3)));

    // 배열이 옮겨졌다(재할당).
    CHECK(!bosscall_can_restore(before, shape(0x2000, 3)));
    // 항목이 빠졌다 - 뒤 항목이 앞으로 당겨졌을 수 있다(0x8BF9A.. 의 제거 코드).
    CHECK(!bosscall_can_restore(before, shape(0x1000, 2)));
    // 개수는 같은데 한 항목의 목록이 바뀌었다(빠지고 새로 붙었다).
    LimitShape swapped = shape(0x1000, 3);
    swapped.allow_ptr[1] = 0x9999;
    CHECK(!bosscall_can_restore(before, swapped));
}

TEST(bosscall_text_names_every_flag_that_is_on) {
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

TEST(bosscall_text_is_bounded_by_the_buffer) {
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
