#include "input/mouse.h"

#include <imgui.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "core/log.h"
#include "input/filter.h"

namespace cdtb::input {
namespace {

constexpr unsigned long long kGraceMs = 300;   // 이만큼 움직였는데 창 메시지가 없으면 끊김
constexpr size_t kPendingCap = 256;            // 렌더가 멎어도 무한정 쌓이지 않게
constexpr float kWheelDelta = 120.0f;          // WHEEL_DELTA

// 펌프 스레드가 쓰고 렌더 스레드가 읽는다
std::atomic<unsigned long long> g_last_legacy_ms{0};
std::atomic<unsigned> g_n_legacy_move{0};
std::atomic<unsigned> g_n_legacy_button{0};
std::atomic<unsigned> g_n_legacy_wheel{0};
std::atomic<unsigned> g_n_raw{0};
std::atomic<unsigned> g_n_raw_dropped{0};
std::mutex g_pending_mutex;
std::vector<RawMouseDecoded> g_pending;

// 렌더 스레드만
bool g_open = false;
LegacyGateState g_gate;
POINT g_last_os{};
unsigned g_n_synth_button = 0;
unsigned g_n_synth_wheel = 0;
unsigned g_n_flip = 0;
bool g_dead_logged = false;

// 게임이 마우스 raw input 을 어떤 플래그로 등록해 뒀는지(RIDEV_NOLEGACY 0x30 이면 창
// 메시지가 애초에 안 만들어진다) - 진단용.
unsigned raw_mouse_flags() {
    UINT count = 0;
    if (::GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE)) != 0 ||
        count == 0) {
        return 0;
    }
    std::vector<RAWINPUTDEVICE> devs(count);
    const UINT got =
        ::GetRegisteredRawInputDevices(devs.data(), &count, sizeof(RAWINPUTDEVICE));
    if (got == static_cast<UINT>(-1)) return 0;
    unsigned flags = 0;
    for (UINT i = 0; i < got; ++i) {
        if (devs[i].usUsagePage == 0x01 && devs[i].usUsage == 0x02) {
            flags |= devs[i].dwFlags;
        }
    }
    return flags;
}

HWND overlay_hwnd() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return vp != nullptr ? static_cast<HWND>(vp->PlatformHandleRaw) : nullptr;
}

std::vector<RawMouseDecoded> take_pending() {
    std::vector<RawMouseDecoded> out;
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    out.swap(g_pending);
    return out;
}

}  // namespace

void mouse_note_legacy(unsigned msg) {
    g_last_legacy_ms.store(::GetTickCount64(), std::memory_order_relaxed);
    if (msg == WM_MOUSEMOVE) {
        g_n_legacy_move.fetch_add(1, std::memory_order_relaxed);
    } else if (msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL) {
        g_n_legacy_wheel.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_n_legacy_button.fetch_add(1, std::memory_order_relaxed);
    }
}

