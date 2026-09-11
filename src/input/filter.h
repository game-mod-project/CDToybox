#pragma once

namespace cdtb::input {

// 오버레이가 켜져 있을 때 게임에 안 넘길 메시지를 고른다. ImGui·windows.h 없이
// 시험한다. 게임이 raw input(WM_INPUT)으로 마우스를 읽어서, 일반 마우스 메시지만
// 막아서는 창 위에서 시점이 돌고 클릭이 게임 행동이 됐다(사용자 보고 2026-09-11).
enum class Swallow {
    No,          // 게임에 넘긴다
    Zero,        // 삼킨다 (return 0)
    DefWindow,   // 삼키되 DefWindowProc 에 넘겨 정리만 시킨다
                 // (WM_INPUT 은 버퍼 정리가 필요)
};

// raw_type: WM_INPUT 의 RAWINPUTHEADER::dwType. 0 마우스, 1 키보드, 2 HID, -1 모름.
// msg 는 UINT 그대로.
Swallow swallow_message(unsigned msg, bool overlay_visible, bool want_keyboard,
                        int raw_type);

// 게임의 키 상태 조회(GetAsyncKeyState)에 0 을 돌려줄 것인가. 마우스 버튼은 오버레이가
// 켜져 있으면 늘, 키보드는 글자 입력칸에 포커스가 있을 때만.
bool mask_key_state(int vk, bool overlay_visible, bool want_keyboard);

}  // namespace cdtb::input
