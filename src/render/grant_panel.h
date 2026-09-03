#pragma once

namespace cdtb::render {

// 아이템 목록 창 안에 접히는 절로 붙는다. 게임 함수를 직접 부르므로
// 반드시 렌더 스레드에서 그려야 한다.
void draw_grant_panel();

// 아이템 목록에서 줄을 누르면 그 키가 지급 칸으로 들어온다. 6810개
// 중에서 키를 손으로 찾아 넣는 것은 쓸 수 없다.
void set_grant_item_key(unsigned int key);

// 보관함이 "지금 고른 것" 을 세트에 담을 때 쓴다. 지급 칸에서
// 고른 담금질과 소켓도 함께 담아야 세트가 온전하다 - 키만 담으면
// 꺼낼 때 맨 아이템이 나온다.
unsigned int grant_item_key();
long long grant_item_count();
unsigned int grant_temper();

// 지급 칸에 고른 보석의 **아이템 키**를 앞에서부터 채워 준다.
// 돌려주는 것은 채운 개수다.
int grant_socket_keys(unsigned int* out, int cap);

}  // namespace cdtb::render
