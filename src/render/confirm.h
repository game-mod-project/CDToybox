#pragma once

#include <imgui.h>

namespace cdtb::render {

// 첫 클릭에 "정말? (3초)" 로 바뀌고 그 안에 다시 누르면 true 를 한 번 낸다.
// guard::is_safe_to_modify() 가 false 면 비활성 + 툴팁. 라벨의 ID 는
// 무장 전후로 같다(### 사용) - PushID 안에서도 쓸 수 있다.
bool confirm_button(const char* label, ImVec2 size = ImVec2(0, 0));
// needs_write_guard=false 면 쓰기 잠금과 무관하게 무장·확인만 한다 - 게임 메모리가
// 아니라 파일을 바꾸는 동작(보관함 세트 지우기)에 쓴다. 기본은 잠금을 본다.
bool confirm_small_button(const char* label, bool needs_write_guard = true);

}  // namespace cdtb::render
