#pragma once

namespace cdtb::render {

// 아이템 목록 창 안에 접히는 절로 붙는다. 게임 함수를 직접 부르므로
// 반드시 렌더 스레드에서 그려야 한다.
void draw_grant_panel();

}  // namespace cdtb::render
