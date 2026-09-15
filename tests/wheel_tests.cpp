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

}  // namespace
