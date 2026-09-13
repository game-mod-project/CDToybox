#include <cstring>

#include "game/inventory.h"
#include "harness.h"

using cdtb::game::BagPlan;
using cdtb::game::bag_kind_branch;
using cdtb::game::bag_kind_cap;
using cdtb::game::bag_kind_known;
using cdtb::game::bag_kind_rules;
using cdtb::game::bag_resolve_branch;
using cdtb::game::kBagBranchA;
using cdtb::game::kBagBranchAuto;
using cdtb::game::kBagBranchB;
using cdtb::game::kBagEngineMax;
using cdtb::game::kBagTargetMax;
using cdtb::game::plan_bag_expand;

namespace {
// 라이브 실측(2026-09-13). cap/a/b = 컨테이너 +0x14/+0x18/+0x1A.
//   가방(종류 1)   240 / 190 / 0     -> 기본 50
//   보관함(종류 7) 440 / 0   / 200   -> 기본 240
// **+0x16(합계)은 인자에 없다** - 읽지 않는다. 그 칸은 게임이 되돌리지 않아 우리
// 값이 세이브에 남고(게임 재시작을 넘어 세 번 재현), 그것 때문에 우리 모델이 제
// 컨테이너를 영영 거부했다. 두 갈래에서 유도하면 낡은 합계가 있어도 맞는다.
constexpr int kSlots = 1460;   // +0x08, 18개 컨테이너 전부 같았다
}  // namespace

TEST(bag_plan_derives_base_from_both_branches) {
    // **2026-09-05 사고의 핵심.** 옛 코드는 기본 = cap - b 였는데, 가방은 확장을
    // a(+0x18)에 담아 b = 0 이라 240 이 나왔다(정답 50). 한 갈래만 보면 틀린다.
    const BagPlan bag = plan_bag_expand(240, 190, 0, kSlots, 700);
    CHECK(bag.apply);
    CHECK(bag.base == 50);
    const BagPlan store = plan_bag_expand(440, 0, 200, kSlots, 700);
    CHECK(store.apply);
    CHECK(store.base == 240);
}

TEST(bag_plan_ignores_a_stale_sum_field) {
    // **2026-09-13 재설계의 이유.** +0x16 이 낡아 있어도(가방에서 464·650 이
    // 세이브를 넘어 살아 왔다) 산술에 안 들어온다. 그 칸이 인자에 없다는 사실
    // 자체가 계약이다 - 낡은 합계와 무관하게 같은 답이 나온다.
    const BagPlan p = plan_bag_expand(240, 190, 0, kSlots, 300);
    CHECK(p.apply);
    CHECK(p.base == 50);
    CHECK(p.capacity == 300);
    CHECK(p.expand == 250);   // 가방은 A 칸(기본)
}

TEST(bag_plan_capacity_is_always_exactly_the_clamped_target) {
    // **상한 보장의 뿌리.** expand = want - base - other 이므로
    // capacity = base + other + expand = want 다. 기본 슬롯 유도가 틀려도 용량은
    // 언제나 정확히 want 이고, want 는 700/732/물리 배열로 잘린다. 그래서
    // "우리가 모르는 제3의 갈래" 가 있어도 용량이 상한을 넘지 못한다 - 옛 모델은
    // 그것을 `합계 = A+B` 검사로 막았는데, 이제는 산술이 구조적으로 막는다.
    for (const int target : {50, 240, 300, 700, 9999}) {
        for (const int slots : {300, 1460}) {
            const BagPlan p = plan_bag_expand(240, 190, 0, slots, target);
            if (!p.apply) continue;
            int want = target;
            if (want > kBagTargetMax) want = kBagTargetMax;
            if (want > slots) want = slots;
            CHECK(p.capacity == want);
            CHECK(p.capacity <= kBagTargetMax);
            CHECK(p.capacity <= kBagEngineMax);
            CHECK(p.capacity <= slots);
        }
    }
}

