// 버프 파라미터 **배율 표**. 이 표가 틀리면 효과 문구의 숫자가 틀리고,
// 숫자가 틀린 툴팁은 없는 툴팁보다 나쁘다. 표는 생성물이므로 여기서 지키는 것은
// "생성기를 다시 돌렸을 때 실측값이 그대로인가" 다 — 값이 바뀌면 여기서 깨진다.
#include <cstdint>

#include "game/buff_param_table.h"
#include "harness.h"

using cdtb::game::buff_param_divisor;
using cdtb::game::buff_param_offset;
using cdtb::game::buff_param_rule;
using cdtb::game::buff_param_rule_count;
using cdtb::game::BuffParamRule;
using cdtb::game::kBuffParamRules;

TEST(buff_param_table_has_the_three_measured_rules) {
    // 생성물이 실측값과 같은지. 생성기를 다시 돌려 표가 바뀌면 여기서 깨진다.
    CHECK_EQ(buff_param_divisor(0x1458FBD20ull), 1000.0);
    CHECK_EQ(buff_param_divisor(0x1458FA908ull), 10000.0);
    CHECK_EQ(buff_param_divisor(0x1458FB5A0ull), 10000000.0);
    CHECK_EQ(buff_param_offset(0x1458FBD20ull), 0x98);
}

TEST(buff_param_table_reports_unknown_classes) {
    CHECK_EQ(buff_param_divisor(0xDEADBEEFull), 0.0);   // 0 = 모르는 클래스
    CHECK_EQ(buff_param_offset(0xDEADBEEFull), 0);      // 진짜 오프셋은 0 일 수 없다
    CHECK(buff_param_rule(0xDEADBEEFull) == nullptr);
}

TEST(buff_param_table_leaves_classes_without_a_parameter_out) {
    // 슬롯 11 이 빈 문자열만 돌려주는 클래스들. 추측해 넣으면 안 된다.
    CHECK_EQ(buff_param_divisor(0x1458FB818ull), 0.0);  // VoidPassiveBuffData
    CHECK_EQ(buff_param_divisor(0x1458FB880ull), 0.0);  // VoidActiveBuffData
    CHECK_EQ(buff_param_divisor(0x1458FC2A0ull), 0.0);  // ImmuneBuffData
    // 파라미터가 숫자가 아니라 표 조회(이름)뿐인 것도 마찬가지다.
    CHECK_EQ(buff_param_divisor(0x1458FADE8ull), 0.0);  // IgnoreUseResourceStat
}

TEST(buff_param_table_keeps_the_level_classes_at_their_raw_value) {
    // 명세 §4.7-H' 2번 표: 레벨형은 나눗셈이 없다. 1 은 "원값", 0 은 "모름"이라
    // 둘을 섞으면 부르는 쪽이 모르는 클래스를 원값으로 그린다.
    CHECK_EQ(buff_param_offset(0x1458FD040ull), 0x94);   // ChangeBuffLevel
    CHECK_EQ(buff_param_divisor(0x1458FD040ull), 1.0);
    CHECK_EQ(buff_param_offset(0x1458FC1C0ull), 0xA0);   // VaryStaticStatLevel (thunk)
    CHECK_EQ(buff_param_divisor(0x1458FC1C0ull), 1.0);
    // Damage 는 VaryStatMaxValue 와 같은 칸·같은 배율이다(명세 같은 표).
    CHECK_EQ(buff_param_offset(0x1458FC370ull), 0x98);
    CHECK_EQ(buff_param_divisor(0x1458FC370ull), 1000.0);
}

TEST(buff_param_table_rows_are_sorted_and_unique) {
    // 오름차순이 아니면 같은 vtable 이 두 줄 들어가도 안 보인다.
    CHECK(buff_param_rule_count() > 0);
    for (std::size_t i = 1; i < buff_param_rule_count(); ++i) {
        CHECK(kBuffParamRules[i - 1].vtable_va < kBuffParamRules[i].vtable_va);
    }
}

TEST(buff_param_table_rows_are_well_formed) {
    for (const BuffParamRule& r : kBuffParamRules) {
        // BuffData 의 값 칸은 전부 +0x90 위다. 0 이나 작은 값은 뽑기 실패의 흔적이다.
        CHECK(r.offset >= 0x90 && r.offset <= 0x200);
        CHECK(r.divisor > 0.0);
        CHECK(r.note != nullptr && r.note[0] != '\0');
        // vtable 은 모듈 고정 VA 다(기준 0x140000000).
        CHECK(r.vtable_va > 0x140000000ull && r.vtable_va < 0x150000000ull);
    }
}

TEST(buff_param_table_is_usable_at_compile_time) {
    // 스냅샷 조립은 배경 스레드에서 돈다 — 조회가 실행시간 초기화에 기대면 안 된다.
    static_assert(buff_param_divisor(0x1458FBD20ull) == 1000.0);
    static_assert(buff_param_offset(0x1458FBD20ull) == 0x98);
    static_assert(buff_param_divisor(0x1ull) == 0.0);
    CHECK(true);
}
