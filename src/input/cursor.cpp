#include "input/cursor.h"

#include <windows.h>

#include <atomic>
#include <mutex>

#include "core/log.h"
#include "input/filter.h"
#include "mem/hook.h"

namespace cdtb::input {
namespace {

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using ShowCursorFn = int(WINAPI*)(BOOL);
using GetAsyncKeyStateFn = SHORT(WINAPI*)(int);

SetCursorPosFn g_orig_set_pos = nullptr;
ClipCursorFn g_orig_clip = nullptr;
ShowCursorFn g_orig_show = nullptr;
GetAsyncKeyStateFn g_orig_async = nullptr;

bool (*g_is_active)() = nullptr;
bool g_installed = false;
bool g_async_hooked = false;
bool g_hidden_by_us = false;
int g_saved_count = 0;
// 열려 있는 동안 게임이 마지막으로 원한 가두기(막은 요청). 게임 스레드가 쓰고
// 렌더 스레드가 닫을 때 읽으므로 뮤텍스로 감싼다. 닫을 때 이것을 되돌린다 -
// 열기 전 상태를 되돌리면 그 사이 인벤 같은 커서 UI 를 연 경우 마우스룩의 중앙
// 가두기가 되살아나 커서가 굳었다(사용자 보고 2026-09-12).
std::mutex g_clip_mutex;
bool g_game_clip_seen = false;
bool g_game_clip_null = true;
RECT g_game_clip{};
unsigned long long g_game_clip_ms = 0;
int g_blocked = 0;
bool g_limit_logged = false;

// 게임은 다른 스레드에서 키 상태를 읽고 렌더 스레드가 프레임마다
// 값을 넣으므로 원자로 둔다.
std::atomic<bool> g_want_keyboard{false};

// 한 프레임에 이만큼까지만 막는다.
constexpr int kBlockLimit = 500;

bool active() { return g_is_active != nullptr && g_is_active(); }

// 게임은 카메라를 돌리려고 매 프레임 커서를 화면 중앙으로 되돌린다.
// 오버레이가 열려 있는 동안 그대로 두면 커서가 한 점에 붙박여 안
// 움직이는 것처럼 보인다. 실제로 그 증상이 났다 - 처음 열 때는
// 멀쩡하다가 한 번 닫고 게임을 조작한 뒤 다시 열면 굳어 있었다.
BOOL WINAPI det_set_cursor_pos(int x, int y) {
    // 원본 포인터는 한 번만 읽는다. 훅을 끈 뒤에도 트램폴린은 남겨 두므로
    // (hook_disable) 여기 멈춰 있던 스레드가 깨어나도 안전하다.
    const SetCursorPosFn orig = g_orig_set_pos;
    if (orig == nullptr) return TRUE;
    if (active() && cursor_should_block(g_blocked++, kBlockLimit)) return TRUE;
    return orig(x, y);
}

// 같은 이유로 커서를 한 구역에 가두는 것도 막는다.
BOOL WINAPI det_clip_cursor(const RECT* rect) {
    const ClipCursorFn orig = g_orig_clip;
    if (orig == nullptr) return TRUE;
    if (active() && cursor_should_block(g_blocked++, kBlockLimit)) {
        // 게임이 원한 것을 기억해 둔다 - 닫을 때 그대로 돌려준다.
        {
            std::lock_guard<std::mutex> lock(g_clip_mutex);
            g_game_clip_seen = true;
            g_game_clip_null = (rect == nullptr);
            if (rect != nullptr) g_game_clip = *rect;
            g_game_clip_ms = ::GetTickCount64();
        }
        return orig(nullptr);
    }
    return orig(rect);
}

// 게임은 키 상태를 GetAsyncKeyState 로 직접 읽는다. 오버레이가 켜져
// 있는 동안 마우스 버튼은 늘, 글자 입력칸에 포커스가 있으면 키보드도
// 0 을 돌려준다 - 안 그러면 검색창에 타자를 치는 동안 캐릭터가 움직인다.
SHORT WINAPI det_get_async_key_state(int vk) {
    // 원본 포인터는 한 번만 읽는다(위와 같은 이유).
    const GetAsyncKeyStateFn orig = g_orig_async;
    if (orig == nullptr) return 0;
    if (mask_key_state(vk, active(),
                       g_want_keyboard.load(std::memory_order_relaxed))) {
        return 0;
    }
    return orig(vk);
}

// 지금 카운터를 읽는다. ShowCursor 는 바꾼 뒤의 값을 돌려주므로
// 한 번 올렸다 도로 내려 원래 값을 알아낸다.
int probe_count() {
    const int after_up = g_orig_show(TRUE);
    g_orig_show(FALSE);
    return after_up - 1;
}

void drive_to(int target) {
    const int delta = cursor_show_delta(probe_count(), target);
    for (int i = 0; i < delta; ++i) g_orig_show(TRUE);
    for (int i = 0; i > delta; --i) g_orig_show(FALSE);
}

}  // namespace

int cursor_show_delta(int current, int target) { return target - current; }

ClipRestore cursor_clip_restore(bool seen, unsigned long long last_ms,
                                unsigned long long now_ms,
                                unsigned long long fresh_ms) {
    if (!seen || now_ms < last_ms) return ClipRestore::Release;
    return (now_ms - last_ms <= fresh_ms) ? ClipRestore::Reapply
                                          : ClipRestore::Release;
}

int cursor_restore_count(int saved, int current_since_zero) {
    return saved + current_since_zero;
}

bool cursor_should_block(int consecutive, int limit) {
    return consecutive < limit;
}

bool cursor_guard_install(bool (*is_active)()) {
    if (g_installed) return true;
    if (is_active == nullptr) return false;
    if (!mem::hook_init()) return false;

    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return false;
    void* set_pos = reinterpret_cast<void*>(
        ::GetProcAddress(user32, "SetCursorPos"));
    void* clip = reinterpret_cast<void*>(
        ::GetProcAddress(user32, "ClipCursor"));
    void* async = reinterpret_cast<void*>(
        ::GetProcAddress(user32, "GetAsyncKeyState"));
    // ShowCursor 는 후킹하지 않는다. 게임은 커서를 켤 때
    // `while (ShowCursor(TRUE) < 0);` 를 쓴다. 훅이 고정된 값을
    // 돌려주면 그 루프가 끝나지 않아 게임이 멎는다 - 실제로 멎었다.
    // 원본만 잡아 두고 우리가 필요할 때 직접 부른다.
    g_orig_show = reinterpret_cast<ShowCursorFn>(
        ::GetProcAddress(user32, "ShowCursor"));
    if (set_pos == nullptr || clip == nullptr || async == nullptr ||
        g_orig_show == nullptr) {
        return false;
    }

    g_is_active = is_active;
    const bool a = mem::hook_install(set_pos, &det_set_cursor_pos,
                                     reinterpret_cast<void**>(&g_orig_set_pos));
    const bool b = mem::hook_install(clip, &det_clip_cursor,
                                     reinterpret_cast<void**>(&g_orig_clip));
    if (!a || !b) {
        if (a) mem::hook_disable(set_pos);
        if (b) mem::hook_disable(clip);
        g_is_active = nullptr;
        log::infof("커서 가드 설치 실패 (SetCursorPos={} ClipCursor={})", a, b);
        return false;
    }
    // 세 번째는 선택이다. 이것만 실패해도 커서 가드는 그대로 산다 - 못 걸면
    // 오버레이를 켠 채 타자를 칠 때 캐릭터가 움직이는 것만 남는다. 실패해도
    // 다시 시도하지 않는다(installed 가 true 라 overlay 가 재호출하지 않는다).
    g_async_hooked = mem::hook_install(async, &det_get_async_key_state,
                                       reinterpret_cast<void**>(&g_orig_async));
    if (!g_async_hooked) {
        // 첫 설치 실패면 애초에 널이고, 재설치(끈 훅을 다시 켜기) 실패면 유효한
        // 트램폴린을 쥐고 있는 것이라 널로 만들지 않는다(재리뷰 관찰).
        log::warnf("커서 가드: GetAsyncKeyState 훅 실패 - 오버레이를 켠 채 "
                   "타자를 치면 캐릭터가 움직일 수 있다");
    }
    g_installed = true;
    log::infof("커서 가드 설치 완료 (SetCursorPos·ClipCursor{})",
               g_async_hooked ? "·GetAsyncKeyState" : "");
    return true;
}

void cursor_guard_remove() {
    if (!g_installed) return;
    cursor_guard_sync(false);
    // 훅은 **끄기만** 한다. 게임 스레드가 디투어 본문에 멈춰 있다가 깨어나
    // 트램폴린을 부를 수 있으므로 트램폴린과 원본 포인터·g_is_active 는 프로세스가
    // 사는 동안 그대로 둔다(Codex 지적 2026-09-11). 다시 켤 때는 hook_install 이
    // 같은 훅을 켜기만 하고 포인터는 그대로다.
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        mem::hook_disable(
            reinterpret_cast<void*>(::GetProcAddress(user32, "SetCursorPos")));
        mem::hook_disable(
            reinterpret_cast<void*>(::GetProcAddress(user32, "ClipCursor")));
        if (g_async_hooked) {
            mem::hook_disable(reinterpret_cast<void*>(
                ::GetProcAddress(user32, "GetAsyncKeyState")));
        }
    }
    g_installed = false;
    log::infof("커서 가드 원복 (훅은 끄기만, 트램폴린은 남긴다)");
}

