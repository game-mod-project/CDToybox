#pragma once

namespace cdtb::render {

// 아이템 목록 창 안에 접히는 절로 붙는다. 게임 함수를 직접 부르므로
// 반드시 렌더 스레드에서 그려야 한다.
void draw_grant_panel();

// 아이템 목록에서 줄을 누르면 그 키가 지급 칸으로 들어온다. 6810개
// 중에서 키를 손으로 찾아 넣는 것은 쓸 수 없다.
void set_grant_item_key(unsigned int key);

}  // namespace cdtb::render
