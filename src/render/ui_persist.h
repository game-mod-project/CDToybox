#pragma once

#include "render/layout_table.h"

// 오버레이 UI 상태를 `cdtoybox_ui.ini` 에 실어 보낸다.
//
// ImGui 는 창의 위치·크기·접힘만 스스로 저장한다. 창의 **열림/닫힘**(우리
// 체크박스)과 창 안 **헤더의 펼침**은 저장하지 않는다 - 헤더 상태는 창의
// 메모리 저장소에만 있어 게임을 끄면 사라진다. 그 둘을 ImGui 의 설정
// 핸들러에 얹어 같은 파일, 같은 저장 시점에 태운다.
namespace cdtb::render {

// ImGui 컨텍스트를 만든 **직후, 첫 NewFrame 전에** 부른다. ImGui 는 첫
// 프레임에서 ini 를 읽으므로 그 전에 핸들러가 붙어 있어야 한다.
void ui_persist_install();

// ImGui 가 ini 를 읽었는가(첫 NewFrame 에서 한 번). **그 전에 저장값을 읽으면
// 늘 비어 있어**, 씨를 뿌리는 쪽이 기본값으로 덮어 버린다.
bool ui_persist_loaded();

// 창 열림. 저장값이 없으면 부르는 쪽이 준 `def`.
bool ui_window_open(Win w, bool def);
void ui_set_window_open(Win w, bool open);

// `ImGui::CollapsingHeader` 를 대신한다. 저장값을 첫 프레임에 한 번 적용하고,
// 사용자가 토글하면 그것을 기록한다. **모르는 키는 접힘**이다.
//
// `key` 는 라벨이 아니라 고정 문자열이어야 한다 - 라벨에는 개수처럼 바뀌는
// 것이 붙어 있어(`즐겨찾기 (3)`) 그것을 키로 쓰면 상태가 날아간다.
// `force_open` 은 이번 프레임에만 강제로 펼친다(보관함이 방금 만든 세트를
// 펼치는 것처럼). 그때도 결과는 기록되므로 다음 실행에 그대로 돌아온다.
bool collapsing_header(const char* key, const char* label,
                       bool force_open = false);

}  // namespace cdtb::render
