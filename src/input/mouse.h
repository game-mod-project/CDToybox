#pragma once

#include <windows.h>

namespace cdtb::input {

// 오버레이가 열려 있는 동안 ImGui 마우스를 창 메시지에만 기대지 않고 채운다.
//
// 배경(실측 2026-09-12, "오버레이 창에서 마우스가 굳음"): 게임의 마우스룩에서는 OS 커서가
// 창 안에서 움직여도 창에 WM_MOUSEMOVE 가 안 들어온다(왜 안 오는지는 미확정 - raw 등록은
// INPUTSINK 뿐이라 NOLEGACY 는 아니고, 펌프가 거르거나 다른 창이 캡처를 쥐는 등이 남는다;
// 수정은 이유와 무관하게 선다). 백엔드(imgui_impl_win32)는 WM_MOUSEMOVE 를 한 번이라도
// 받아 추적 중(MouseTrackedArea != 0)이면 GetCursorPos 대체 경로를 끄므로, 그 상태로
// 마우스룩에서 열면 소프트 커서가 그 자리에 굳는다(진단 로그: "OS 커서는 창 안에서
// 움직였는데 ImGui 좌표가 멎었다", 포그라운드 true).
//
// - mouse_feed_frame: 렌더 스레드. ImGui_ImplWin32_NewFrame 뒤, ImGui::NewFrame 앞에서
//   게임 창이 포그라운드면 GetCursorPos 를 ImGui 좌표로 넣는다(같은 값은 ImGui 가 거른다).
//   창 메시지가 끊긴 것이 실측되면 raw input 에서 모아 둔 버튼·휠도 넣는다. 증거는 둘:
//   커서가 300ms 이상 이어서 움직이는데 창 메시지가 없음(filter.h legacy_gate_step), 또는
//   raw 버튼·휠이 50ms 넘게 창 메시지로 답을 못 받음(raw_answered - 움직임 없이 누른 첫
//   클릭을 잃지 않게). 창 메시지가 살아 있으면 답 받은 raw 는 버린다(둘 다 넣으면 휠이
//   두 번 간다). 합성으로 내린 버튼의 raw 뗌은 창 메시지가 살아 있어도 넣어 짝을 맞추고,
//   닫을 때 남은 것은 떼어 준다. 다른 창이 앞에 있으면 판정도 입력도 쉰다.
// - mouse_note_legacy: 펌프 스레드(WndProc). 창 마우스 메시지가 올 때마다 시각을 적는다 -
//   오버레이가 닫혀 있을 때도 부른다(열 때의 초기 판정에 쓴다).
// - mouse_on_raw: 펌프 스레드. 오버레이가 열린 동안 WM_INPUT(마우스)을 풀어 버튼·휠만
//   모아 둔다. ImGui 에는 렌더 스레드가 넣는다(펌프 스레드에서 io 큐를 더 만지지 않는다).
// - mouse_sync: 렌더 스레드. 열림/닫힘 전환에 상태를 되돌리고 닫을 때 한 줄 진단을 남긴다.
void mouse_note_legacy(unsigned msg);
void mouse_on_raw(HRAWINPUT handle);
void mouse_sync(bool overlay_visible);
void mouse_feed_frame();

}  // namespace cdtb::input
