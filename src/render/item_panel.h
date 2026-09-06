#pragma once

namespace cdtb::render {

// 아이템 목록 패널을 그린다. ImGui 프레임 안에서 부른다.
//
// 표는 배경 분석 스레드가 미리 만들어 둔다(game::discover_items).
// 여기서는 걸러 보여주기만 하므로 프레임마다 게임 메모리를 읽지
// 않는다.
void draw_item_panel(bool* open);

}  // namespace cdtb::render
