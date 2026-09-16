#include "input/wndproc.h"

#include <imgui.h>

#include <vector>

#include "core/log.h"
#include "input/filter.h"
#include "input/mouse.h"
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

    // 오버레이가 꺼져 있는 동안 입력을 백엔드에 넘기면 ImGui 이벤트 큐에
    // 쌓이기만 한다 - 비우는 것은 NewFrame 뿐인데 숨김 상태에서는 안 돈다.
    // 켤 때 그 밀린 것이 재생돼 켜기 전의 손놀림·클릭이 되풀이됐다(filter.h).
    if (backend_should_see(msg, overlay::is_visible())) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    }

    // 창 마우스 메시지가 오는지 늘 적어 둔다(닫혀 있을 때도) - 열린 동안 끊기면
    // 렌더 스레드가 raw input 으로 버튼·휠을 합성한다(mouse.h).
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) mouse_note_legacy(msg);

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
                // 버튼·휠을 모아 둔다 - 창 메시지가 끊긴 동안의 대체 입력(mouse.h).
                if (h.dwType == RIM_TYPEMOUSE) {
                    mouse_on_raw(reinterpret_cast<HRAWINPUT>(lp));
                }
            } else {
                // 못 읽으면 게임에 넘긴다(fail-open). 그것을 로그로 알 수 있어야
                // 한다.
                static unsigned s_fail = 0;
                if ((s_fail++ % 1000) == 0) {
                    log::warnf("raw input 헤더를 못 읽어 게임에 넘긴다 ({}회째)",
                               s_fail);
                }
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

    // 게임이 raw input 을 어느 창으로 받는지 한 번 남긴다 - "오버레이를 켜도
    // 시점이 돈다" 가 나면 대상 창이 우리가 서브클래싱한 창인지부터 갈라야 한다.
    // flags 로 RIDEV_INPUTSINK(0x100)·RIDEV_NOLEGACY(0x30) 도 같이 드러난다.
    UINT count = 0;
    if (::GetRegisteredRawInputDevices(nullptr, &count,
                                       sizeof(RAWINPUTDEVICE)) == 0 &&
        count > 0) {
        std::vector<RAWINPUTDEVICE> devs(count);
        const UINT got = ::GetRegisteredRawInputDevices(devs.data(), &count,
                                                        sizeof(RAWINPUTDEVICE));
        if (got != static_cast<UINT>(-1)) {
            for (UINT i = 0; i < got; ++i) {
                log::infof(
                    "raw input 등록: usage {:#x}/{:#x} flags {:#x} target {} ({})",
                    devs[i].usUsagePage, devs[i].usUsage, devs[i].dwFlags,
                    static_cast<void*>(devs[i].hwndTarget),
                    devs[i].hwndTarget == nullptr ? "포커스 창"
                    : devs[i].hwndTarget == hwnd  ? "우리 창"
                                                  : "다른 창");
            }
        }
    }
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
