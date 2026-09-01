#pragma once

#include <cstdint>

#include <imgui.h>

namespace cdtb::render {

// 등급별 글자색과 분류 이름. 아이템 목록과 지급 칸이 같은 것을
// 써야 해서 따로 뺐다.
ImVec4 grade_color(std::uint8_t grade);
const char* category_name(std::uint8_t c);

}  // namespace cdtb::render
