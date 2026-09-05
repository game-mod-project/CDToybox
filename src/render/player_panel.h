#pragma once

namespace cdtb::render {

// 플레이어 치트(B-1): Godmode·무한 스태미나·무한 정신력. 게이지 배열을
// 분석 스레드가 발견·freeze 한다(player.{h,cpp}). 위험 타입(발열·탈것 화염)은
// 건드리지 않는다.
void draw_player_panel(bool* open);

}  // namespace cdtb::render