void mouse_on_raw(HRAWINPUT handle) {
    RAWINPUT ri{};
    UINT size = sizeof(ri);
    if (::GetRawInputData(handle, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) ==
        static_cast<UINT>(-1)) {
        return;
    }
    if (ri.header.dwType != RIM_TYPEMOUSE) return;
    g_n_raw.fetch_add(1, std::memory_order_relaxed);
    const RawMouseDecoded d =
        decode_raw_mouse(ri.data.mouse.usButtonFlags, ri.data.mouse.usButtonData);
    if (d.down == 0 && d.up == 0 && d.wheel == 0 && d.hwheel == 0) return;   // 이동만
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    if (g_pending.size() >= kPendingCap) {
        g_n_raw_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_pending.push_back(d);
}

void mouse_sync(bool overlay_visible) {
    if (overlay_visible == g_open) return;
    g_open = overlay_visible;
    if (overlay_visible) {
        g_gate = LegacyGateState{};
        ::GetCursorPos(&g_last_os);
        g_n_synth_button = 0;
        g_n_synth_wheel = 0;
        g_n_flip = 0;
        g_dead_logged = false;
        g_n_legacy_move.store(0, std::memory_order_relaxed);
        g_n_legacy_button.store(0, std::memory_order_relaxed);
        g_n_legacy_wheel.store(0, std::memory_order_relaxed);
        g_n_raw.store(0, std::memory_order_relaxed);
        g_n_raw_dropped.store(0, std::memory_order_relaxed);
        take_pending();
        return;
    }
    take_pending();
    // 한 줄 진단: 열린 동안 창 메시지가 왔는지, raw 만 왔는지, 무엇을 합성했는지.
    log::infof("마우스 진단: 창 메시지 이동 {} 버튼 {} 휠 {}, raw 마우스 {} (넘쳐 버림 {}), "
               "합성 버튼 {} 휠 {}, 창 메시지 {} (전환 {}회), raw 등록 flags {:#x}",
               g_n_legacy_move.load(std::memory_order_relaxed),
               g_n_legacy_button.load(std::memory_order_relaxed),
               g_n_legacy_wheel.load(std::memory_order_relaxed),
               g_n_raw.load(std::memory_order_relaxed),
               g_n_raw_dropped.load(std::memory_order_relaxed), g_n_synth_button,
               g_n_synth_wheel, g_gate.dead ? "끊김" : "살아 있음", g_n_flip,
               raw_mouse_flags());
}

void mouse_feed_frame() {
    if (!g_open) return;
    const HWND hwnd = overlay_hwnd();
    POINT os{};
    if (hwnd == nullptr || !::GetCursorPos(&os)) {
        take_pending();
        return;
    }
    const bool moved = os.x != g_last_os.x || os.y != g_last_os.y;
    g_last_os = os;
    const unsigned long long now = ::GetTickCount64();
    const bool was_dead = g_gate.dead;
    const bool dead = legacy_gate_step(
        g_gate, now, moved, g_last_legacy_ms.load(std::memory_order_relaxed), kGraceMs);
    if (dead != was_dead) {
        ++g_n_flip;
        if (dead && !g_dead_logged) {
            log::infof("마우스: 창 메시지가 끊겼다 - 커서는 움직이는데 {}ms 넘게 WM_MOUSEMOVE "
                       "가 없다, raw input 으로 버튼·휠을 넣는다 (raw 등록 flags {:#x})",
                       kGraceMs, raw_mouse_flags());
            g_dead_logged = true;
        }
    }
    const std::vector<RawMouseDecoded> pending = take_pending();
    // 백엔드와 같은 조건 - 다른 창이 앞에 있으면 넣지 않는다. raw input 은 INPUTSINK 라
    // 뒤에 있어도 오므로 모아 둔 것도 버린다.
    if (::GetForegroundWindow() != hwnd) return;
    ImGuiIO& io = ImGui::GetIO();
    POINT client = os;
    if (::ScreenToClient(hwnd, &client)) {
        io.AddMousePosEvent(static_cast<float>(client.x), static_cast<float>(client.y));
    }
    if (!dead) return;   // 창 메시지가 살아 있으면 백엔드가 이미 넣었다
    for (const RawMouseDecoded& ev : pending) {
        for (int b = 0; b < 5; ++b) {
            if (ev.down & (1u << b)) {
                io.AddMouseButtonEvent(b, true);
                ++g_n_synth_button;
            }
            if (ev.up & (1u << b)) {
                io.AddMouseButtonEvent(b, false);
                ++g_n_synth_button;
            }
        }
        if (ev.wheel != 0 || ev.hwheel != 0) {
            // 백엔드와 같은 규약: 세로는 delta/120, 가로는 부호를 뒤집는다.
            io.AddMouseWheelEvent(-static_cast<float>(ev.hwheel) / kWheelDelta,
                                  static_cast<float>(ev.wheel) / kWheelDelta);
            ++g_n_synth_wheel;
        }
    }
}

}  // namespace cdtb::input
