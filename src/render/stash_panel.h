#pragma once

#include <cstddef>
#include <vector>

#include "game/stash.h"

namespace cdtb::render {

// 게임 밖에 두는 아이템 보관함. 즐겨찾기와 세트를 담고 한 번에
// 지급한다. DLL 옆 cdtoybox_stash.txt 에 저장된다.
void draw_stash_panel(bool* open);

// 아이템 목록의 별표에서 부른다.
void stash_toggle_favorite(unsigned int key);
bool stash_is_favorite(unsigned int key);

// 인벤토리 창이 "보관함에 담기" 로 부른다.
//
// 지금 펼쳐 둔 세트의 번호. 없으면 -1 이다. 여럿을 펼쳐 두면
// 화면에서 마지막으로 그린 것이 잡힌다 - 아래에 있는 것이다.
int stash_open_set();

// 그 세트에 한 줄을 넣는다. 번호가 범위 밖이면 false.
// 넣으면 파일을 저장할 것이 있다고 표시한다.
bool stash_add_entry(int set, const game::StashEntry& entry);

// -------------------------------------------------------------- 지급 큐(공용)
//
// 세이브 간 이월은 이 큐(지급 경로)가 한다. 인벤토리 창의
// "가져오기" 도 여기에 태운다 - 빈 인벤토리·다른 세이브에서도
// 아이템을 다시 만들어 넣기 때문이다.
//
// 큐 배수(stash_queue_pump)는 overlay 가 매 프레임 부른다. 예전에는
// draw_stash_panel 안에서만 돌아 **보관함 창을 열어 둬야** 지급이
// 진행됐다 - 인벤토리에서 가져오기를 눌러도 창이 닫혀 있으면 멈췄다.

// 목록을 지급 큐 끝에 붙인다. 앞선 지급이 다 끝났으면 큐를 비우고
// 새로 시작한다(무한히 커지지 않게).
void stash_enqueue(const std::vector<game::StashEntry>& items);

// 큐에서 한 개를 지급 시도한다(2초 쿨다운). overlay 가 매 프레임 부른다.
void stash_queue_pump();

// 남은 개수 / 전체 개수(진행 표시용).
std::size_t stash_queue_remaining();
std::size_t stash_queue_total();

}  // namespace cdtb::render
