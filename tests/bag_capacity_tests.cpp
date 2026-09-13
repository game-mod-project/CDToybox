#include "game/inventory.h"
#include "harness.h"

using cdtb::game::BagPlan;
using cdtb::game::kBagBranchA;
using cdtb::game::kBagBranchAuto;
using cdtb::game::kBagBranchB;
using cdtb::game::kBagEngineMax;
using cdtb::game::kBagTargetMax;
using cdtb::game::bag_kind_branch;
using cdtb::game::bag_kind_cap;
using cdtb::game::bag_kind_rules;
using cdtb::game::bag_kind_selected;
using cdtb::game::bag_resolve_branch;
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

TEST(bag_plan_writes_the_branch_you_choose) {
    // **2026-09-13.** B(+0x1A)에 넣은 60 이 세이브·로드에서 사라졌다. 어느 칸이
    // 저장되는 쪽인지 배포를 거듭하지 않고 가리려고 칸을 고를 수 있게 했다.
    // 어느 쪽을 고르든 반대 칸은 그대로 두고, 결과 용량은 같아야 한다.
    const BagPlan a = plan_bag_expand(240, 190, 190, 0, kSlots, 700, kBagBranchA);
    CHECK(a.apply);
    CHECK(a.branch == kBagBranchA);
    CHECK(a.other == 0);            // +0x1A 는 그대로
    CHECK(a.expand == 650);         // +0x18 이 190 -> 650
    CHECK(a.sum == 650);
    CHECK(a.capacity == 700);

    const BagPlan b = plan_bag_expand(240, 190, 190, 0, kSlots, 700, kBagBranchB);
    CHECK(b.apply);
    CHECK(b.branch == kBagBranchB);
    CHECK(b.other == 190);          // +0x18 은 그대로
    CHECK(b.expand == 460);         // +0x1A 가 0 -> 460
    CHECK(b.sum == 650);
    CHECK(b.capacity == 700);

    // 기본값은 A 다 - 가방의 기존 확장이 거기 있고, B 는 리로드에서 사라진 칸이다.
    const BagPlan d = plan_bag_expand(240, 190, 190, 0, kSlots, 700);
    CHECK(d.branch == kBagBranchA);
    CHECK(d.expand == a.expand);
}

TEST(bag_plan_branch_a_grows_the_bags_own_expansion) {
    // 사용자가 실제로 쓸 값(300)에서, A 는 가방이 원래 갖고 있던 190 을 250 으로
    // 키우고 B 는 0 인 채로 둔다. 이 모양이 "정당하게 산 확장" 과 구분되지 않는
    // 상태이고, 그래서 저장에 남을 가능성이 있는 쪽이다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 300, kBagBranchA);
    CHECK(p.apply);
    CHECK(p.expand == 250);
    CHECK(p.other == 0);
    CHECK(p.capacity == 300);
}

TEST(bag_plan_leaves_the_other_branch_alone) {
    // 고르지 않은 칸은 계획에 값으로 실려 나갈 뿐 바뀌지 않는다. 그래야 엔진이
    // 합계를 sum 으로 재계산하든 max 로 재계산하든 결과가 목표 이하가 된다
    // (max 면 오히려 작아진다 - 안전한 쪽).
    for (const int branch : {kBagBranchA, kBagBranchB}) {
        const BagPlan p = plan_bag_expand(440, 200, 0, 200, kSlots, 700, branch);
        CHECK(p.apply);
        CHECK(p.other == (branch == kBagBranchA ? 200 : 0));
        CHECK(p.sum == p.other + p.expand);
        CHECK(p.capacity == p.base + p.sum);
        CHECK(p.capacity == 700);
    }
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
    // 칸을 골라도 거르개는 그대로다 - 모르는 모양은 어느 칸으로도 안 쓴다.
    CHECK(!plan_bag_expand(500, 200, 150, 100, kSlots, 700, kBagBranchA).apply);
    CHECK(!plan_bag_expand(500, 200, 150, 100, kSlots, 700, kBagBranchB).apply);
}

