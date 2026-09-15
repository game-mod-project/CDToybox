// 탈것 휠 허용 목록 합치기. 순수 함수라 가짜 값으로 전부 태운다.
//
// 이 계산이 틀리면 게임의 정적 표에 쓰레기 카테고리가 들어가거나(중복·잘림)
// 원래 있던 것을 잃는다. 실측 값(2026-09-15, `gamedata/reserveslot`):
//   메인 휠  [1, 5]      (0x4E 일반 탈것 · 0x51 지상 차량)
//   드래곤   [2]         (0x4F)
//   ATAG     [3, 4]      (0x50 · 0x52)
#include "game/reserveslot.h"
#include "harness.h"

using cdtb::game::kWheelMaxCats;
using cdtb::game::wheel_merge;

namespace {

TEST(wheel_merge_adds_dragon_and_atag) {
    const int base[] = {1, 5};
    const int add[] = {2, 3, 4};
    int out[kWheelMaxCats] = {};
    const int n = wheel_merge(base, 2, add, 3, out, kWheelMaxCats);
    CHECK_EQ(n, 5);
    // 원래 것이 앞에 그대로 남아야 한다 - 게임이 순서에 기대는지 모른다.
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 5);
    CHECK_EQ(out[2], 2);
    CHECK_EQ(out[3], 3);
    CHECK_EQ(out[4], 4);
}

TEST(wheel_merge_skips_duplicates) {
    const int base[] = {1, 5, 2};
    const int add[] = {2, 3};
    int out[kWheelMaxCats] = {};
    const int n = wheel_merge(base, 3, add, 2, out, kWheelMaxCats);
    CHECK_EQ(n, 4);
    CHECK_EQ(out[3], 3);
}

TEST(wheel_merge_is_idempotent) {
    const int base[] = {1, 5};
    const int add[] = {2, 3, 4};
    int a[kWheelMaxCats] = {};
    const int n1 = wheel_merge(base, 2, add, 3, a, kWheelMaxCats);
    int b[kWheelMaxCats] = {};
    const int n2 = wheel_merge(a, n1, add, 3, b, kWheelMaxCats);
    // 두 번 걸어도 늘어나면 안 된다 - 화면이 토글을 여러 번 누른다.
    CHECK_EQ(n2, n1);
    for (int i = 0; i < n1; ++i) CHECK_EQ(a[i], b[i]);
}

TEST(wheel_merge_add_with_internal_duplicates) {
    const int base[] = {1};
    const int add[] = {2, 2, 2};
    int out[kWheelMaxCats] = {};
    const int n = wheel_merge(base, 1, add, 3, out, kWheelMaxCats);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[1], 2);
}

TEST(wheel_merge_truncates_instead_of_overflowing) {
    const int base[] = {1, 5};
    const int add[] = {2, 3, 4};
    int out[3] = {};
    const int n = wheel_merge(base, 2, add, 3, out, 3);
    // 들어가는 만큼만 넣고 **그 개수를 정직하게** 돌려준다.
    CHECK_EQ(n, 3);
    CHECK_EQ(out[2], 2);
}

TEST(wheel_merge_base_alone_can_overflow) {
    const int base[] = {1, 2, 3, 4};
    int out[2] = {};
    const int n = wheel_merge(base, 4, nullptr, 0, out, 2);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 1);
    CHECK_EQ(out[1], 2);
}

TEST(wheel_merge_rejects_bad_output) {
    const int base[] = {1};
    int out[1] = {};
    CHECK_EQ(wheel_merge(base, 1, nullptr, 0, nullptr, 4), 0);
    CHECK_EQ(wheel_merge(base, 1, nullptr, 0, out, 0), 0);
}

TEST(wheel_merge_empty_base_takes_everything) {
    const int add[] = {2, 3};
    int out[kWheelMaxCats] = {};
    const int n = wheel_merge(nullptr, 0, add, 2, out, kWheelMaxCats);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 2);
    CHECK_EQ(out[1], 3);
}

}  // namespace