TEST(bag_plan_writes_the_branch_you_choose) {
    // 어느 쪽을 고르든 반대 칸은 그대로 두고, 결과 용량은 같아야 한다.
    const BagPlan a = plan_bag_expand(240, 190, 0, kSlots, 700, kBagBranchA);
    CHECK(a.apply);
    CHECK(a.branch == kBagBranchA);
    CHECK(a.other == 0);            // +0x1A 는 그대로
    CHECK(a.expand == 650);         // +0x18 이 190 -> 650
    CHECK(a.capacity == 700);

    const BagPlan b = plan_bag_expand(240, 190, 0, kSlots, 700, kBagBranchB);
    CHECK(b.apply);
    CHECK(b.branch == kBagBranchB);
    CHECK(b.other == 190);          // +0x18 은 그대로
    CHECK(b.expand == 460);         // +0x1A 가 0 -> 460
    CHECK(b.capacity == 700);
}

TEST(bag_branch_is_per_kind_and_matches_where_the_expansion_lives) {
    // **2026-09-13 조사가 뒤집은 것.** 한 칸을 모든 종류에 강요하면 반드시 절반이
    // 남의 칸을 쓴다. 실측: 가방은 190 을 +0x18 에, 보관함은 200 을 +0x1A 에.
    // 상류 소스(CT v5.0 2605~2618)도 +0x1A 를 보관함 세이브의
    // _varyExpandSlotCount 로 적으면서 "가방과는 다른 것" 이라고 못박는다.
    CHECK(bag_kind_branch(1) == kBagBranchA);    // 가방
    CHECK(bag_kind_branch(7) == kBagBranchB);    // 보관함
    CHECK(bag_kind_branch(9) == kBagBranchB);
    CHECK(bag_kind_branch(11) == kBagBranchB);
    CHECK(bag_kind_branch(99) == kBagBranchA);   // 표에 없으면 무해한 기본

    CHECK(bag_resolve_branch(kBagBranchAuto, 1) == kBagBranchA);
    CHECK(bag_resolve_branch(kBagBranchAuto, 7) == kBagBranchB);
    CHECK(bag_resolve_branch(kBagBranchA, 7) == kBagBranchA);   // 강제
    CHECK(bag_resolve_branch(kBagBranchB, 1) == kBagBranchB);
    // 모르는 값은 강제가 아니라 자동으로 떨어진다 - 화면이 이상한 값을 줘도
    // 남의 칸을 쓰지 않는다.
    for (const int weird : {-1, 3, 99}) {
        CHECK(bag_resolve_branch(weird, 1) == kBagBranchA);
        CHECK(bag_resolve_branch(weird, 7) == kBagBranchB);
    }
}

TEST(bag_branch_default_grows_each_kinds_own_expansion) {
    const BagPlan bag = plan_bag_expand(240, 190, 0, kSlots, 300,
                                        bag_resolve_branch(kBagBranchAuto, 1));
    CHECK(bag.apply);
    CHECK(bag.branch == kBagBranchA);
    CHECK(bag.expand == 250);
    CHECK(bag.other == 0);

    const BagPlan st = plan_bag_expand(440, 0, 200, kSlots, 500,
                                       bag_resolve_branch(kBagBranchAuto, 7));
    CHECK(st.apply);
    CHECK(st.branch == kBagBranchB);
    CHECK(st.expand == 260);
    CHECK(st.other == 0);
    CHECK(st.capacity == 500);
}

TEST(bag_plan_never_exceeds_the_physical_array) {
    // 물리 배열(+0x08)보다 크게 잡지 않는다. 용량까지 도는 순회가 배열 밖으로
    // 나가는 것이 2026-09-05 의 "가방 획득 시 팅김" 이었다.
    const BagPlan p = plan_bag_expand(240, 190, 0, 300, 700);
    CHECK(p.apply);
    CHECK(p.capacity == 300);
}

