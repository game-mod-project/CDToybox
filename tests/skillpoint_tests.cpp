// 스킬 포인트(어비스 결속)의 순수 계산. 실측으로 확정된 구조를 시험이 지킨다.
//
// 2026-09-13 실측(사용자가 결속 하나를 쓰는 순간 전후 비교):
//   지식 컴포넌트 +0xC8 -> [보유 1, 총합 151, ?, 70, 151, 1, 150, 151]
//   쓴 뒤          -> [보유 0, 총합 151, ?, 70, 151, 1, 149, 151]
//   화면: 우상단 ×1 -> ×0, 좌하단 "사용 ×150" -> "×151"
// **좌하단은 저장된 값이 아니다** - 화면이 `총합 - 보유` 로 그린다(151-1=150,
// 151-0=151 로 둘 다 맞는다). 그래서 보유만 올리면 "사용" 이 줄어 보인다.
#include "game/skillpoint.h"
#include "harness.h"

using cdtb::game::BondPlan;
using cdtb::game::kBondAddMax;
using cdtb::game::kBondCeiling;
using cdtb::game::BondBackup;
using cdtb::game::bond_restore_blocked;
using cdtb::game::plan_bond_add;

TEST(bond_add_raises_have_and_total_together) {
    // **핵심 계약.** 총합을 같이 올려야 화면의 "사용"(= 총합 - 보유)이 그대로다.
    const BondPlan p = plan_bond_add(0, 151, 10, true);
    CHECK(p.apply);
    CHECK(p.have == 10);
    CHECK(p.total == 161);
    // 사용 = 총합 - 보유 = 161 - 10 = 151 - 바뀌지 않았다.
    CHECK(p.total - p.have == 151);
}

TEST(bond_add_without_total_makes_the_screen_lie) {
    // 총합을 안 올리면 보유가 총합을 넘는 모양이 나오므로 **거부한다.**
    // 화면이 `총합 - 보유` 를 그리는데 그게 음수가 되는 상태다.
    // (200 을 넣으면 한 번 상한 100 으로 잘려 보유 100 <= 총합 151 이라 통과한다 -
    //  자르기가 먼저라는 것도 계약이므로 걸리는 입력을 따로 골라 둔다.)
    CHECK(plan_bond_add(0, 151, 200, false).apply);
    CHECK(!plan_bond_add(100, 151, 100, false).apply);   // 200 > 151
    CHECK(!plan_bond_add(151, 151, 1, false).apply);     // 152 > 151
    // 넘지 않는 범위면 허용하되, "사용" 이 그만큼 줄어드는 것은 사실이다.
    const BondPlan p = plan_bond_add(0, 151, 10, false);
    CHECK(p.apply);
    CHECK(p.have == 10);
    CHECK(p.total == 151);
    CHECK(p.total - p.have == 141);   // 사용이 151 -> 141 로 줄어 보인다
}

TEST(bond_add_refuses_shapes_it_does_not_understand) {
    // 보유가 총합보다 크면 우리 모델이 아니다 - 화면의 "사용" 이 음수가 된다.
    CHECK(!plan_bond_add(200, 151, 10, true).apply);
    CHECK(!plan_bond_add(-1, 151, 10, true).apply);
    CHECK(!plan_bond_add(0, -1, 10, true).apply);
    // 더할 것이 없다.
    CHECK(!plan_bond_add(0, 151, 0, true).apply);
    CHECK(!plan_bond_add(0, 151, -5, true).apply);
    // 건너뛸 때는 이유를 남긴다(화면·로그에 낸다).
    CHECK(plan_bond_add(200, 151, 10, true).skip[0] != '\0');
}

TEST(bond_add_clamps_the_step_and_the_ceiling) {
    // 한 번에 더하는 양은 상한으로 잘린다.
    const BondPlan big = plan_bond_add(0, 151, 9999, true);
    CHECK(big.apply);
    CHECK(big.have == kBondAddMax);
    CHECK(big.total == 151 + kBondAddMax);

    // **천장.** u16 칸이라 구조적 한계는 65535 지만 근거 없이 그 근처로 가지 않는다
    // (참고 모드에 스킬 포인트 기능이 없어 빌려 올 숫자가 없다).
    CHECK(!plan_bond_add(kBondCeiling, kBondCeiling, 1, true).apply);
    CHECK(!plan_bond_add(0, kBondCeiling, 1, true).apply);   // 총합이 넘는다
    CHECK(kBondCeiling < 65535);
    CHECK(kBondAddMax <= kBondCeiling);
}

