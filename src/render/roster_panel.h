#pragma once

namespace cdtb::render {

// 탈것·용병·캐릭터 카탈로그 뷰어를 그린다. ImGui 프레임 안에서 부른다.
//
// 표는 배경 분석 스레드가 미리 만들어 둔다(game::discover_roster).
// 여기서는 걸러 보여주기만 하므로 프레임마다 게임 메모리를 읽지 않는다.
// 읽기 전용이다 - 소환·수정은 아직 없다(Tier 2 대상).
void draw_roster_panel(bool* open);

}  // namespace cdtb::render
