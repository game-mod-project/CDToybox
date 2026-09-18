// 탈것 체력·스태미나의 **순수 부분**. 게임 없이 전부 태운다.
//
// 실측(2026-09-18, exe 1.0.0.2944 · 게임에서 직접 읽음):
//   블랙스타(드래곤)  체력 2,500,000  스태미나 300,000   게이지 [0]·[12]
//   와이번            체력   600,000  스태미나 150,000   게이지 [0]·[12]
//   무역 마차         체력 5,000,000
//
// 쓰기 실측은 **두 번** 했다. 처음 것("체력은 2초마다 덮이고 스태미나는 안
// 덮인다")은 틀렸다 - 사용자가 "바로 복구된다" 고 해 다시 파 보니, 우리가 쓰던
// 배열이 **거울**이고 진짜 값은 서버 realm 사본에 있었다(mountvital.h 머리).
// 그래서 여기 상수에는 권위 사본으로 가는 길과 기준최대 칸이 함께 못박힌다.
#include <cstdint>
#include <string>

#include "game/mountvital.h"
#include "harness.h"

namespace {

using cdtb::game::gauge_offset;
using cdtb::game::kGaugeBaseMax;
using cdtb::game::kGaugeCur;
using cdtb::game::kGaugeMax;
using cdtb::game::kGaugeStride;
using cdtb::game::kGaugeType;
using cdtb::game::kMountVitalMax;
using cdtb::game::kMvActorHandle;
using cdtb::game::kMvArray;
using cdtb::game::kMvRoot;
using cdtb::game::kMvServerStatusClass;
using cdtb::game::kMvStatusActor;
using cdtb::game::kTypeHealth;
using cdtb::game::kTypeStamina;
using cdtb::game::mount_clamp;
using cdtb::game::mount_handle_plausible;
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

// ------------------------------------------------------- 권위 사본으로 가는 길

TEST(authority_path_offsets_match_the_measured_chain) {
    // 실측(2026-09-18): A.T.A.G. 의 서버 상태 컴포넌트에서
    //   +0x08 -> 서버 액터 0x2BD880E0400 (+0x60 = 핸들 0xB0100003, 클라와 같다)
    //   +0x18 -> root -> +0x58 = 권위 게이지 배열 0x2BDA4029000
    // 한 칸만 어긋나면 엉뚱한 객체에 쓴다.
    CHECK_EQ(static_cast<long long>(kMvStatusActor), 0x08LL);
    CHECK_EQ(static_cast<long long>(kMvActorHandle), 0x60LL);
    CHECK_EQ(static_cast<long long>(kMvRoot), 0x18LL);
    CHECK_EQ(static_cast<long long>(kMvArray), 0x58LL);
}

TEST(authority_class_is_the_server_status_component) {
    // 클래스 이름은 RVA 와 달리 게임 갱신에 안 흔들린다 - 그래서 이 길을 골랐다.
    // 이름이 바뀌면 여기서 먼저 걸린다.
    CHECK(std::string(kMvServerStatusClass) ==
          ".?AVServerStatusActorComponent@pa@@");
}

TEST(base_max_is_a_separate_slot_from_max) {
    // 최대는 두 칸이다. 같은 칸으로 적으면 기준값이 안 써져 언젠가 되돌아간다.
    CHECK_EQ(static_cast<long long>(kGaugeBaseMax), 0x58LL);
    CHECK(kGaugeBaseMax != kGaugeMax);
    CHECK(kGaugeBaseMax < kGaugeStride);   // 같은 항목 안에 있다
}

TEST(mount_handle_plausible_accepts_the_three_namespaces) {
    // 실측에서 본 것: 일반 0xB010, 사용자 0xA010(고용주), 0x9010(사용자 액터).
    CHECK(mount_handle_plausible(0xB0100003));
    CHECK(mount_handle_plausible(0xA0100001));
    CHECK(mount_handle_plausible(0x90100001));
}

TEST(mount_handle_plausible_rejects_junk) {
    // 서버 컴포넌트 후보에는 vtable 값을 우연히 담은 메모리가 섞인다. 그것을
    // 걸러내는 것이 이 판정의 일이다 - 통과시키면 남의 객체에 쓴다.
    CHECK(!mount_handle_plausible(0));
    CHECK(!mount_handle_plausible(0xFFFFFFFF));
    CHECK(!mount_handle_plausible(0x00000003));
    CHECK(!mount_handle_plausible(0xB0110003));   // 한 자리 다르다
    CHECK(!mount_handle_plausible(0xC0100003));
}

}  // namespace