TEST(bag_plan_does_nothing_when_already_there) {
    // 이미 목표면 쓰지 않는다 - 같은 값을 다시 쓰며 로그를 더럽히지 않는다.
    // **자동 다시 적용이 이 길을 탄다**: 리로드 없이 한 바퀴 더 돌아도 조용하다.
    for (const int branch : {kBagBranchA, kBagBranchB}) {
        const BagPlan p =
            plan_bag_expand(240, 190, 190, 0, kSlots, 700, branch);
        const int a2 = branch == kBagBranchA ? p.expand : p.other;
        const int b2 = branch == kBagBranchA ? p.other : p.expand;
        const BagPlan again =
            plan_bag_expand(p.capacity, p.sum, a2, b2, kSlots, 700, branch);
        CHECK(!again.apply);
        CHECK(again.skip[0] != '\0');
    }
}

TEST(bag_plan_refuses_to_shrink) {
    // 목표가 기본 슬롯보다 작으면 건드리지 않는다. 줄이는 것은 복원 경로가 한다
    // (원본을 기억해 두고 그대로 되돌린다 - 옛 restore 는 확장을 0 으로 써서
    // 가방의 190 을 날렸고 그건 복원이 아니었다).
    CHECK(!plan_bag_expand(240, 190, 190, 0, kSlots, 10).apply);
    // 이미 목표보다 큰 경우도 건드리지 않는다. **두 칸을 다 확인한다** - 큰 값이
    // 우리가 덮어쓸 칸에 있으면 옛 검사(`expand < 0`)는 걸리지 않았고, 그대로
    // 900 짜리 가방을 700 으로 깎았다(2026-09-13, 이 줄이 잡았다).
    CHECK(!plan_bag_expand(900, 850, 850, 0, kSlots, 700, kBagBranchA).apply);
    CHECK(!plan_bag_expand(900, 850, 850, 0, kSlots, 700, kBagBranchB).apply);
    CHECK(!plan_bag_expand(900, 850, 0, 850, kSlots, 700, kBagBranchA).apply);
    CHECK(!plan_bag_expand(900, 850, 0, 850, kSlots, 700, kBagBranchB).apply);
}

