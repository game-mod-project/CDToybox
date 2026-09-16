#include "input/mouse.h"

#include <imgui.h>
#include <imgui_internal.h>   // 진단: 백엔드가 큐에 넣은 좌표를 본다

#include <algorithm>
#include <cmath>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <vector>

#include "core/log.h"
#include "input/filter.h"

namespace cdtb::input {
namespace {

constexpr unsigned long long kGraceMs = 300;       // 이만큼 이어서 움직이는데 창 메시지가 없으면 끊김
constexpr unsigned long long kAnswerSlackMs = 32;  // 틱 해상도(≈16ms)의 두 배 - raw 와 창 메시지 짝 맞추기
constexpr unsigned long long kHoldMs = 50;         // raw 버튼·휠이 이만큼 답을 못 받으면 끊김 증거
constexpr size_t kPendingCap = 256;                // 렌더가 멎어도 무한정 쌓이지 않게
constexpr float kWheelDelta = 120.0f;              // WHEEL_DELTA

struct PendingRaw {
    RawMouseDecoded ev;
    unsigned long long at_ms;   // 펌프 스레드가 WM_INPUT 을 처리한 시각
};

// 펌프 스레드가 쓰고 렌더 스레드가 읽는다
std::atomic<unsigned long long> g_last_legacy_ms{0};
std::atomic<unsigned> g_n_legacy_move{0};
std::atomic<unsigned> g_n_legacy_button{0};
std::atomic<unsigned> g_n_legacy_wheel{0};
std::atomic<unsigned> g_n_raw{0};
std::atomic<unsigned> g_n_raw_dropped{0};
std::mutex g_pending_mutex;
std::vector<PendingRaw> g_pending;

// 렌더 스레드만
bool g_open = false;
LegacyGateState g_gate;
POINT g_last_os{};
std::vector<PendingRaw> g_held;   // 펌프에서 가져왔지만 아직 넣을지 버릴지 못 정한 것
unsigned g_synth_down = 0;        // 합성으로 내린 버튼 비트 - 전환·닫기 때 떼어 준다(리뷰 M-2)
unsigned g_n_synth_button = 0;
unsigned g_n_synth_wheel = 0;
unsigned g_n_flip = 0;
bool g_dead_logged = false;

// ---- 진단(기능 없음): 좌표 원천이 둘인가 ----------------------------------
// 우리는 프레임마다 OS 커서 좌표를 넣는데, 창 이동 메시지가 살아 있으면 백엔드도
// 넣는다. 그러면 한 프레임에 좌표 이벤트가 둘이고 최종 좌표는 **큐 도착 순서**가
// 정한다 - 순서가 프레임마다 뒤바뀌면 떨림·굳음·의도치 않은 드래그가 한꺼번에 난다.
// 실제로 둘이 어긋나는지 세어 본다. 어긋남이 0 이면 이 가설은 폐기한다.
unsigned g_n_pos_ours_only = 0;   // 백엔드가 이번 프레임에 안 넣었다(= 마우스룩)
unsigned g_n_pos_both = 0;        // 둘 다 넣었다
unsigned g_n_pos_disagree = 0;    // 그중 좌표가 다른 프레임
float g_max_disagree = 0.0f;      // 가장 크게 어긋난 거리(픽셀)

// 게임이 마우스 raw input 을 어떤 플래그로 등록해 뒀는지(RIDEV_NOLEGACY 0x30 이 섞이면 창
// 메시지가 애초에 안 만들어진다) - 진단용. 조회 실패·등록이 하나도 없음은 "?" 로, 마우스 아닌
// 등록만 있으면 "등록 없음" 으로 가른다(0 과 섞지 않는다, 리뷰 M-4). 첫 호출(버퍼 NULL)의
// 반환값은 문서상 (UINT)-1 + ERROR_INSUFFICIENT_BUFFER 이고 실측으로는 0 이라, 반환값은 안
// 보고 개수만 본다.
std::string raw_mouse_flags_text() {
    UINT count = 0;
    ::GetRegisteredRawInputDevices(nullptr, &count, sizeof(RAWINPUTDEVICE));
    if (count == 0) return "?";
    std::vector<RAWINPUTDEVICE> devs(count);
    const UINT got =
        ::GetRegisteredRawInputDevices(devs.data(), &count, sizeof(RAWINPUTDEVICE));
    if (got == static_cast<UINT>(-1)) return "?";
    bool found = false;
    unsigned flags = 0;
    for (UINT i = 0; i < got; ++i) {
        if (devs[i].usUsagePage == 0x01 && devs[i].usUsage == 0x02) {
            flags |= devs[i].dwFlags;
            found = true;
        }
    }
    return found ? std::format("{:#x}", flags) : std::string("등록 없음");
}

// 이번 프레임에 **백엔드가 이미 큐에 넣은** 마지막 좌표. 이벤트는 ImGui::NewFrame
// 이 처리하므로, 우리가 넣기 직전에 큐를 보면 백엔드가 이 프레임에 무엇을 넣었는지
// 그대로 보인다. io.MousePos 를 보면 안 된다 - 그건 **지난** 프레임이 적용된 값이다.
bool backend_queued_pos(float* x, float* y) {
    const ImGuiContext* g = ImGui::GetCurrentContext();
    if (g == nullptr) return false;
    for (int i = g->InputEventsQueue.Size - 1; i >= 0; --i) {
        const ImGuiInputEvent& e = g->InputEventsQueue[i];
        if (e.Type == ImGuiInputEventType_MousePos) {
            *x = e.MousePos.PosX;
            *y = e.MousePos.PosY;
            return true;
        }
    }
    return false;
}

HWND overlay_hwnd() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    return vp != nullptr ? static_cast<HWND>(vp->PlatformHandleRaw) : nullptr;
}

// 펌프 스레드가 모아 둔 것을 렌더 스레드의 대기 목록으로 옮긴다.
void pull_pending() {
    std::vector<PendingRaw> got;
    {
        std::lock_guard<std::mutex> lock(g_pending_mutex);
        got.swap(g_pending);
    }
    for (const PendingRaw& p : got) {
        if (g_held.size() >= kPendingCap) break;
        g_held.push_back(p);
    }
}

void drop_pending() {
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    g_pending.clear();
}

// 합성으로 내린 채 남은 버튼을 뗀다(리뷰 M-2). 이미 떼어졌으면 ImGui 가 같은 상태를 거른다.
void release_synth_down(ImGuiIO& io) {
    for (int b = 0; b < 5; ++b) {
        if (g_synth_down & (1u << b)) io.AddMouseButtonEvent(b, false);
    }
    g_synth_down = 0;
}

void feed_held(ImGuiIO& io) {
    for (const PendingRaw& p : g_held) {
        for (int b = 0; b < 5; ++b) {
            if (p.ev.down & (1u << b)) {
                // 이미 눌려 있으면(창 메시지로 눌린 것) ImGui 가 거르니 우리 것이 아니다 -
                // 소유권을 표시하면 나중에 남의 버튼을 떼게 된다(리뷰 R-1).
                if (!io.MouseDown[b]) g_synth_down |= 1u << b;
                io.AddMouseButtonEvent(b, true);
                ++g_n_synth_button;
            }
            if (p.ev.up & (1u << b)) {
                io.AddMouseButtonEvent(b, false);
                g_synth_down &= ~(1u << b);
                ++g_n_synth_button;
            }
        }
        if (p.ev.wheel != 0 || p.ev.hwheel != 0) {
            // 백엔드와 같은 규약: 세로는 delta/120, 가로는 부호를 뒤집는다.
            io.AddMouseWheelEvent(-static_cast<float>(p.ev.hwheel) / kWheelDelta,
                                  static_cast<float>(p.ev.wheel) / kWheelDelta);
            ++g_n_synth_wheel;
        }
    }
    g_held.clear();
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
    const PendingRaw p{d, ::GetTickCount64()};
    std::lock_guard<std::mutex> lock(g_pending_mutex);
    if (g_pending.size() >= kPendingCap) {
        g_n_raw_dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    g_pending.push_back(p);
}

void mouse_sync(bool overlay_visible) {
    if (overlay_visible == g_open) return;
    g_open = overlay_visible;
    if (overlay_visible) {
        g_gate = LegacyGateState{};
        ::GetCursorPos(&g_last_os);
        g_held.clear();
        g_synth_down = 0;
        g_n_synth_button = 0;
        g_n_synth_wheel = 0;
        g_n_flip = 0;
        g_dead_logged = false;
        g_n_pos_ours_only = 0;
        g_n_pos_both = 0;
        g_n_pos_disagree = 0;
        g_max_disagree = 0.0f;
        g_n_legacy_move.store(0, std::memory_order_relaxed);
        g_n_legacy_button.store(0, std::memory_order_relaxed);
        g_n_legacy_wheel.store(0, std::memory_order_relaxed);
        g_n_raw.store(0, std::memory_order_relaxed);
        g_n_raw_dropped.store(0, std::memory_order_relaxed);
        drop_pending();
        return;
    }
    drop_pending();
    g_held.clear();
    // 합성으로 내린 채 닫으면 다음에 열 때 눌린 채 시작한다(숨긴 동안은 NewFrame 이 안 돌아
    // 상태가 언 채고, 닫힌 동안은 raw 도 안 모은다, 리뷰 M-2). 해체 경로에서는 컨텍스트가
    // 아직 살아 있지만(teardown 이 뒤) 초기화 실패 경로를 위해 확인한다. 큐에 얹힌 뗌은
    // 다음에 열 때 첫 NewFrame 이 처리한다.
    if (g_synth_down != 0 && ImGui::GetCurrentContext() != nullptr) {
        release_synth_down(ImGui::GetIO());
    }
    g_synth_down = 0;
    // 한 줄 진단: 열린 동안 창 메시지가 왔는지, raw 만 왔는지, 무엇을 합성했는지. "끊김 판정
    // 없음" 은 "창 메시지가 온다" 가 아니라 증거가 없었다는 뜻이다 - 창 메시지 수로 가른다.
    log::infof("마우스 진단: 창 메시지 이동 {} 버튼 {} 휠 {}, raw 마우스 {} (넘쳐 버림 {}), "
               "합성 버튼 {} 휠 {}, 판정 {} (전환 {}회), raw 등록 flags {}",
               g_n_legacy_move.load(std::memory_order_relaxed),
               g_n_legacy_button.load(std::memory_order_relaxed),
               g_n_legacy_wheel.load(std::memory_order_relaxed),
               g_n_raw.load(std::memory_order_relaxed),
               g_n_raw_dropped.load(std::memory_order_relaxed), g_n_synth_button,
               g_n_synth_wheel, g_gate.dead ? "끊김" : "끊김 판정 없음", g_n_flip,
               raw_mouse_flags_text());
    // 진단(기능 없음): 좌표 원천이 둘인 프레임이 얼마나 되고 얼마나 어긋났는가.
    // 어긋남이 0 이면 "원천 둘" 가설은 폐기한다.
    log::infof("마우스 좌표 원천: 우리만 {} 프레임, 둘 다 {} 프레임 (그중 어긋남 {}, "
               "최대 {:.1f}px)",
               g_n_pos_ours_only, g_n_pos_both, g_n_pos_disagree,
               static_cast<double>(g_max_disagree));
}

void mouse_feed_frame() {
    if (!g_open) return;
    const HWND hwnd = overlay_hwnd();
    POINT os{};
    if (hwnd == nullptr || !::GetCursorPos(&os)) {
        drop_pending();
        g_held.clear();
        return;
    }
    const bool moved = os.x != g_last_os.x || os.y != g_last_os.y;
    g_last_os = os;
    // 펌프 것을 가져온 뒤에 시각을 뜬다 - 대기 항목의 at_ms <= now 가 구조적으로 보장돼
    // 아래 뺄셈이 언더플로하지 않는다(리뷰 R-2).
    pull_pending();
    const unsigned long long now = ::GetTickCount64();
    // 다른 창이 앞에 있으면 판정도 입력도 쉰다(리뷰 M-3: 밖에서 움직인 것을 끊김으로 오판해
    // 돌아온 첫 휠이 두 번 간다). raw input 은 INPUTSINK 라 뒤에 있어도 오므로 모아 둔 것은
    // 버리고, 움직임은 돌아온 뒤부터 새로 센다. 포커스를 잃으면 백엔드가 ImGui 의 버튼을 전부
    // 뗀다(WM_KILLFOCUS → AddFocusEvent) - 우리가 내린 것도 같이 풀리므로 마스크만 비운다.
    if (::GetForegroundWindow() != hwnd) {
        g_held.clear();
        g_synth_down = 0;
        g_gate.last_move_ms = 0;
        g_gate.run_start_ms = 0;
        return;
    }
    const unsigned long long last_legacy = g_last_legacy_ms.load(std::memory_order_relaxed);
    const bool was_dead = g_gate.dead;
    bool dead = legacy_gate_step(g_gate, now, moved, last_legacy, kGraceMs);
    const char* why = "커서가 이어서 움직이는 동안 창 메시지가 없다";
    ImGuiIO& io = ImGui::GetIO();
    if (!dead) {
        // 우리가 합성으로 내린 버튼의 뗌은 창 메시지가 살아 있어도 raw 에서 넣어 짝을 맞춘다
        // (raw 는 INPUTSINK 라 회복 뒤에도 온다; 백엔드가 같은 뗌을 또 넣어도 ImGui 가
        // 거른다). 전환 때 일괄로 떼면 아직 쥐고 있는 버튼이 끊긴다(리뷰 R-1).
        if (g_synth_down != 0) {
            for (const PendingRaw& p : g_held) {
                const unsigned owned_up = p.ev.up & g_synth_down;
                for (int b = 0; b < 5; ++b) {
                    if (owned_up & (1u << b)) {
                        io.AddMouseButtonEvent(b, false);
                        g_synth_down &= ~(1u << b);
                        ++g_n_synth_button;
                    }
                }
            }
        }
        // 창 메시지가 답한 raw 는 버린다 - 백엔드가 그 창 메시지로 이미 넣었다.
        g_held.erase(std::remove_if(g_held.begin(), g_held.end(),
                                    [&](const PendingRaw& p) {
                                        return raw_answered(p.at_ms, last_legacy,
                                                            kAnswerSlackMs);
                                    }),
                     g_held.end());
        // 답을 못 받은 채 kHoldMs 를 넘긴 것이 있으면 그것이 끊김의 증거다(리뷰 M-1: 연 직후
        // 움직이지 않고 누른 클릭이 사라지지 않게). 나머지는 답을 기다리며 남긴다.
        if (!g_held.empty() && now - g_held.front().at_ms >= kHoldMs) {
            g_gate.dead = true;
            dead = true;
            why = "raw 버튼·휠이 창 메시지로 답을 못 받았다";
        }
    }
    if (dead != was_dead) {
        ++g_n_flip;
        if (dead && !g_dead_logged) {
            log::infof("마우스: 창 마우스 메시지가 끊겼다({}; 기준 움직임 {}ms 이상·답 {}ms) - "
                       "raw input 으로 버튼·휠을 넣는다 (raw 등록 flags {})",
                       why, kGraceMs, kHoldMs, raw_mouse_flags_text());
            g_dead_logged = true;
        }
        // 끊김→회복 때 일괄로 떼지 않는다 - 우리가 내린 버튼의 raw 뗌은 살아 있어도 위에서
        // 넣으므로 짝은 사용자가 뗄 때 맞는다. 남는 것은 닫기·포그라운드 상실 때 정리한다.
    }
    POINT client = os;
    if (::ScreenToClient(hwnd, &client)) {
        const float ours_x = static_cast<float>(client.x);
        const float ours_y = static_cast<float>(client.y);
        // 진단만: 백엔드가 이번 프레임에 이미 넣은 좌표와 견준다(기능 영향 없음).
        float bx = 0.0f;
        float by = 0.0f;
        if (backend_queued_pos(&bx, &by)) {
            ++g_n_pos_both;
            const float dx = bx - ours_x;
            const float dy = by - ours_y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 0.5f) {
                ++g_n_pos_disagree;
                // windows.h 의 max 매크로를 피한다(NOMINMAX 가 없다).
                if (dist > g_max_disagree) g_max_disagree = dist;
            }
        } else {
            ++g_n_pos_ours_only;
        }
        io.AddMousePosEvent(ours_x, ours_y);
    }
    if (!dead) return;   // 창 메시지가 살아 있으면 백엔드가 넣는다 - 답을 기다리는 raw 는 남는다
    feed_held(io);
}

}  // namespace cdtb::input
