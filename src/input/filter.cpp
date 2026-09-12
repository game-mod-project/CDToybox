#include "input/filter.h"

namespace cdtb::input {
namespace {

// windows.h 를 안 끌어오려고 값을 그대로 적는다(WinUser.h 의 상수).
constexpr unsigned kWmInput = 0x00FF;
constexpr unsigned kWmKeyFirst = 0x0100;   // WM_KEYDOWN..WM_UNICHAR
constexpr unsigned kWmKeyLast = 0x0109;
constexpr unsigned kWmChar = 0x0102;
constexpr unsigned kWmSysChar = 0x0106;
constexpr unsigned kWmUniChar = 0x0109;
constexpr unsigned kWmImeStart = 0x010D;   // WM_IME_STARTCOMPOSITION
constexpr unsigned kWmImeEnd = 0x010E;     // WM_IME_ENDCOMPOSITION
constexpr unsigned kWmImeComp = 0x010F;    // WM_IME_COMPOSITION
constexpr unsigned kWmImeChar = 0x0286;
constexpr unsigned kWmMouseFirst = 0x0200;  // WM_MOUSEMOVE..WM_MOUSEHWHEEL
constexpr unsigned kWmMouseLast = 0x020E;
constexpr int kRawMouse = 0;
constexpr int kRawKeyboard = 1;

bool is_mouse_button_vk(int vk) {
    // L R M X1 X2
    return vk == 0x01 || vk == 0x02 || vk == 0x04 || vk == 0x05 || vk == 0x06;
}

}  // namespace

Swallow swallow_message(unsigned msg, bool overlay_visible, bool want_keyboard,
                        int raw_type) {
    if (!overlay_visible) return Swallow::No;
    if (msg == kWmInput) {
        if (raw_type == kRawMouse) return Swallow::DefWindow;
        if (raw_type == kRawKeyboard && want_keyboard) return Swallow::DefWindow;
        return Swallow::No;
    }
    // 마우스는 창 위든 빈 곳이든 전부 - 시점 조작 게임이라 반쯤 넘기면 뜻이 없다.
    if (msg >= kWmMouseFirst && msg <= kWmMouseLast) return Swallow::Zero;
    // 글자는 늘 막는다(채팅·명령창으로 새지 않게). WM_UNICHAR 도 글자다.
    if (msg == kWmChar || msg == kWmSysChar || msg == kWmUniChar) {
        return Swallow::Zero;
    }
    // IME 조합(한글)은 입력칸에 포커스가 있을 때만 게임에 안 넘긴다. WM_IME_CHAR 와
    // 조합 시작·끝은 DefWindowProc 이 WM_CHAR 를 만들고 조합 창을 띄워야 ImGui 가
    // 글자를 받으므로 기본 처리는 시킨다(DefWindow). WM_IME_COMPOSITION 은 백엔드가
    // 이미 DefWindowProcW 를 부르니 삼키기만 한다(둘이 부르면 글자가 겹친다).
    if (msg == kWmImeChar || msg == kWmImeStart || msg == kWmImeEnd) {
        return want_keyboard ? Swallow::DefWindow : Swallow::No;
    }
    if (msg == kWmImeComp) return want_keyboard ? Swallow::Zero : Swallow::No;
    // 나머지 키도 입력칸에 포커스가 있을 때만 - 오버레이를 켠 채 걷는 것은
    // 되어야 한다.
    if (msg >= kWmKeyFirst && msg <= kWmKeyLast) {
        return want_keyboard ? Swallow::Zero : Swallow::No;
    }
    return Swallow::No;
}

bool mask_key_state(int vk, bool overlay_visible, bool want_keyboard) {
    if (!overlay_visible) return false;
    if (is_mouse_button_vk(vk)) return true;
    return want_keyboard;
}

}  // namespace cdtb::input