TEST(bag_plan_skips_shapes_it_does_not_understand) {
    // 용량이 확장보다 작다 - 우리 모델이 아니다. (예전에는 이 검사가 +0x16 을
    // 봤고, 그 낡은 값 때문에 멀쩡한 컨테이너를 영영 거부했다.)
    CHECK(!plan_bag_expand(100, 150, 0, kSlots, 700).apply);
    // 기본 슬롯이 0 이하로 나온다.
    CHECK(!plan_bag_expand(190, 190, 0, kSlots, 700).apply);
    // 음수·0 칸.
    CHECK(!plan_bag_expand(-1, 0, 0, kSlots, 700).apply);
    CHECK(!plan_bag_expand(240, 190, 0, 0, 700).apply);
    // 건너뛸 때는 이유를 남긴다(로그·화면에 쓴다).
    CHECK(plan_bag_expand(100, 150, 0, kSlots, 700).skip[0] != '\0');
}

TEST(bag_plan_does_nothing_when_already_there) {
    // 이미 목표면 쓰지 않는다. **자동 재적용이 이 길을 탄다** - 리로드 없이 한
    // 바퀴 더 돌아도 조용하다.
    for (const int branch : {kBagBranchA, kBagBranchB}) {
        const BagPlan p = plan_bag_expand(240, 190, 0, kSlots, 700, branch);
        const int a2 = branch == kBagBranchA ? p.expand : p.other;
        const int b2 = branch == kBagBranchA ? p.other : p.expand;
        const BagPlan again =
            plan_bag_expand(p.capacity, a2, b2, kSlots, 700, branch);
        CHECK(!again.apply);
        CHECK(again.same);
        CHECK(again.skip[0] != '\0');
    }
}

TEST(bag_plan_refuses_to_shrink) {
    // 목표가 기본 슬롯보다 작으면 건드리지 않는다. 줄이는 것은 복원 경로가 한다.
    CHECK(!plan_bag_expand(240, 190, 0, kSlots, 10).apply);
    // 이미 목표보다 큰 경우도 건드리지 않는다. **두 칸을 다 확인한다** - 큰 값이
    // 우리가 덮어쓸 칸에 있으면 옛 검사는 안 걸려 900 짜리를 700 으로 깎았다.
    CHECK(!plan_bag_expand(900, 850, 0, kSlots, 700, kBagBranchA).apply);
    CHECK(!plan_bag_expand(900, 850, 0, kSlots, 700, kBagBranchB).apply);
    CHECK(!plan_bag_expand(900, 0, 850, kSlots, 700, kBagBranchA).apply);
    CHECK(!plan_bag_expand(900, 0, 850, kSlots, 700, kBagBranchB).apply);
}

TEST(bag_plan_target_zero_changes_nothing) {
    // 0 은 "복원" 이 아니라 **바꿀 것 없음**이다. 종류별 목표에서 0 은 "이 종류는
    // 안 건드린다" 는 뜻이라, 여기서 새는 길이 있으면 안 고른 종류가 바뀐다.
    CHECK(!plan_bag_expand(240, 190, 0, kSlots, 0).apply);
    CHECK(!plan_bag_expand(440, 0, 200, kSlots, 0).apply);
}