bool cursor_guard_installed() { return g_installed; }

void cursor_guard_set_want_keyboard(bool want) {
    g_want_keyboard.store(want, std::memory_order_relaxed);
}

void cursor_guard_sync(bool overlay_visible) {
    if (!g_installed) return;
    // 상한을 넘겨 통과시킨 프레임이 있으면 한 번 남긴다 - 게임이 "될 때까지
    // 다시 부르는" 꼴인지 로그로 가려야 한다(커서가 튀거나 붙박이는 증상의
    // 후보).
    if (g_blocked > kBlockLimit && !g_limit_logged) {
        log::warnf(
            "커서 가드: 한 프레임에 SetCursorPos/ClipCursor {}회 - 상한 {} 을 "
            "넘어 통과시켰다",
            g_blocked, kBlockLimit);
        g_limit_logged = true;
    }
    g_blocked = 0;      // 예산은 프레임마다 되돌린다
    if (overlay_visible == g_hidden_by_us) return;
    if (overlay_visible) {
        g_saved_count = probe_count();
        {
            std::lock_guard<std::mutex> lock(g_clip_mutex);
            g_game_clip_seen = false;
        }
        g_orig_clip(nullptr);      // 게임이 걸어 둔 가두기를 푼다
        // OS 커서를 확실히 띄우고 그걸 쓴다. 숨겨 놓고 ImGui가
        // 따로 그리게 했더니 게임이 다시 띄워 둘로 보였다.
        // ShowCursor 는 후킹할 수 없으므로(게임이 멎는다) 게임과
        // 다투는 대신 하나로 합친다.
        drive_to(0);
        g_hidden_by_us = true;
    } else {
        // 그 사이 게임이 바꾼 만큼(인벤을 열며 +1 등)을 얹어 되돌린다.
        drive_to(cursor_restore_count(g_saved_count, probe_count()));
        // 가두기는 게임의 마지막 요청대로. 마우스룩이면 방금(500ms 안)도 가두려 했을
        // 테니 그것을 다시 걸고, 커서 UI 로 넘어가 요청이 끊겼으면 푼다.
        {
            bool seen = false, is_null = true;
            RECT rect{};
            unsigned long long at = 0;
            {
                std::lock_guard<std::mutex> lock(g_clip_mutex);
                seen = g_game_clip_seen;
                is_null = g_game_clip_null;
                rect = g_game_clip;
                at = g_game_clip_ms;
            }
            const ClipRestore how =
                cursor_clip_restore(seen, at, ::GetTickCount64());
            if (how == ClipRestore::Reapply && !is_null) {
                g_orig_clip(&rect);
            } else {
                g_orig_clip(nullptr);
            }
        }
        g_hidden_by_us = false;
        g_limit_logged = false;
        // 렌더가 멎은 채 켜져 있어도 키보드가 통째로 안 막히게
        g_want_keyboard.store(false, std::memory_order_relaxed);
    }
}

}  // namespace cdtb::input
