// 탈것 소환 쿨다운·시간제한 판정. 순수 함수라 가짜 값으로 전부 태운다.
//
// 실측 값(2026-09-15, `gamedata/characterinfo` 를 파일에서 직접 읽어 확인):
//   드래곤·ATAG  쿨 3600 · 제한 600
//   곰·사슴      쿨  300 · 제한   0
//   낙타·배      쿨    0 · 제한   0
//
// (한때 여기 있던 `wheel_merge` 시험들은 그 기능과 함께 걷어냈다 - 메인 휠에
// 카테고리를 얹는 것은 게임 검증에서 폐기됐다. reserveslot.h 의 주석 참조.)
#include "game/clan.h"
#include "game/reserveslot.h"
#include "harness.h"

namespace {

// 메인 휠 허용 목록 합치기. 실측(2026-09-15): 메인 [1,5] · ATAG [3,4] · 드래곤 [2].
TEST(wheel_merge_adds_dragon_and_atag) {
    using cdtb::game::kWheelMaxCats;
    using cdtb::game::wheel_merge;
    const int base[] = {1, 5};
    const int add[] = {2, 3, 4};
    int out[kWheelMaxCats] = {};
    const int n = wheel_merge(base, 2, add, 3, out, kWheelMaxCats);
    CHECK_EQ(n, 5);
    // 원래 것이 앞에 그대로 남아야 한다 - 게임이 순서에 기대는지 모른다.
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 5);
    CHECK_EQ(out[2], 2);
}

TEST(wheel_merge_is_idempotent_and_dedupes) {
    using cdtb::game::kWheelMaxCats;
    using cdtb::game::wheel_merge;
    const int base[] = {1, 5};
    const int add[] = {2, 2, 3};
    int a[kWheelMaxCats] = {};
    const int n1 = wheel_merge(base, 2, add, 3, a, kWheelMaxCats);
    CHECK_EQ(n1, 4);   // 안의 중복도 한 번만
    int b[kWheelMaxCats] = {};
    // 두 번 걸어도 늘어나면 안 된다 - 화면이 토글을 여러 번 누른다.
    CHECK_EQ(wheel_merge(a, n1, add, 3, b, kWheelMaxCats), n1);
}

TEST(wheel_merge_truncates_instead_of_overflowing) {
    using cdtb::game::wheel_merge;
    const int base[] = {1, 5};
    const int add[] = {2, 3, 4};
    int out[3] = {};
    // 들어가는 만큼만 넣고 **그 개수를 정직하게** 돌려준다.
    CHECK_EQ(wheel_merge(base, 2, add, 3, out, 3), 3);
    CHECK_EQ(out[2], 2);
}

TEST(wheel_merge_rejects_bad_output) {
    using cdtb::game::wheel_merge;
    const int base[] = {1};
    int out[1] = {};
    CHECK_EQ(wheel_merge(base, 1, nullptr, 0, nullptr, 4), 0);
    CHECK_EQ(wheel_merge(base, 1, nullptr, 0, out, 0), 0);
}

TEST(mount_needs_free_picks_timed_mounts) {
    using cdtb::game::mount_needs_free;
    // 드래곤: 쿨다운도 시간제한도 걸려 있다
    CHECK(mount_needs_free(16984, 3600, 600));
    // 곰: 쿨다운만 걸려 있다
    CHECK(mount_needs_free(16979, 300, 0));
    // 낙타/배: 아무것도 안 걸려 있다 - 건드릴 이유가 없다
    CHECK(!mount_needs_free(16978, 0, 0));
    // 탈것이 아니면 쿨다운이 있어도 건드리지 않는다
    CHECK(!mount_needs_free(0, 3600, 600));
}

TEST(mount_needs_free_is_idempotent_after_patch) {
    using cdtb::game::kCoolTimeFree;
    using cdtb::game::kDurationFree;
    using cdtb::game::mount_needs_free;
    // 우리가 쓴 값이 들어간 뒤에는 다시 손댈 것이 없어야 한다 - 안 그러면
    // 백업이 우리 값을 "원본" 으로 덮어쓴다.
    CHECK(!mount_needs_free(16984, kCoolTimeFree, kDurationFree));
    CHECK(!mount_needs_free(16979, kCoolTimeFree, 0));
}

TEST(vehicle_place_gated_picks_ground_checked) {
    using cdtb::game::vehicle_place_gated;
    CHECK(vehicle_place_gated(30.0f));    // 드래곤
    CHECK(!vehicle_place_gated(0.0f));    // 와이번(정상 동작)
    CHECK(!vehicle_place_gated(-1.0f));   // 음수는 안 건드린다
    // 우리가 쓴 뒤에는 다시 손댈 것이 없어야 한다(백업이 우리 값을 덮지 않게).
    CHECK(!vehicle_place_gated(0.0f));
}

// 휠 색인 옮기기의 여유 판정. 실측(2026-09-15): 타입 5 벡터 13/18(여유 5),
// 타입 9 벡터 7/8(여유 1), 타입 2 벡터 2/2(여유 0).
TEST(reindex_has_room_needs_spare_capacity) {
    using cdtb::game::reindex_has_room;
    CHECK(reindex_has_room(13, 18));   // 특수 탑승물 - 들어간다
    CHECK(reindex_has_room(7, 8));
    CHECK(!reindex_has_room(2, 2));    // 꽉 참 - 재할당이 필요하므로 안 건드린다
    CHECK(!reindex_has_room(0, 0));
    // 개수가 용량보다 큰 말도 안 되는 값이면 손대지 않는다.
    CHECK(!reindex_has_room(9, 8));
}