TEST(bag_plan_marks_the_shapes_it_recognized) {
    // **자동 재적용이 "끝났다" 와 "아직 덜 만들어졌다" 를 가르는 칸이다.**
    // 여기가 뒤집히면 리로드 직후 한쪽 realm 에만 걸린 채로 끝난다(실측).
    CHECK(!plan_bag_expand(-1, 0, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(100, 150, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(190, 190, 0, kSlots, 700).understood);
    CHECK(!plan_bag_expand(240, 190, 0, 0, 700).understood);

    // 알아보고 나서 안 건드리기로 한 것 - 다시 해도 같다.
    CHECK(plan_bag_expand(240, 190, 0, kSlots, 10).understood);
    CHECK(plan_bag_expand(900, 850, 0, kSlots, 700).understood);
    const BagPlan p = plan_bag_expand(240, 190, 0, kSlots, 700);
    CHECK(p.apply);
    CHECK(p.understood);
    CHECK(!p.same);
    // same 은 "이미 그 값" 일 때만이다.
    CHECK(!plan_bag_expand(900, 850, 0, kSlots, 700).same);
}

TEST(bag_kind_cap_and_filter_come_from_the_same_table) {
    // **거르개와 상한이 어긋나면 "건드리는데 상한이 없는 종류" 가 생긴다.**
    const auto rules = bag_kind_rules();
    CHECK(!rules.empty());
    // 목표 배열의 길이와 표의 길이는 반드시 같아야 한다 - 어긋나면 apply_to 가
    // span 밖을 읽는다.
    CHECK(rules.size() == cdtb::game::kBagKindCount);
    for (const auto& r : rules) {
        CHECK(r.cap > 0);
        CHECK(r.cap <= kBagTargetMax);
        CHECK(kBagTargetMax <= kBagEngineMax);
        CHECK(r.name != nullptr && r.name[0] != '\0');
        CHECK(bag_kind_cap(r.kind) == r.cap);
        CHECK(bag_kind_known(r.kind));
        CHECK(r.branch == kBagBranchA || r.branch == kBagBranchB);
        CHECK(bag_kind_branch(r.kind) == r.branch);
        CHECK(bag_resolve_branch(kBagBranchAuto, r.kind) == r.branch);
    }
    // **이름은 서로 달라야 한다.** 화면이 그것을 SliderInt 라벨로 쓰고, 라벨이
    // 곧 ImGui ID 다. 같으면 두 슬라이더가 한 값을 공유하는데 ImGui 는 경고도
    // 안 낸다 - 이 창에서 ID 충돌이 실제로 났던 적이 있다.
    for (std::size_t i = 0; i < rules.size(); ++i) {
        for (std::size_t j = i + 1; j < rules.size(); ++j) {
            CHECK(std::strcmp(rules[i].name, rules[j].name) != 0);
        }
    }
    // 표에 없는 종류는 상한이 0 이고 어느 쪽이든 안 건드린다. 작은 칸(용량
    // 5·10·20·50)까지 부풀린 것이 2026-09-05 "리로드 후 지급 손상" 의 유력한
    // 원인이다. 종류 4 는 혼자 +0x20 에 8칸짜리 보조 배열을 달아 뺐다.
    for (const std::uint16_t k :
         {0, 2, 3, 4, 5, 6, 8, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 99}) {
        CHECK(bag_kind_cap(k) == 0);
        CHECK(!bag_kind_known(k));
    }
}

TEST(bag_plan_obeys_the_per_kind_limit) {
    // 종류별 상한이 목표보다 낮으면 그쪽으로 잘린다.
    const BagPlan p = plan_bag_expand(240, 190, 0, kSlots, 700, kBagBranchA, 300);
    CHECK(p.apply);
    CHECK(p.capacity == 300);

    // 목표가 상한보다 낮으면 목표가 이긴다.
    const BagPlan q = plan_bag_expand(240, 190, 0, kSlots, 260, kBagBranchA, 700);
    CHECK(q.apply);
    CHECK(q.capacity == 260);

    // 상한이 전체 상한보다 크면 전체 상한이 이긴다 - 표를 잘못 고쳐도 700 위로는
    // 절대 안 간다.
    const BagPlan big =
        plan_bag_expand(240, 190, 0, kSlots, 9999, kBagBranchA, 9999);
    CHECK(big.apply);
    CHECK(big.capacity == kBagTargetMax);

    // 0·음수는 "상한 없음" 이 아니라 전체 상한이다.
    for (const int bad : {0, -1, -9999}) {
        const BagPlan z =
            plan_bag_expand(240, 190, 0, kSlots, 9999, kBagBranchA, bad);
        CHECK(z.apply);
        CHECK(z.capacity == kBagTargetMax);
    }
}

TEST(bag_repair_fixes_only_the_sum_we_broke) {
    // **실측 2026-09-13.** 리로드에서 게임은 +0x14 와 두 갈래는 되돌리면서
    // +0x16 은 우리가 쓴 값을 그대로 뒀다(게임 재시작을 넘어 세 번 재현).
    // 새 산술은 그 칸을 안 읽으므로 손상이 기능을 막지는 않지만, 우리가 세이브에
    // 남긴 쓰레기라 치울 수단은 있어야 한다.
    const auto p = cdtb::game::plan_bag_repair(240, 408, 190, 0, nullptr);
    CHECK(p.apply);
    CHECK(p.sum == 190);
    CHECK(p.base == 50);

    // 멀쩡한 것은 건드리지 않는다.
    CHECK(!cdtb::game::plan_bag_repair(240, 190, 190, 0, nullptr).apply);
    CHECK(!cdtb::game::plan_bag_repair(440, 200, 0, 200, nullptr).apply);

    // 갈래 합이 용량을 넘으면 우리가 만든 모양이 아니다 - 안 건드린다.
    CHECK(!cdtb::game::plan_bag_repair(240, 999, 300, 100, nullptr).apply);
    CHECK(!cdtb::game::plan_bag_repair(-1, 0, 0, 0, nullptr).apply);
    CHECK(cdtb::game::plan_bag_repair(240, 190, 190, 0, nullptr).skip[0] != '\0');

    // 고친 결과는 반드시 우리 모델이 받아들이는 모양이어야 한다.
    const auto after = plan_bag_expand(240, 190, 0, kSlots, 700);
    CHECK(after.apply);
    CHECK(after.base == 50);
}

TEST(bag_repair_prefers_the_recorded_original) {
    cdtb::game::BagBackup rec;
    rec.realm = 0;
    rec.kind = 1;
    rec.cap = 240;
    rec.a = 190;
    rec.b = 0;
    rec.want_cap = 300;
    rec.want_exp = 250;

    const auto p = cdtb::game::plan_bag_repair(240, 250, 190, 60, &rec);
    CHECK(p.apply);
    CHECK(p.from_record);
    CHECK(p.a == 190);
    CHECK(p.b == 0);
    CHECK(p.base == 50);

    // 용량이 기록의 원본과 다르면 그 컨테이너가 우리 것이라고 볼 수 없다.
    const auto other = cdtb::game::plan_bag_repair(500, 400, 190, 0, &rec);
    CHECK(!other.from_record);
}

TEST(bag_repair_refuses_the_half_written_shape) {
    // `a+b > sum` 은 컨테이너가 아직 채워지는 중이거나 우리 쓰기가 반쯤 지나간
    // 모양이다. 그걸 "고치면" 기본 슬롯 유도가 틀어져 정당한 확장을 덮는다.
    CHECK(!cdtb::game::plan_bag_repair(240, 0, 190, 0, nullptr).apply);
    CHECK(!cdtb::game::plan_bag_repair(240, 100, 190, 0, nullptr).apply);
    // 관측된 손상의 방향(합계가 더 크다)만 연다.
    CHECK(cdtb::game::plan_bag_repair(240, 408, 190, 0, nullptr).apply);
}

TEST(bag_plan_holds_its_invariants_across_the_whole_range) {
    // 속성 시험. **핵심은 capacity == want 다** - 그 항등식이 상한 보장의 전부다.
    for (const int branch : {kBagBranchA, kBagBranchB}) {
        for (int a = 0; a <= 300; a += 50) {
            for (int b = 0; b <= 300; b += 50) {
                for (int base = 1; base <= 300; base += 100) {
                    const int cap = base + a + b;
                    for (const int target : {50, 240, 300, 700, 9999}) {
                        for (const int limit : {0, 300, 700, 9999}) {
                            const auto p = plan_bag_expand(cap, a, b, 1460,
                                                           target, branch,
                                                           limit);
                            if (!p.apply) continue;
                            const int eff =
                                (limit <= 0 || limit > kBagTargetMax)
                                    ? kBagTargetMax
                                    : limit;
                            int want = target;
                            if (want > eff) want = eff;
                            // **용량은 언제나 정확히 잘린 목표다.**
                            CHECK(p.capacity == want);
                            CHECK(p.capacity <= kBagTargetMax);
                            CHECK(p.capacity <= kBagEngineMax);
                            CHECK(p.capacity >= cap);   // 절대 줄이지 않는다
                            CHECK(p.base == base);      // 두 갈래로 유도한 기본
                            CHECK(p.expand >= 0);
                            CHECK(p.other == (branch == kBagBranchA ? b : a));
                            CHECK(p.branch == branch);
                            CHECK(p.capacity == p.base + p.other + p.expand);
                        }
                    }
                }
            }
        }
    }
}
