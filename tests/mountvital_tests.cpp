// 탈것 체력·스태미나의 **순수 부분**. 게임 없이 전부 태운다.
//
// 실측(2026-09-18, exe 1.0.0.2944 · 게임에서 직접 읽음):
//   블랙스타(드래곤)  체력 2,500,000  스태미나 300,000   게이지 [0]·[12]
//   와이번            체력   600,000  스태미나 150,000   게이지 [0]·[12]
//   무역 마차         체력 5,000,000
//
// 그리고 쓰기 실측 - 체력은 약 2초마다 권위 쪽이 덮고, 스태미나는 안 덮는다.
// 그래서 체력은 매 틱 고정이 필요하다. 그 갈림이 `mount_pin_active` 에 있다.
#include <cstdint>

#include "game/mountvital.h"
#include "harness.h"

namespace {

using cdtb::game::gauge_offset;
using cdtb::game::kGaugeCur;
using cdtb::game::kGaugeMax;
using cdtb::game::kGaugeStride;
using cdtb::game::kGaugeType;
using cdtb::game::kMountVitalMax;
using cdtb::game::kTypeHealth;
using cdtb::game::kTypeStamina;
using cdtb::game::mount_clamp;
using cdtb::game::mount_pin_active;
using cdtb::game::mount_pin_wants;
using cdtb::game::mount_type_safe;
using cdtb::game::MountPin;

TEST(gauge_layout_matches_the_measured_array) {
    // 한 칸만 어긋나면 남의 게이지를 쓴다.
    CHECK_EQ(static_cast<long long>(kGaugeStride), 0x90LL);
    CHECK_EQ(static_cast<long long>(kGaugeType), 0x00LL);
    CHECK_EQ(static_cast<long long>(kGaugeCur), 0x08LL);
    CHECK_EQ(static_cast<long long>(kGaugeMax), 0x18LL);
    CHECK_EQ(static_cast<long long>(gauge_offset(0)), 0LL);
    // 드래곤·와이번 둘 다 스태미나가 [12] 였다.
    CHECK_EQ(static_cast<long long>(gauge_offset(12)), 12LL * 0x90LL);
}

TEST(mount_type_safe_refuses_the_pin_forbidden_types) {
    // player.h 가 "핀 금지" 로 못박은 것들 - 발열·자연발화·탈것 화염.
    CHECK(!mount_type_safe(17));
    CHECK(!mount_type_safe(18));
    CHECK(!mount_type_safe(48));
    // 우리가 쓰는 둘은 통과해야 한다.
    CHECK(mount_type_safe(kTypeHealth));
    CHECK(mount_type_safe(kTypeStamina));
}

TEST(mount_clamp_keeps_values_inside_the_field) {
    CHECK_EQ(mount_clamp(0), 0LL);
    CHECK_EQ(mount_clamp(-1), 0LL);
    CHECK_EQ(mount_clamp(-999999), 0LL);
    CHECK_EQ(mount_clamp(2500000), 2500000LL);   // 드래곤 최대 체력
    CHECK_EQ(mount_clamp(kMountVitalMax), kMountVitalMax);
    CHECK_EQ(mount_clamp(kMountVitalMax + 1), kMountVitalMax);
    // 화면 입력이 넘쳐도 음수로 안 돈다.
    CHECK_EQ(mount_clamp(9223372036854775807LL), kMountVitalMax);
}

TEST(mount_pin_wants_treats_minus_one_as_leave_alone) {
    CHECK(!mount_pin_wants(-1));
    CHECK(mount_pin_wants(0));       // 0 은 "0 으로 써라" 지 "건드리지 마라" 가 아니다
    CHECK(mount_pin_wants(1));
    CHECK(!mount_pin_wants(-2));
}

TEST(mount_pin_active_needs_a_target_and_a_field) {
    MountPin p;
    CHECK(!mount_pin_active(p));           // 대상도 칸도 없다

    p.handle = 0xB0100004;                 // 실측 핸들(블랙스타)
    CHECK(!mount_pin_active(p));           // 대상만 있고 쓸 칸이 없다

    p.hp_cur = 2500000;
    CHECK(mount_pin_active(p));

    p.handle = 0;                          // 대상이 사라지면 꺼진다
    CHECK(!mount_pin_active(p));
}

TEST(mount_pin_active_accepts_any_single_field) {
    for (int which = 0; which < 4; ++which) {
        MountPin p;
        p.handle = 0xB0100004;
        if (which == 0) p.hp_cur = 1;
        if (which == 1) p.hp_max = 1;
        if (which == 2) p.sta_cur = 1;
        if (which == 3) p.sta_max = 1;
        CHECK(mount_pin_active(p));
    }
}

TEST(mount_pin_active_accepts_zero_as_a_real_value) {
    // "체력을 0 으로 고정" 은 쓸 수 있는 설정이다. -1 과 구분돼야 한다.
    MountPin p;
    p.handle = 0xB0100004;
    p.hp_cur = 0;
    CHECK(mount_pin_active(p));
}

}  // namespace
