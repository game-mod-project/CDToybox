#include <string>

#include "game/effect_format.h"
#include "harness.h"

namespace {
using cdtb::game::duration_suffix;
using cdtb::game::effect_line;
using cdtb::game::format_needs_params;
using cdtb::game::effect_number;
using cdtb::game::EffectValues;

// 스탯 이름표(Task 3 의 몫)를 흉내낸 콜백 - 이 시험에서 쓰는 두 키만 안다.
std::string names(std::string_view table, std::string_view key) {
    if (table == "SubLevel" && key == "Hp") return "생명";
    if (table == "Status" && key == "IceResistance") return "냉기 저항";
    return "";
}

// 아무 것도 모르는 이름표 - "이름표가 비었을 때" 를 흉내낸다.
std::string empty_names(std::string_view, std::string_view) {
    return "";
}

}  // namespace

// 아래 여섯은 docs/superpowers/plans/2026-09-23-item-effects.md Task 2 의 시험을
// 글자 그대로 옮긴 것이다(문자열은 명세 §4.7-E 의 실측 문구).

TEST(effect_line_fills_param_and_duration) {
    EffectValues v;
    v.param = 45;
    v.duration_ms = 60000;
    CHECK_EQ(effect_line("{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가", v, names),
             std::string("생명 최대치 45 증가(1분)"));
}

TEST(effect_line_fills_repeat_tick_and_absolute_param) {
    EffectValues v;
    v.param = -15;
    v.repeat_tick = 1;
    v.duration_ms = 20000;
    CHECK_EQ(effect_line("{Staticinfo:SubLevel:Hp} : {RepeatTick} 초마다 {|Param1|} 감소", v,
                          names),
             std::string("생명 : 1 초마다 15 감소(20초)"));
}

TEST(effect_line_leaves_unknown_tokens_alone) {
    // 이름표가 비면 토큰을 지우지 않는다 - 빈 자리가 남으면 문장이 깨진다.
    EffectValues v;
    v.param = 6;
    CHECK_EQ(effect_line("{Staticinfo:Status:Xyz} Lv{Param1}", v, empty_names),
             std::string("{Staticinfo:Status:Xyz} Lv6"));
}

TEST(duration_suffix_floors_to_minutes_then_seconds) {
    CHECK_EQ(duration_suffix(90000), std::string("(1분)"));  // 실측: 90000 은 "1분"
    CHECK_EQ(duration_suffix(60000), std::string("(1분)"));
    CHECK_EQ(duration_suffix(20000), std::string("(20초)"));
    CHECK_EQ(duration_suffix(6000), std::string("(6초)"));
    CHECK_EQ(duration_suffix(0), std::string(""));
}

TEST(effect_number_drops_trailing_zeros) {
    CHECK_EQ(effect_number(75.0), std::string("75"));
    CHECK_EQ(effect_number(2.5), std::string("2.5"));
}

TEST(effect_line_handles_key_and_money_tokens_by_leaving_them) {
    EffectValues v;
    CHECK_EQ(effect_line("대지의 울림 ({Key:Key_Skill_1})", v, names),
             std::string("대지의 울림 ({Key:Key_Skill_1})"));
}

// 여기부터는 위임 지시가 추가로 요구한 시험(계획 문서에는 없다).

TEST(effect_line_replaces_every_staticinfo_token_in_one_line) {
    // {Staticinfo:...} 가 한 줄에 여럿이면 전부 바뀌어야 한다.
    EffectValues v;
    v.param = 3;
    CHECK_EQ(effect_line(
                 "{Staticinfo:SubLevel:Hp} / {Staticinfo:Status:IceResistance} Lv{Param1}", v,
                 names),
             std::string("생명 / 냉기 저항 Lv3"));
}

TEST(effect_line_survives_mismatched_braces) {
    // 짝이 안 맞는 중괄호에서 죽지 않고 그대로 둔다.
    EffectValues v;
    v.param = 1;
    CHECK_EQ(effect_line("{Param1", v, names), std::string("{Param1"));  // 안 닫힘
    CHECK_EQ(effect_line("} 증가", v, names), std::string("} 증가"));    // 안 열림
}

TEST(effect_line_of_empty_format_is_empty) {
    // 빈 형식 문자열이면 빈 결과 - 접미(duration_suffix)도 안 붙는다.
    EffectValues v;
    v.duration_ms = 60000;
    CHECK(effect_line("", v, names).empty());
}

TEST(format_needs_params_finds_every_value_slot) {
    // 걷기 쪽이 "형식은 값을 요구하는데 `_paramList` 가 비었다" 를 가려내는 데
    // 쓴다 - 그런 줄은 토큰을 화면에 내보내지 않고 버린다.
    CHECK(format_needs_params("{Param0} 증가"));
    CHECK(format_needs_params("{Param3} 증가"));
    CHECK(format_needs_params("{|Param1|} 감소"));
    CHECK(format_needs_params("{RepeatTick} 초마다"));
    CHECK(format_needs_params("{Staticinfo:SubLevel:Hp} 최대치 {Param1} 증가"));
}

TEST(format_needs_params_ignores_tokens_that_are_not_values) {
    // 이름·키·화폐 토큰은 값 자리가 아니다. 이것까지 값으로 치면 멀쩡한 줄을
    // 버리게 된다.
    CHECK(!format_needs_params("용기 고정"));
    CHECK(!format_needs_params("{Staticinfo:SubLevel:Hp} 고정"));
    CHECK(!format_needs_params("대지의 울림 ({Key:Key_Skill_1})"));
    CHECK(!format_needs_params("{Money:Silver:3}"));
    CHECK(!format_needs_params(""));
    // `{Param4}` 는 없다(4~7 은 `{|ParamN|}` 꼴로만 온다).
    CHECK(!format_needs_params("{Param4} 증가"));
    // 짝이 안 맞아도 죽지 않는다.
    CHECK(!format_needs_params("{Param1"));
}
