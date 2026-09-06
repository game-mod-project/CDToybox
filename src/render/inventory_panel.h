#pragma once

namespace cdtb::render {

// 인벤토리를 읽어 보여 준다. 게임의 가방을 그대로 훑는 것이 아니라
// 서버 인벤토리 컴포넌트의 레코드를 읽는다.
//
// **버튼을 눌러야 읽는다.** 매 프레임 500칸 넘게 읽으면 프레임이
// 무너진다 - 소켓까지 따라가면 칸마다 포인터를 한 번 더 쫓는다.
void draw_inventory_panel(bool* open);

// 가져오기(import) 전용 지급 큐를 한 개씩 배수한다. overlay 가 매
// 프레임 부른다 - 보관함 창·인벤토리 창을 열지 않아도, 오버레이를
// 숨겨도 진행된다. 보관함(stash) 지급 큐와는 완전히 분리돼 있다.
void inventory_import_pump();

}  // namespace cdtb::render
