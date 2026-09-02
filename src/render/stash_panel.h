#pragma once

namespace cdtb::render {

// 게임 밖에 두는 아이템 보관함. 즐겨찾기와 세트를 담고 한 번에
// 지급한다. DLL 옆 cdtoybox_stash.txt 에 저장된다.
void draw_stash_panel();

// 아이템 목록의 별표에서 부른다.
void stash_toggle_favorite(unsigned int key);
bool stash_is_favorite(unsigned int key);

}  // namespace cdtb::render
