#include "harness.h"
#include "input/filter.h"

using cdtb::input::mask_key_state;
using cdtb::input::Swallow;
using cdtb::input::swallow_message;

namespace {
constexpr unsigned kWmInput = 0x00FF, kWmKeyDown = 0x0100, kWmChar = 0x0102,
                   kWmSysKeyDown = 0x0104, kWmSysChar = 0x0106,
                   kWmMouseMove = 0x0200, kWmLButtonDown = 0x0201,
                   kWmMouseWheel = 0x020A, kWmMouseHWheel = 0x020E,
                   kWmAfterMouse = 0x020F;
}  // namespace

TEST(input_filter_passes_everything_when_hidden) {
    CHECK(swallow_message(kWmInput, false, true, 0) == Swallow::No);
    CHECK(swallow_message(kWmMouseMove, false, true, -1) == Swallow::No);
    CHECK(swallow_message(kWmKeyDown, false, true, -1) == Swallow::No);
    CHECK(swallow_message(kWmChar, false, true, -1) == Swallow::No);
    CHECK(!mask_key_state(0x01, false, true));
}

TEST(input_filter_raw_input_by_device_type) {
    // 마우스는 늘
    CHECK(swallow_message(kWmInput, true, false, 0) == Swallow::DefWindow);
    // 키보드는 포커스 없으면 통과
    CHECK(swallow_message(kWmInput, true, false, 1) == Swallow::No);
    CHECK(swallow_message(kWmInput, true, true, 1) == Swallow::DefWindow);
    // HID 는 손대지 않음
    CHECK(swallow_message(kWmInput, true, true, 2) == Swallow::No);
    CHECK(swallow_message(kWmInput, true, true, -1) == Swallow::No);
}

TEST(input_filter_mouse_messages_always_swallowed) {
    CHECK(swallow_message(kWmMouseMove, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmLButtonDown, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmMouseWheel, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmMouseHWheel, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmAfterMouse, true, false, -1) == Swallow::No);
}

TEST(input_filter_keys_only_with_text_focus_chars_always) {
    CHECK(swallow_message(kWmKeyDown, true, false, -1) == Swallow::No);
    CHECK(swallow_message(kWmKeyDown, true, true, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmSysKeyDown, true, true, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmChar, true, false, -1) == Swallow::Zero);
    CHECK(swallow_message(kWmSysChar, true, false, -1) == Swallow::Zero);
}

TEST(input_filter_key_state_mask) {
    CHECK(mask_key_state(0x01, true, false));    // VK_LBUTTON
    CHECK(mask_key_state(0x02, true, false));    // VK_RBUTTON
    CHECK(mask_key_state(0x06, true, false));    // VK_XBUTTON2
    CHECK(!mask_key_state(0x57, true, false));   // 'W' - 포커스 없으면 걷는다
    CHECK(mask_key_state(0x57, true, true));
    // VK_INSERT 는 메시지로 처리되므로 무관
    CHECK(!mask_key_state(0x2D, true, false));
}

TEST(input_filter_ime_and_unichar) {
    CHECK(swallow_message(0x0109, true, false, -1) == Swallow::Zero);       // WM_UNICHAR 는 글자
    CHECK(swallow_message(0x010F, true, true, -1) == Swallow::Zero);        // WM_IME_COMPOSITION - 백엔드가 DefWindowProc 을 부른다
    CHECK(swallow_message(0x010D, true, true, -1) == Swallow::DefWindow);   // WM_IME_STARTCOMPOSITION - 조합 창
    CHECK(swallow_message(0x010E, true, true, -1) == Swallow::DefWindow);   // WM_IME_ENDCOMPOSITION
    CHECK(swallow_message(0x0286, true, true, -1) == Swallow::DefWindow);   // WM_IME_CHAR - WM_CHAR 를 만들어야 ImGui 가 받는다
    CHECK(swallow_message(0x010F, true, false, -1) == Swallow::No);         // 포커스 없으면 통과
    CHECK(swallow_message(0x0286, true, false, -1) == Swallow::No);
    CHECK(swallow_message(0x010F, false, true, -1) == Swallow::No);
}
