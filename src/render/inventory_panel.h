#pragma once

namespace cdtb::render {

// 인벤토리를 읽어 보여 준다. 게임의 가방을 그대로 훑는 것이 아니라
// 서버 인벤토리 컴포넌트의 레코드를 읽는다.
//
// **버튼을 눌러야 읽는다.** 매 프레임 500칸 넘게 읽으면 프레임이
// 무너진다 - 소켓까지 따라가면 칸마다 포인터를 한 번 더 쫓는다.
void draw_inventory_panel();

}  // namespace cdtb::render