TEST(bond_add_never_produces_a_negative_screen_value) {
    // 속성 시험. 쓸 값이 나왔다면 **화면이 그리는 "사용" 이 음수가 아니어야** 한다.
    for (int have = 0; have <= 300; have += 25) {
        for (int total = 0; total <= 300; total += 25) {
            for (const int add : {1, 5, 10, 100, 9999}) {
                for (const bool also : {false, true}) {
                    const BondPlan p = plan_bond_add(have, total, add, also);
                    if (!p.apply) continue;
                    CHECK(p.have > have);            // 반드시 늘어난다
                    CHECK(p.have <= kBondCeiling);
                    CHECK(p.total <= kBondCeiling);
                    CHECK(p.total >= p.have);        // 사용이 음수가 안 된다
                    CHECK(p.have - have <= kBondAddMax);
                    if (also) {
                        // 총합도 올렸으면 "사용" 은 그대로다.
                        CHECK(p.total - p.have == total - have);
                    } else {
                        CHECK(p.total == total);
                    }
                }
            }
        }
    }
}

TEST(bond_restore_allows_exactly_what_we_wrote) {
    // 되돌리기 기록: 처음 본 1/151 에서 11/161 로 만들었다.
    BondBackup b;
    b.realm = 0;
    b.have = 1;
    b.total = 151;
    b.wrote_have = 11;
    b.wrote_total = 161;
    CHECK(bond_restore_blocked(b, 11, 161) == nullptr);
}

TEST(bond_restore_refuses_when_the_game_gave_or_took_bonds) {
    // **검토 치명 2 의 회귀 시험.** 더한 뒤 사용자가 게임에서 결속을 정당하게
    // 얻거나 쓰면, 옛 원본을 그대로 쓰는 것은 그 결속을 지우는 일이 된다.
    // 가방에서 같은 관문이 없어 "돈 주고 산 칸을 지우는" 길이 열렸었다.
    BondBackup b;
    b.realm = 0;
    b.have = 1;
    b.total = 151;
    b.wrote_have = 11;
    b.wrote_total = 161;

    // 게임이 5를 더 줬다(16/166). 되돌리면 그 5가 사라진다.
    CHECK(bond_restore_blocked(b, 16, 166) != nullptr);
    // 사용자가 결속을 썼다(0/161). 되돌리면 안 가진 1을 주고 누적을 내린다.
    CHECK(bond_restore_blocked(b, 0, 161) != nullptr);
    // 보유만 맞고 총합이 다른 경우도 막는다.
    CHECK(bond_restore_blocked(b, 11, 171) != nullptr);
    // 막을 때는 이유를 남긴다(화면·로그에 그대로 낸다).
    CHECK(bond_restore_blocked(b, 16, 166)[0] != '\0');
}

TEST(bond_restore_refuses_records_that_do_not_know_what_they_wrote) {
    // 쓰기가 실패해 무엇을 써 놓았는지 모르는 기록은 되돌리지 않는다 -
    // 그 상태에서 옛 원본을 쓰면 지금 값이 무엇이든 덮어쓰게 된다.
    BondBackup b;
    b.realm = 0;
    b.have = 1;
    b.total = 151;
    CHECK(b.wrote_have == 0 && b.wrote_total == 0);
    CHECK(bond_restore_blocked(b, 11, 161) != nullptr);
    // 값을 못 읽은 경우도 막는다.
    BondBackup w = b;
    w.wrote_have = 11;
    w.wrote_total = 161;
    CHECK(bond_restore_blocked(w, -1, 161) != nullptr);
}

TEST(bond_restore_refuses_when_it_would_be_an_increase) {
    // 복원이 **증가**가 되는 상황은 우리가 만든 것이 아니다.
    BondBackup b;
    b.realm = 0;
    b.have = 1;
    b.total = 151;
    b.wrote_have = 11;
    b.wrote_total = 140;   // 지금 총합이 원본보다 작다
    CHECK(bond_restore_blocked(b, 11, 140) != nullptr);
}
