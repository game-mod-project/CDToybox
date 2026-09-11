#pragma once

#include <imgui.h>

namespace cdtb::render {

// 첫 클릭에 "정말? (3초)" 로 바뀌고 그 안에 다시 누르면 true 를 한 번 낸다.
// guard::is_safe_to_modify() 가 false 면 비활성 + 툴팁. 라벨의 ID 는
// 무장 전후로 같다(### 사용) - PushID 안에서도 쓸 수 있다.
bool confirm_button(const char* label, ImVec2 size = ImVec2(0, 0));
bool confirm_small_button(const char* label);

}  // namespace cdtb::render
