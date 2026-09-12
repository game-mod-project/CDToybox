#include "game/inventory.h"
#include "harness.h"

using cdtb::game::BagPlan;
using cdtb::game::kBagEngineMax;
using cdtb::game::kBagTargetMax;
using cdtb::game::plan_bag_expand;

namespace {
// 라이브 실측값(2026-09-12). cap/sum/a/b = 컨테이너 +0x14/+0x16/+0x18/+0x1A.
//   가방(종류 1)   240 / 190 / 190 / 0     -> 기본 50
//   보관함(종류 7) 440 / 200 / 0   / 200   -> 기본 240
constexpr int kSlots = 1460;   // +0x08, 18개 컨테이너 전부 같았다
}  // namespace

TEST(bag_plan_derives_base_from_the_sum_not_one_branch) {
    // **2026-09-05 사고의 핵심.** 옛 코드는 기본 = cap - b 였는데, 가방은 확장을
    // a(+0x18)에 담아 b = 0 이라 240 이 나왔다(정답 50). 합계로 유도해야 한다.
    const BagPlan bag = plan_bag_expand(240, 190, 190, 0, kSlots, 700);
    CHECK(bag.apply);
    CHECK(bag.base == 50);
    const BagPlan store = plan_bag_expand(440, 200, 0, 200, kSlots, 700);
    CHECK(store.apply);
    CHECK(store.base == 240);
}

TEST(bag_plan_leaves_the_other_branch_alone) {
    // +0x18(a)은 건드리지 않는다. 그래야 엔진이 합계를 sum 으로 재계산하든 max 로
    // 재계산하든 결과가 목표 이하가 된다(max 면 오히려 작아진다 - 안전한 쪽).
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 700);
    CHECK(p.expand_b == 700 - 50 - 190);   // = 460
    CHECK(p.sum == 190 + p.expand_b);      // = 650
    CHECK(p.capacity == 700);
    CHECK(p.base + p.sum == p.capacity);
}

TEST(bag_plan_clamps_to_the_screen_max) {
    // 700 이 화면 상한이다. 그 위를 넣어도 700 으로 잘린다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 9999);
    CHECK(p.apply);
    CHECK(p.capacity == kBagTargetMax);
    CHECK(kBagTargetMax <= kBagEngineMax);
}

TEST(bag_plan_never_exceeds_the_physical_array) {
    // 물리 배열(+0x08)보다 크게 잡지 않는다. 용량까지 도는 순회가 배열 밖으로
    // 나가는 것이 2026-09-05 의 "가방 획득 시 팅김" 이었다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, 300, 700);
    CHECK(p.apply);
    CHECK(p.capacity == 300);
}

TEST(bag_plan_skips_shapes_it_does_not_understand) {
    // 용량이 확장 합계보다 작다 - 우리 모델이 아니다.
    CHECK(!plan_bag_expand(100, 200, 0, 0, kSlots, 700).apply);
    // 두 갈래가 다 차 있는데 합이 합계와 다르다.
    CHECK(!plan_bag_expand(500, 200, 150, 100, kSlots, 700).apply);
    // 기본 슬롯이 0 이하로 나온다.
    CHECK(!plan_bag_expand(190, 190, 190, 0, kSlots, 700).apply);
    // 음수·0 칸.
    CHECK(!plan_bag_expand(-1, 0, 0, 0, kSlots, 700).apply);
    CHECK(!plan_bag_expand(240, 190, 190, 0, 0, 700).apply);
    // 건너뛸 때는 이유를 남긴다(로그·화면에 쓴다).
    CHECK(plan_bag_expand(100, 200, 0, 0, kSlots, 700).skip[0] != '\0');
}

TEST(bag_plan_does_nothing_when_already_there) {
    // 이미 목표면 쓰지 않는다 - 같은 값을 다시 쓰며 로그를 더럽히지 않는다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 700);
    const BagPlan again =
        plan_bag_expand(p.capacity, p.sum, 190, p.expand_b, kSlots, 700);
    CHECK(!again.apply);
    CHECK(again.skip[0] != '\0');
}

TEST(bag_plan_refuses_to_shrink) {
    // 목표가 기본 슬롯보다 작으면 건드리지 않는다. 줄이는 것은 복원 경로가 한다
    // (원본을 기억해 두고 그대로 되돌린다 - 옛 restore 는 확장을 0 으로 써서
    // 가방의 190 을 날렸고 그건 복원이 아니었다).
    CHECK(!plan_bag_expand(240, 190, 190, 0, kSlots, 10).apply);
    // 이미 목표보다 큰 경우도 건드리지 않는다.
    CHECK(!plan_bag_expand(900, 850, 850, 0, kSlots, 700).apply);
}

TEST(bag_plan_target_zero_changes_nothing) {
    // 0 은 "복원" 이 아니라 "바꿀 것 없음" 이다.
    CHECK(!plan_bag_expand(240, 190, 190, 0, kSlots, 0).apply);
}
