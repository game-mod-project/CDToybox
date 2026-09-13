#include "game/inventory.h"
#include "harness.h"

using cdtb::game::BagPlan;
using cdtb::game::kBagEngineMax;
using cdtb::game::kBagTargetMax;
using cdtb::game::bag_kind_selected;
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

TEST(bag_kind_filter_never_touches_the_small_slots) {
    // **옛 사고의 나머지 절반.** 산술만 덮으면 거르개 한 줄을 지워도 시험이 전부
    // 통과한다 - 그런데 화면 툴팁은 바로 그 거르개를 "작은 칸을 안 건드린다" 고
    // 약속한다. 약속을 시험이 지키게 한다(리뷰 지적 8).
    //
    // 2026-09-13 실측 18개 컨테이너의 종류와 용량:
    //   1 가방 240 / 7 보관함 440 / 4 300 / 9 300 / 11 300 / 8·12 240
    //   0·2·3 20 / 10·19 50 / 13 5 / 14~18 10
    CHECK(bag_kind_selected(1, false));   // 가방은 언제나
    CHECK(bag_kind_selected(1, true));
    CHECK(!bag_kind_selected(7, false));  // 보관함은 켰을 때만
    CHECK(bag_kind_selected(7, true));
    CHECK(bag_kind_selected(9, true));
    CHECK(bag_kind_selected(11, true));
    // 종류 4 는 혼자 +0x20 에 8칸짜리 보조 배열을 단다(실측). 정체를 확인할
    // 때까지 뺀다 - 용량만 올리고 그쪽을 두는 것은 모르는 모양을 건드리는 것이다.
    CHECK(!bag_kind_selected(4, true));
    // 작은 칸(용량 5·10·20·50)과 나머지는 어느 쪽이든 절대 건드리지 않는다.
    for (const std::uint16_t k :
         {0, 2, 3, 5, 6, 8, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 99}) {
        CHECK(!bag_kind_selected(k, false));
        CHECK(!bag_kind_selected(k, true));
    }
}

TEST(bag_plan_never_produces_a_capacity_above_the_limit) {
    // 속성 시험. 작은 범위를 전수로 돌며 "쓸 값이 나왔다면 반드시 성립해야 하는 것"
    // 을 확인한다. 핵심은 마지막의 a + b == sum 이다 - 모델 검사에 구멍이
    // 생기면 그 줄에서 걸린다(나머지는 정의상 성립하는 항등식이다).
    for (int a = 0; a <= 300; a += 50) {
        for (int b = 0; b <= 300; b += 50) {
            for (int extra = -50; extra <= 50; extra += 25) {
                const int sum = a + b + extra;
                for (int base = 1; base <= 300; base += 100) {
                    const int cap = base + sum;
                    for (const int target : {50, 240, 300, 700, 9999}) {
                        const auto p =
                            plan_bag_expand(cap, sum, a, b, 1460, target);
                        if (!p.apply) continue;
                        CHECK(p.capacity <= cdtb::game::kBagEngineMax);
                        CHECK(p.capacity <= cdtb::game::kBagTargetMax);
                        CHECK(p.capacity == p.base + p.sum);
                        CHECK(p.sum == a + p.expand_b);
                        CHECK(p.expand_b >= 0);
                        CHECK(p.base > 0);
                        // **이 한 줄이 지적 2 의 회귀를 잡는다** - 쓸 값이
                        // 나왔다면 입력 모델이 성립했어야 한다. 나머지는
                        // plan 안에서 정의상 성립하는 항등식이라 못 잡는다
                        // (리뷰 재검토 B-8).
                        CHECK(a + b == sum);
                    }
                }
            }
        }
    }
}

TEST(bag_plan_rejects_a_hidden_third_branch) {
    // 두 갈래 중 하나가 0 이어도 합이 맞지 않으면 건드리지 않는다. 예전 검사는
    // 두 갈래가 **모두** 0 이 아닐 때만 걸려, 이 모양이 그냥 통과했고 엔진
    // 재계산에서 732 를 넘길 수 있었다(리뷰 지적 2).
    CHECK(!plan_bag_expand(500, 400, 0, 0, 1460, 700).apply);
    CHECK(!plan_bag_expand(500, 400, 0, 300, 1460, 700).apply);
    // 합이 맞으면 정상 동작해야 한다(검사를 조이다 쓰던 것을 잃지 않았는지).
    CHECK(plan_bag_expand(500, 400, 400, 0, 1460, 700).apply);
}