// 2026-09-15 사고의 회귀 시험. 대상을 "메인 목록에 없는 것" 으로 잡았더니,
// 얹기가 메인 목록에 2·3·4 를 붙여 놓은 뒤로 드래곤은 빠지고 반려 동물 열여섯이
// 대상이 됐다(새끼 와이번이 휠로 올라가고 반려 동물에서 사라짐).
//
// 실측 목록: 메인 슬롯 1000006 = [1,5] · 정비 1000019 = [3,4] · 드래곤 1000020 = [2]
// 타입행 1 말 · 5 특수 탑승물 · 2 드래곤 · 3 A.T.A.G. · 9·11·14·15·18 반려·용병
TEST(disguise_role_before_wheel_extend) {
    using cdtb::game::disguise_role;
    using cdtb::game::DisguiseRole;
    const int main_cats[] = {1, 5};
    const int lock[] = {2, 3, 4};
    CHECK(disguise_role(main_cats, 2, lock, 3, 2) == DisguiseRole::Target);
    CHECK(disguise_role(main_cats, 2, lock, 3, 3) == DisguiseRole::Target);
    CHECK(disguise_role(main_cats, 2, lock, 3, 1) == DisguiseRole::DonorCat);
    CHECK(disguise_role(main_cats, 2, lock, 3, 5) == DisguiseRole::DonorCat);
    CHECK(disguise_role(main_cats, 2, lock, 3, 9) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 2, lock, 3, 18) == DisguiseRole::None);
}

TEST(disguise_role_survives_wheel_extend) {
    using cdtb::game::disguise_role;
    using cdtb::game::DisguiseRole;
    // 얹기를 켠 뒤의 메인 목록. 답이 위와 **한 칸도 달라지면 안 된다.**
    const int main_cats[] = {1, 5, 2, 3, 4};
    const int lock[] = {2, 3, 4};
    CHECK(disguise_role(main_cats, 5, lock, 3, 2) == DisguiseRole::Target);
    CHECK(disguise_role(main_cats, 5, lock, 3, 3) == DisguiseRole::Target);
    CHECK(disguise_role(main_cats, 5, lock, 3, 4) == DisguiseRole::Target);
    CHECK(disguise_role(main_cats, 5, lock, 3, 1) == DisguiseRole::DonorCat);
    CHECK(disguise_role(main_cats, 5, lock, 3, 5) == DisguiseRole::DonorCat);
    // 사고가 난 자리 - 반려 동물·용병은 무슨 일이 있어도 대상이 아니다.
    CHECK(disguise_role(main_cats, 5, lock, 3, 9) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 11) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 14) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 15) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 18) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 19) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 5, lock, 3, 0) == DisguiseRole::None);
}

TEST(disguise_role_handles_empty_lists) {
    using cdtb::game::disguise_role;
    using cdtb::game::DisguiseRole;
    const int main_cats[] = {1, 5};
    const int lock[] = {2};
    CHECK(disguise_role(nullptr, 0, nullptr, 0, 2) == DisguiseRole::None);
    CHECK(disguise_role(main_cats, 2, nullptr, 0, 5) == DisguiseRole::DonorCat);
    CHECK(disguise_role(nullptr, 0, lock, 1, 2) == DisguiseRole::Target);
}

// 기증자를 고르는 기준. 지면 거리 검사로는 말과 와이번이 안 갈려(둘 다 0) 말이
// 기증자로 뽑혔고, 드래곤 자리에서 말이 나왔다(2026-09-15 17:23).
// 실측 _maxAllowableHeight: 드래곤 1350 · 와이번 1350 · 말 3.40282e+38.
TEST(vehicle_flies_needs_a_ceiling) {
    using cdtb::game::vehicle_flies;
    CHECK(vehicle_flies(1350.0f));              // 드래곤·와이번
    CHECK(!vehicle_flies(3.40282347e+38f));     // 말 - 상한 없음 = 땅 것
    CHECK(!vehicle_flies(0.0f));
    CHECK(!vehicle_flies(-1.0f));
}

// "호출할 수 없는 위치입니다" 의 진짜 출처. 살아 있는 게임에서 잰 값
// (2026-09-15): A.T.A.G. 0개 -> 어디서나 성공 · 와이번 2개{4,3} -> 정상 ·
// 블랙스타 1개{3} -> 거부. 개수만 0 으로 두면 A.T.A.G. 와 같은 상태가 된다.
TEST(spawn_voxel_gated_frees_only_restricted_species) {
    using cdtb::game::spawn_voxel_gated;
    CHECK(spawn_voxel_gated(1));    // 블랙스타 - 복셀 3 하나만 받는다
    CHECK(spawn_voxel_gated(2));    // 와이번
    CHECK(!spawn_voxel_gated(0));   // A.T.A.G. - 이미 자유, 손대지 않는다
}

}  // namespace
