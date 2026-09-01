#pragma once

namespace cdtb::input {

// ShowCursor 는 카운터다. 0 이상이면 커서가 보인다. current 에서
// target 으로 옮기려면 몇 번 불러야 하는지 돌려준다. 양수면
// ShowCursor(TRUE) 횟수, 음수면 ShowCursor(FALSE) 횟수다.
int cursor_show_delta(int current, int target);

// 한 프레임에 막을 수 있는 횟수에 상한을 둔다. 게임이 "될 때까지
// 다시 부르는" 관용구를 쓰면 훅이 게임을 가둘 수 있다. 상한을 넘으면
// 그냥 통과시킨다 - 커서가 흔들리는 편이 멎는 것보다 낫다.
bool cursor_should_block(int consecutive, int limit);

// 오버레이가 열려 있는 동안 게임이 커서를 되돌리거나 가두거나 다시
// 띄우는 것을 막는다. is_active 는 "지금 오버레이가 열려 있는가" 를
// 돌려준다. 오버레이에 직접 의존하지 않으려고 함수 포인터로 받는다.
bool cursor_guard_install(bool (*is_active)());
void cursor_guard_remove();
bool cursor_guard_installed();

// 오버레이가 열리고 닫히는 순간에 OS 커서 상태를 맞춘다. 열릴 때
// 지금 카운터를 기억해 두고 숨기며, 닫힐 때 기억한 값으로 되돌린다.
void cursor_guard_sync(bool overlay_visible);

}  // namespace cdtb::input
