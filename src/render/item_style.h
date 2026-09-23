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

// 설명 칸(스펙 §2). 칸에는 첫 줄만 그리고 - 열 폭에서 잘린다 - 마우스를 올리면
// **게임 툴팁과 같은 차례**로 효과 줄 · 장착 부위 · 설명 전문을 보인다.
// desc 가 비면 아무것도 안 그린다(툴팁도 없다 - 스펙 §2).
//
// key 로 효과 스냅샷(`game::item_effects_for`)을 조회한다. 프레임마다 도는
// 자리라 해시 조회 한 번이 전부이고, 표를 걷는 일은 배경 스레드가 미리 끝내
// 둔다. 절을 늘어놓는 규칙은 `game::tooltip_sections`(순수 함수)에 있다.
void desc_cell(const std::string& desc, std::uint32_t key);

// --- 필터 줄 ---------------------------------------------------------
//
// 아이템 목록과 인벤토리 창이 **같은 모양**이어야 해서 여기로 뺐다.
// 한쪽만 고치면 두 창이 서로 다르게 생긴다.

// 등급 Combo 의 라벨. 색인 0 이 "전체", 그 뒤가 등급 0..5 다.
extern const char* const kGradeLabels;

// 전용 Combo 의 라벨. 색인 0 이 "전체", 그 뒤가 game::EquipOwner 0..3 이다
// (공용 · 클리프 · 데미안 · 웅카). 글자는 game::owner_label 과 같아야
// 한다 - 고를 때와 표에 보일 때가 달라 보이면 안 된다.
extern const char* const kOwnerLabels;

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
