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

// 닫을 때 커서 가두기를 어떻게 되돌릴지. 열려 있는 동안 게임이 마지막으로 원한
// 가두기가 fresh_ms 안의 것이면(마우스룩 - 매 프레임 다시 가둔다) 그것을 다시 걸고,
// 오래됐거나 없으면(인벤 같은 커서 UI 로 넘어가 가두기를 그만뒀다) 푼다. 열기 전
// 상태를 그대로 되돌리면 그 사이 인벤을 연 경우 중앙 가두기가 되살아나 커서가
// 굳었다(사용자 보고 2026-09-12).
enum class ClipRestore { Release, Reapply };
ClipRestore cursor_clip_restore(bool seen, unsigned long long last_ms,
                                unsigned long long now_ms,
                                unsigned long long fresh_ms = 500);

// 닫을 때 ShowCursor 카운터의 목표. 열 때 saved 를 기억하고 0 으로 맞췄으니, 그 사이
// 게임이 바꾼 만큼(current - 0)을 saved 에 얹는다 - 게임이 인벤을 열며 +1 했으면
// 닫은 뒤에도 보여야 한다.
int cursor_restore_count(int saved, int current_since_zero);

// 오버레이가 열려 있는 동안 게임이 커서를 되돌리거나 가두거나 다시
// 띄우는 것을 막는다. is_active 는 "지금 오버레이가 열려 있는가" 를
// 돌려준다. 오버레이에 직접 의존하지 않으려고 함수 포인터로 받는다.
bool cursor_guard_install(bool (*is_active)());
void cursor_guard_remove();
bool cursor_guard_installed();

// 오버레이가 열리고 닫히는 순간에 OS 커서 상태를 맞춘다. 열릴 때 지금 카운터를
// 기억해 두고 커서를 띄우며, 닫힐 때 기억한 값에 그 사이 게임이 바꾼 만큼을 얹어
// 되돌리고, 가두기는 게임의 마지막 요청(cursor_clip_restore)대로 한다.
void cursor_guard_sync(bool overlay_visible);

// 게임이 GetAsyncKeyState 로 직접 읽는 키 상태를 오버레이가 켜져 있는
// 동안 걸러 준다. 마우스 버튼은 늘 0, 키보드는 글자 입력칸에 포커스가
// 있을 때만 0. 포커스 여부는 렌더 스레드가 프레임마다 넣어 준다(게임은
// 다른 스레드에서 읽는다).
void cursor_guard_set_want_keyboard(bool want);

}  // namespace cdtb::input
