#pragma once

namespace cdtb::render {

// 재배치를 허용하지 않는 표(Reorderable 없음)의 열을 늘 자연 순서로 둔다. BeginTable 이
// 참을 낸 바로 뒤에 부른다.
//
// ImGui 1.92.9b 는 정렬만 되는 표의 저장 설정에 정렬한 열 한 줄만 적는다. 다시 읽을 때
// 나머지 열은 표시 순서 -1("맨 뒤로")을 받아 **정렬한 열이 맨 앞에 선다**(imgui_tables.cpp
// TableLoadSettingsForColumns 의 "Remaining entries are matched sequentially", 상류
// ocornut/imgui#9519 - 상류 우회 1d7b37d 는 Reorderable 표에만 듣는다). 설정을 읽는
// 프레임에 ImGui 자신의 "열 순서 초기화" 요청을 같이 걸면 TableUpdateLayout 이 설정 적재
// 바로 뒤에 표시 순서만 되돌린다 - 저장된 정렬은 그대로다(헤드리스 실측 2026-09-22).
void table_keep_natural_order();

}  // namespace cdtb::render
