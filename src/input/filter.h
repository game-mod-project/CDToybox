#pragma once

namespace cdtb::input {

// 오버레이가 켜져 있을 때 게임에 안 넘길 메시지를 고른다. ImGui·windows.h 없이
// 시험한다. 게임이 raw input(WM_INPUT)으로 마우스를 읽어서, 일반 마우스 메시지만
// 막아서는 창 위에서 시점이 돌고 클릭이 게임 행동이 됐다(사용자 보고 2026-09-11).
enum class Swallow {
    No,          // 게임에 넘긴다
    Zero,        // 삼킨다 (return 0)
    DefWindow,   // 삼키되 DefWindowProc 에 넘겨 정리만 시킨다
                 // (WM_INPUT 은 버퍼 정리, WM_IME_CHAR 는 WM_CHAR 생성이 필요)
};

// raw_type: WM_INPUT 의 RAWINPUTHEADER::dwType. 0 마우스, 1 키보드, 2 HID, -1 모름.
// msg 는 UINT 그대로.
// 글자(WM_CHAR·WM_SYSCHAR·WM_UNICHAR)는 늘, IME 조합은 포커스 때만 막는다.
Swallow swallow_message(unsigned msg, bool overlay_visible, bool want_keyboard,
                        int raw_type);

// 게임의 키 상태 조회(GetAsyncKeyState)에 0 을 돌려줄 것인가. 마우스 버튼은 오버레이가
// 켜져 있으면 늘, 키보드는 글자 입력칸에 포커스가 있을 때만.
bool mask_key_state(int vk, bool overlay_visible, bool want_keyboard);

// 창 마우스 메시지(WM_MOUSEMOVE·버튼·휠)가 끊겼는지 판정한다. 게임의 마우스룩에서는
// OS 커서가 창 안에서 움직여도 창에 WM_MOUSEMOVE 가 안 들어오고(실측 2026-09-12: 열린
// 동안 커서는 움직였는데 ImGui 좌표가 멎음; 왜 안 오는지는 미확정 - raw 등록은 INPUTSINK
// 뿐이라 NOLEGACY 는 아니다), 백엔드는 WM_MOUSEMOVE 를 한 번이라도 받아 추적 중이면
// GetCursorPos 대체 경로를 끈다. 이 판정이 참이면 버튼·휠을 raw input 에서 합성한다
// (mouse.h). 렌더 스레드가 프레임마다 부른다. 규칙:
//  - 창 메시지가 grace 안에 왔으면 곧바로 살아 있음(false) - 합성하면 휠이 두 번 간다.
//  - OS 커서가 grace 이상 이어서 움직이는데 그동안 창 메시지가 없으면 끊김(true).
//  - 커서가 안 움직이는 동안은 이전 판정을 유지한다(가만히 있다 클릭해도 합성이 이어진다).
// 시각은 ms 단조 시계(GetTickCount64). last_legacy_ms 는 다른 스레드가 적어 now 보다
// 클 수 있다 - 그때는 "방금" 으로 본다. 0 은 "한 번도 안 옴".
struct LegacyGateState {
    unsigned long long last_move_ms = 0;   // 마지막으로 OS 커서가 움직인 시각
    unsigned long long run_start_ms = 0;   // 지금 이어지는 움직임이 시작된 시각
    bool dead = false;
};
bool legacy_gate_step(LegacyGateState& s, unsigned long long now_ms, bool os_moved,
                      unsigned long long last_legacy_ms,
                      unsigned long long grace_ms = 300);

// raw 버튼·휠 보고가 창 메시지로 "답을 받았는가". 창 메시지가 살아 있으면 같은 물리 입력의
// 창 메시지가 raw 와 같은 펌프 패스에서 온다 - 도착 시각 뒤(틱 해상도 오차 slack 안에서는
// 앞이어도) 창 마우스 메시지가 하나라도 왔으면 답을 받은 것이고 raw 는 버린다(백엔드가 넣었다).
// 답을 못 받은 채 한동안 남으면 그것이 끊김의 증거다(움직임 없이 누른 첫 클릭을 잃지 않게).
bool raw_answered(unsigned long long raw_at_ms, unsigned long long last_legacy_ms,
                  unsigned long long slack_ms = 32);

// RAWMOUSE 의 usButtonFlags/usButtonData 를 ImGui 버튼 번호(0 왼 1 오른 2 가운데 3 X1
// 4 X2)의 비트마스크와 휠 값(WHEEL_DELTA=120 단위, 부호 있음)으로 푼다. 이동만 있는
// 보고는 전부 0 이다.
struct RawMouseDecoded {
    unsigned down = 0;   // 비트 b 가 1 이면 버튼 b 눌림
    unsigned up = 0;     // 비트 b 가 1 이면 버튼 b 뗌
    int wheel = 0;       // 세로 휠, 앞으로 밀면 양수
    int hwheel = 0;      // 가로 휠, 오른쪽이 양수
};
RawMouseDecoded decode_raw_mouse(unsigned button_flags, unsigned short button_data);

// 이번 프레임에 우리가 커서 좌표를 ImGui 에 넣어야 하는가.
//
// 백엔드가 이미 넣었으면 **넣지 않는다.** 한 프레임에 좌표 이벤트가 둘이면 최종
// 값을 큐 도착 순서가 정하는데, 그 순서는 렌더·펌프 스레드 타이밍이라 프레임마다
// 뒤바뀐다. 둘 다 커서를 재지만 잰 순간이 달라 값이 다르다(실측 2026-09-16 전투:
// 2136 프레임 중 433 프레임 어긋남, 최대 59.8px). 그러면 좌표가 두 자리를 오가고
// (떨림), 뒤로 갔다 오고(굳음), MouseDelta 가 그만큼 튄다(의도치 않은 드래그).
//
// 백엔드가 조용한 프레임에는 우리가 넣는다 - 그것이 2026-09-12 에 고친 "마우스룩
// 중에 ImGui 좌표가 멎는다" 의 해결이고, 그 경로는 그대로 남는다.
bool should_inject_mouse_pos(bool backend_queued_this_frame);

}  // namespace cdtb::input
