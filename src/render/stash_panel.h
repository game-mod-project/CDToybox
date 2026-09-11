#pragma once

#include "game/stash.h"

namespace cdtb::render {

// 게임 밖에 두는 아이템 보관함. 즐겨찾기와 세트를 담고 한 번에
// 지급한다. DLL 옆 cdtoybox_stash.txt 에 저장된다.
void draw_stash_panel(bool* open);

// overlay::draw_windows() 가 매 프레임, 창을 그리기 전에 부른다. 창이 닫혀 있어도
// 자동 저장(변경 1초 뒤)과 일괄 지급 큐가 여기서 돈다. 창이 안 그려진 프레임에는
// "펼쳐 둔 세트" 를 비워 인벤 '보관' 이 닫힌 창의 세트에 담지 않게 한다.
void stash_tick();

// 아이템 목록의 별표에서 부른다.
void stash_toggle_favorite(unsigned int key);
bool stash_is_favorite(unsigned int key);

// 인벤토리 창이 "보관함에 담기" 로 부른다.
//
// 지금 펼쳐 둔 세트의 번호. 없으면 -1 이다. 여럿을 펼쳐 두면
// 화면에서 마지막으로 그린 것이 잡힌다 - 아래에 있는 것이다.
int stash_open_set();

// 펼쳐 둔 세트의 이름. 없으면 "". 인벤 '보관' 버튼 툴팁·결과 줄에 쓴다.
// 다음 세트 변경(추가·삭제·다시 읽기)까지만 유효하다 - 보관하지 말고 그 자리에서 쓴다.
const char* stash_open_set_name();

// 그 세트에 한 줄을 넣는다. 번호가 범위 밖이면 false.
// 넣으면 파일을 저장할 것이 있다고 표시한다.
bool stash_add_entry(int set, const game::StashEntry& entry);

}  // namespace cdtb::render
