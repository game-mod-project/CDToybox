#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

namespace cdtb::render {

// 등급별 글자색과 분류 이름. 아이템 목록과 지급 칸이 같은 것을
// 써야 해서 따로 뺐다.
ImVec4 grade_color(std::uint8_t grade);
const char* category_name(std::uint8_t c);

// --- 필터 줄 ---------------------------------------------------------
//
// 아이템 목록과 인벤토리 창이 **같은 모양**이어야 해서 여기로 뺐다.
// 한쪽만 고치면 두 창이 서로 다르게 생긴다.

// 등급 Combo 의 라벨. 색인 0 이 "전체", 그 뒤가 등급 0..5 다.
extern const char* const kGradeLabels;

float text_width(const char* s);

// 다음 항목이 창 오른쪽을 넘지 않으면 같은 줄에 이어 붙인다.
//
// ImGui 데모의 줄바꿈 관용구다. SameLine 을 무조건 걸면 창을 좁혔을
// 때 오른쪽이 잘려 나가고, 무조건 줄을 바꾸면 넓은 창에서 빈 줄이
// 남는다. 남은 폭을 보고 정한다.
void flow_same_line(float next_width);

// 아이템 표에 실제로 있는 분류만 모아 Combo 용 문자열을 만든다.
// `values` 의 i번째가 Combo 색인 i+1 에 대응한다(0 은 "전체").
void build_category_labels(std::vector<std::uint8_t>* values,
                           std::string* labels);

}  // namespace cdtb::render
