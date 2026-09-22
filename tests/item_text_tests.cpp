#include <string>
#include <string_view>

#include "game/item_text.h"
#include "harness.h"

namespace {
using cdtb::game::clean_item_desc;
using cdtb::game::desc_first_line;
}  // namespace

// 규칙은 2949 설명 6736개 전수에서 나왔다(스펙 §3 표기). 문장은 같은 구조의 합성이다.

TEST(clean_item_desc_turns_br_into_newline) {
    CHECK_EQ(clean_item_desc("앞 문장.<br/>뒤 문장."), std::string("앞 문장.\n뒤 문장."));
}

TEST(clean_item_desc_keeps_the_blank_line_of_a_double_br) {
    // 연속 <br/><br/> 가 196건 - 문단 사이 빈 줄이다.
    CHECK_EQ(clean_item_desc("첫 문단.<br/><br/>둘째 문단."),
             std::string("첫 문단.\n\n둘째 문단."));
}

TEST(clean_item_desc_replaces_a_knowledge_placeholder_with_its_label) {
    CHECK_EQ(clean_item_desc("{Staticinfo:Knowledge:Knowledge_Hp#생명}이 회복된다."),
             std::string("생명이 회복된다."));
}

TEST(clean_item_desc_keeps_spaces_inside_an_item_placeholder_label) {
    CHECK_EQ(clean_item_desc("재료: {Staticinfo:Item:Recipe_Item_Skill_AbyssGear_SwordAura_LV1"
                             "#기어 제작법 : 바람 가르기}를 만든다."),
             std::string("재료: 기어 제작법 : 바람 가르기를 만든다."));
}

TEST(clean_item_desc_leaves_an_unclosed_placeholder_alone) {
    // 전수에는 없다. 모르는 꼴은 손대지 않는다.
    CHECK_EQ(clean_item_desc("{Staticinfo:Item:X#라벨"), std::string("{Staticinfo:Item:X#라벨"));
}

TEST(clean_item_desc_leaves_a_placeholder_without_a_label_alone) {
    CHECK_EQ(clean_item_desc("{Staticinfo:Item:X}"), std::string("{Staticinfo:Item:X}"));
}

TEST(clean_item_desc_leaves_a_placeholder_with_an_empty_label_alone) {
    CHECK_EQ(clean_item_desc("{Staticinfo:A:B#}"), std::string("{Staticinfo:A:B#}"));
}

TEST(clean_item_desc_leaves_a_placeholder_with_two_hashes_alone) {
    CHECK_EQ(clean_item_desc("{Staticinfo:A:B#x#y}"), std::string("{Staticinfo:A:B#x#y}"));
}

TEST(clean_item_desc_does_not_swallow_the_next_placeholder) {
    // 앞 것은 안 닫혀 그대로 두고, 뒤의 온전한 자리표시는 푼다.
    CHECK_EQ(clean_item_desc("{Staticinfo:A:B#x {Staticinfo:C:D#y}"),
             std::string("{Staticinfo:A:B#x y"));
}

TEST(clean_item_desc_trims_spaces_around_line_breaks) {
    // <br/> 옆에 공백이 붙은 설명이 10건이다.
    CHECK_EQ(clean_item_desc("앞 문장. <br/> 뒤 문장."), std::string("앞 문장.\n뒤 문장."));
}

TEST(clean_item_desc_keeps_bracket_headings) {
    // [효과] 139건 · [QA] 14건은 평문이다.
    CHECK_EQ(clean_item_desc("[효과] 공격력 증가"), std::string("[효과] 공격력 증가"));
}

TEST(clean_item_desc_of_empty_is_empty) {
    CHECK(clean_item_desc("").empty());
}

TEST(desc_first_line_takes_the_first_line) {
    CHECK(desc_first_line("첫 줄\n둘째 줄") == std::string_view("첫 줄"));
}

TEST(desc_first_line_skips_leading_blank_lines) {
    CHECK(desc_first_line("\n\n둘째 줄") == std::string_view("둘째 줄"));
}

TEST(desc_first_line_of_one_line_is_the_whole) {
    CHECK(desc_first_line("한 줄") == std::string_view("한 줄"));
}

TEST(desc_first_line_of_empty_is_empty) {
    CHECK(desc_first_line("").empty());
}
