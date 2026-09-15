// 탈것 소환 쿨다운·시간제한 판정. 순수 함수라 가짜 값으로 전부 태운다.
//
// 실측 값(2026-09-15, `gamedata/characterinfo` 를 파일에서 직접 읽어 확인):
//   드래곤·ATAG  쿨 3600 · 제한 600
//   곰·사슴      쿨  300 · 제한   0
//   낙타·배      쿨    0 · 제한   0
//
// (한때 여기 있던 `wheel_merge` 시험들은 그 기능과 함께 걷어냈다 - 메인 휠에
// 카테고리를 얹는 것은 게임 검증에서 폐기됐다. reserveslot.h 의 주석 참조.)
#include "game/reserveslot.h"
#include "harness.h"

namespace {

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

}  // namespace
