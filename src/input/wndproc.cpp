#include "input/wndproc.h"

#include <imgui.h>

#include "core/log.h"
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

    // 오버레이가 열려 있을 때만 입력을 게임에 넘기지 않는다.
    // 항상 소비하면 게임 조작이 막힌다.
    if (overlay::is_visible()) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouse_msg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
        const bool key_msg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if ((mouse_msg && io.WantCaptureMouse) ||
            (key_msg && io.WantCaptureKeyboard) || msg == WM_CHAR) {
            return 0;
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