TEST(bag_plan_marks_the_shapes_it_recognized) {
    // **자동 재적용이 "끝났다" 와 "아직 덜 만들어졌다" 를 가르는 칸이다.**
    // 여기가 뒤집히면 리로드 직후 한쪽 realm 에만 걸린 채로 끝난다(실측
    // 2026-09-13: 재탐색 0.001초 뒤에 들어가 서버 realm 4개가 전부 밀렸다).

    // 모양 검사를 통과 못 한 것 - 다시 해 봐야 한다.
    CHECK(!plan_bag_expand(-1, 0, 0, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(100, 200, 0, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(500, 200, 150, 100, kSlots, 700).understood);
    CHECK(!plan_bag_expand(190, 190, 190, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(240, 190, 190, 0, 0, 700).understood);

    // 알아보고 나서 안 건드리기로 한 것 - 다시 해도 같다.
    CHECK(plan_bag_expand(240, 190, 190, 0, kSlots, 10).understood);
    CHECK(plan_bag_expand(900, 850, 850, 0, kSlots, 700).understood);
    // 쓸 것이 있는 것도 당연히 알아본 것이다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 700);
    CHECK(p.apply);
    CHECK(p.understood);
    CHECK(!p.same);
    // "이미 그 값이다" 는 알아본 것이고 same 이다.
    const BagPlan again =
        plan_bag_expand(p.capacity, p.sum, p.expand, p.other, kSlots, 700);
    CHECK(!again.apply);
    CHECK(again.understood);
    CHECK(again.same);
    // same 은 "이미 그 값" 일 때만이다 - 목표보다 큰 것은 same 이 아니다.
    CHECK(!plan_bag_expand(900, 850, 850, 0, kSlots, 700).same);
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

TEST(bag_kind_cap_and_filter_come_from_the_same_table) {
    // **거르개와 상한이 어긋나면 "건드리는데 상한이 없는 종류" 가 생긴다.**
    // 한 표에서 나오게 한 것이 그 때문이고, 이 시험이 그 계약이다.
    const auto rules = bag_kind_rules();
    CHECK(!rules.empty());
    for (const auto& r : rules) {
        // 건드리는 종류에는 반드시 쓸 만한 상한이 있어야 한다.
        CHECK(r.cap > 0);
        CHECK(r.cap <= cdtb::game::kBagTargetMax);
        CHECK(cdtb::game::kBagTargetMax <= cdtb::game::kBagEngineMax);
        CHECK(r.name != nullptr && r.name[0] != '\0');
        CHECK(bag_kind_cap(r.kind) == r.cap);
        // 칸도 표에서 나온다. 둘이 갈라지면 "건드리는데 칸을 모르는 종류" 가 생긴다.
        CHECK(r.branch == kBagBranchA || r.branch == kBagBranchB);
        CHECK(bag_kind_branch(r.kind) == r.branch);
        CHECK(bag_resolve_branch(kBagBranchAuto, r.kind) == r.branch);
        // storage_only 가 거짓이면 언제나, 참이면 켰을 때만.
        CHECK(bag_kind_selected(r.kind, true));
        CHECK(bag_kind_selected(r.kind, false) == !r.storage_only);
    }
    // 표에 없는 종류는 상한이 0 이고 어느 쪽이든 안 건드린다.
    for (const std::uint16_t k : {0, 2, 3, 4, 5, 6, 8, 10, 12, 13, 99}) {
        CHECK(bag_kind_cap(k) == 0);
        CHECK(!bag_kind_selected(k, false));
        CHECK(!bag_kind_selected(k, true));
    }
}

TEST(bag_branch_is_per_kind_and_matches_where_the_expansion_lives) {
    // **2026-09-13 조사가 뒤집은 것이다.** 한 칸을 모든 종류에 강요하면 반드시
    // 절반이 남의 칸을 쓴다 - 가방의 +0x1A(가방에게는 남의 칸)에 넣은 값이
    // 리로드에서 사라진 것이 그것이다.
    //
    // 실측: 가방은 확장 190 을 +0x18 에, 보관함은 200 을 +0x1A 에 단다.
    // 상류 소스(CT v5.0 2605~2618)도 +0x1A 를 보관함 세이브의
    // _varyExpandSlotCount 로 적으면서 "가방과는 다른 것" 이라고 못박는다.
    CHECK(bag_kind_branch(1) == kBagBranchA);    // 가방
    CHECK(bag_kind_branch(7) == kBagBranchB);    // 보관함
    CHECK(bag_kind_branch(9) == kBagBranchB);
    CHECK(bag_kind_branch(11) == kBagBranchB);
    // 표에 없는 종류는 안 건드리므로 값은 무해한 기본이면 된다.
    CHECK(bag_kind_branch(99) == kBagBranchA);

    // 자동은 종류별 칸으로 풀린다.
    CHECK(bag_resolve_branch(kBagBranchAuto, 1) == kBagBranchA);
    CHECK(bag_resolve_branch(kBagBranchAuto, 7) == kBagBranchB);
    // 강제는 종류를 무시한다(시험용 경로).
    CHECK(bag_resolve_branch(kBagBranchA, 7) == kBagBranchA);
    CHECK(bag_resolve_branch(kBagBranchB, 1) == kBagBranchB);
    // 모르는 값은 강제가 아니라 자동으로 떨어진다 - 화면이 이상한 값을 줘도
    // 남의 칸을 쓰지 않는다.
    for (const int weird : {-1, 3, 99}) {
        CHECK(bag_resolve_branch(weird, 1) == kBagBranchA);
        CHECK(bag_resolve_branch(weird, 7) == kBagBranchB);
    }
}

TEST(bag_branch_default_leaves_each_kinds_own_expansion_growing) {
    // 자동으로 풀린 칸으로 계획을 세우면, 그 종류가 **원래 쓰던 칸**이 자란다.
    // 가방 240/190/190/0 목표 300 -> +0x18 이 190 에서 250 으로.
    const BagPlan bag = plan_bag_expand(240, 190, 190, 0, kSlots, 300,
                                        bag_resolve_branch(kBagBranchAuto, 1));
    CHECK(bag.apply);
    CHECK(bag.branch == kBagBranchA);
    CHECK(bag.expand == 250);
    CHECK(bag.other == 0);        // +0x1A 는 그대로

    // 보관함 440/200/0/200 목표 500 -> +0x1A 가 200 에서 260 으로.
    const BagPlan st = plan_bag_expand(440, 200, 0, 200, kSlots, 500,
                                       bag_resolve_branch(kBagBranchAuto, 7));
    CHECK(st.apply);
    CHECK(st.branch == kBagBranchB);
    CHECK(st.expand == 260);
    CHECK(st.other == 0);         // +0x18 은 그대로
    CHECK(st.capacity == 500);
}

TEST(bag_plan_obeys_the_per_kind_limit) {
    // 종류별 상한이 목표보다 낮으면 그쪽으로 잘린다. 가방(기본 50)에 상한 300 을
    // 걸고 목표 700 을 넣으면 300 이 나와야 한다.
    const BagPlan p = plan_bag_expand(240, 190, 190, 0, kSlots, 700,
                                      kBagBranchA, 300);
    CHECK(p.apply);
    CHECK(p.capacity == 300);

    // 목표가 상한보다 낮으면 목표가 이긴다.
    const BagPlan q = plan_bag_expand(240, 190, 190, 0, kSlots, 260,
                                      kBagBranchA, 700);
    CHECK(q.apply);
    CHECK(q.capacity == 260);

    // 상한이 전체 상한보다 크면 전체 상한이 이긴다 - 표를 잘못 고쳐도
    // 700 위로는 절대 안 간다.
    const BagPlan big = plan_bag_expand(240, 190, 190, 0, kSlots, 9999,
                                        kBagBranchA, 9999);
    CHECK(big.apply);
    CHECK(big.capacity == kBagTargetMax);

    // 0·음수는 "상한 없음" 이 아니라 전체 상한이다. 상한 없는 길을 만들지 않는다.
    for (const int bad : {0, -1, -9999}) {
        const BagPlan z = plan_bag_expand(240, 190, 190, 0, kSlots, 9999,
                                          kBagBranchA, bad);
        CHECK(z.apply);
        CHECK(z.capacity == kBagTargetMax);
    }
}

TEST(bag_plan_never_produces_a_capacity_above_the_limit) {
    // 속성 시험. 작은 범위를 전수로 돌며 "쓸 값이 나왔다면 반드시 성립해야 하는 것"
    // 을 확인한다. 핵심은 마지막의 a + b == sum 이다 - 모델 검사에 구멍이
    // 생기면 그 줄에서 걸린다(나머지는 정의상 성립하는 항등식이다).
    // **두 칸을 다 돈다** - 칸을 고를 수 있게 된 뒤로는 한쪽만 돌면 나머지 절반의
    // 산술이 시험 밖에 남는다.
    for (const int branch : {kBagBranchA, kBagBranchB}) {
        for (int a = 0; a <= 300; a += 50) {
            for (int b = 0; b <= 300; b += 50) {
                for (int extra = -50; extra <= 50; extra += 25) {
                    const int sum = a + b + extra;
                    for (int base = 1; base <= 300; base += 100) {
                        const int cap = base + sum;
                        for (const int target : {50, 240, 300, 700, 9999}) {
                          // 상한도 같이 돈다 - 0(표에 없음)·낮은 값·전체 상한.
                          for (const int limit : {0, 300, 700, 9999}) {
                            const auto p = plan_bag_expand(cap, sum, a, b, 1460,
                                                           target, branch,
                                                           limit);
                            if (!p.apply) continue;
                            CHECK(p.capacity <= cdtb::game::kBagEngineMax);
                            CHECK(p.capacity <= cdtb::game::kBagTargetMax);
                            CHECK(p.capacity == p.base + p.sum);
                            CHECK(p.sum == p.other + p.expand);
                            CHECK(p.expand >= 0);
                            CHECK(p.base > 0);
                            // **절대 줄이지 않는다.** 확장 기능이 용량을 깎으면
                            // 밖으로 밀려난 아이템이 어떻게 되는지 모른다.
                            CHECK(p.capacity >= cap);
                            // 고르지 않은 칸은 **현재 값 그대로** 실린다. 이 줄이
                            // 없으면 "반대 칸을 건드리지 않는다" 는 약속이 시험
                            // 밖에 남는다.
                            CHECK(p.other == (branch == kBagBranchA ? b : a));
                            CHECK(p.branch == branch);
                            // **이 한 줄이 지적 2 의 회귀를 잡는다** - 쓸 값이
                            // 나왔다면 입력 모델이 성립했어야 한다. 나머지는
                            // plan 안에서 정의상 성립하는 항등식이라 못 잡는다
                            // (리뷰 재검토 B-8).
                            CHECK(a + b == sum);
                            // 종류별 상한을 어떤 값으로 줘도 전체 상한을 넘지
                            // 않는다(표를 잘못 고쳐도 세이브는 안 깨진다).
                            const int eff =
                                (limit <= 0 || limit > cdtb::game::kBagTargetMax)
                                    ? cdtb::game::kBagTargetMax
                                    : limit;
                            CHECK(p.capacity <= eff);
                          }
                        }
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
