#include "input/wndproc.h"

#include <imgui.h>

#include "core/log.h"
#include "input/filter.h"
#include "render/overlay.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT,
                                                             WPARAM, LPARAM);

namespace cdtb::input {
namespace {

HWND g_hwnd = nullptr;
WNDPROC g_original = nullptr;

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (overlay::handle_hotkey(static_cast<int>(wp))) return 0;
    }

    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);

    const bool visible = overlay::is_visible();

    // 오버레이 위에서는 OS 커서를 끈다. ImGui 가 자기 것을 그리므로 그냥 두면
    // 두 개로 보인다. 마우스를 전부 오버레이가 가지므로 클라이언트 영역
    // 어디서나 끈다.
    if (msg == WM_SETCURSOR && LOWORD(lp) == HTCLIENT && visible) {
        ::SetCursor(nullptr);
        return TRUE;
    }

    if (visible) {
        // raw input 은 장치 종류를 보고 가른다 - 마우스는 늘, 키보드는 입력칸에
        // 포커스가 있을 때만 막는다(오버레이를 켠 채 걷는 것은 되어야 한다).
        int raw_type = -1;
        if (msg == WM_INPUT) {
            RAWINPUTHEADER h{};
            UINT n = sizeof(h);
            if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_HEADER,
                                  &h, &n, sizeof(RAWINPUTHEADER)) == sizeof(h)) {
                raw_type = static_cast<int>(h.dwType);
            }
        }
        switch (swallow_message(msg, true,
                                ImGui::GetIO().WantCaptureKeyboard,
                                raw_type)) {
            case Swallow::Zero: return 0;
            // WM_INPUT 은 DefWindowProc 이 버퍼를 정리한다 - 그냥 0 을
            // 돌려주면 샌다.
            case Swallow::DefWindow: return ::DefWindowProcW(hwnd, msg, wp, lp);
            case Swallow::No: break;
        }
    }
    return ::CallWindowProcW(g_original, hwnd, msg, wp, lp);
}

}  // namespace

void install(HWND hwnd) {
    if (g_original != nullptr || hwnd == nullptr) return;
    g_hwnd = hwnd;
    g_original = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc)));
    log::infof("WndProc 서브클래싱 설치: hwnd={}", static_cast<void*>(hwnd));
}

void remove() {
    if (g_original == nullptr) return;
    ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(g_original));
    g_original = nullptr;
    g_hwnd = nullptr;
    log::infof("WndProc 원복");
}

bool is_installed() { return g_original != nullptr; }

}  // namespace cdtb::input
