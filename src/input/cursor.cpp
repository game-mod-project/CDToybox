#include "input/cursor.h"

#include <windows.h>

#include "core/log.h"
#include "mem/hook.h"

namespace cdtb::input {
namespace {

using SetCursorPosFn = BOOL(WINAPI*)(int, int);
using ClipCursorFn = BOOL(WINAPI*)(const RECT*);
using ShowCursorFn = int(WINAPI*)(BOOL);

SetCursorPosFn g_orig_set_pos = nullptr;
ClipCursorFn g_orig_clip = nullptr;
ShowCursorFn g_orig_show = nullptr;

bool (*g_is_active)() = nullptr;
bool g_installed = false;
bool g_hidden_by_us = false;
int g_saved_count = 0;
int g_blocked = 0;

// 한 프레임에 이만큼까지만 막는다.
constexpr int kBlockLimit = 500;

bool active() { return g_is_active != nullptr && g_is_active(); }

// 게임은 카메라를 돌리려고 매 프레임 커서를 화면 중앙으로 되돌린다.
// 오버레이가 열려 있는 동안 그대로 두면 커서가 한 점에 붙박여 안
// 움직이는 것처럼 보인다. 실제로 그 증상이 났다 - 처음 열 때는
// 멀쩡하다가 한 번 닫고 게임을 조작한 뒤 다시 열면 굳어 있었다.
BOOL WINAPI det_set_cursor_pos(int x, int y) {
    if (active() && cursor_should_block(g_blocked++, kBlockLimit)) return TRUE;
    return g_orig_set_pos(x, y);
}

// 같은 이유로 커서를 한 구역에 가두는 것도 막는다.
BOOL WINAPI det_clip_cursor(const RECT* rect) {
    if (active() && cursor_should_block(g_blocked++, kBlockLimit)) {
        return g_orig_clip(nullptr);
    }
    return g_orig_clip(rect);
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
    // ShowCursor 는 후킹하지 않는다. 게임은 커서를 켤 때
    // `while (ShowCursor(TRUE) < 0);` 를 쓴다. 훅이 고정된 값을
    // 돌려주면 그 루프가 끝나지 않아 게임이 멎는다 - 실제로 멎었다.
    // 원본만 잡아 두고 우리가 필요할 때 직접 부른다.
    g_orig_show = reinterpret_cast<ShowCursorFn>(
        ::GetProcAddress(user32, "ShowCursor"));
    if (set_pos == nullptr || clip == nullptr || g_orig_show == nullptr) {
        return false;
    }

    g_is_active = is_active;
    const bool a = mem::hook_install(set_pos, &det_set_cursor_pos,
                                     reinterpret_cast<void**>(&g_orig_set_pos));
    const bool b = mem::hook_install(clip, &det_clip_cursor,
                                     reinterpret_cast<void**>(&g_orig_clip));
    if (!a || !b) {
        if (a) mem::hook_remove(set_pos);
        if (b) mem::hook_remove(clip);
        g_is_active = nullptr;
        log::infof("커서 가드 설치 실패 (SetCursorPos={} ClipCursor={})", a, b);
        return false;
    }
    g_installed = true;
    log::infof("커서 가드 설치 완료");
    return true;
}

void cursor_guard_remove() {
    if (!g_installed) return;
    cursor_guard_sync(false);
    HMODULE user32 = ::GetModuleHandleW(L"user32.dll");
    if (user32 != nullptr) {
        mem::hook_remove(
            reinterpret_cast<void*>(::GetProcAddress(user32, "SetCursorPos")));
        mem::hook_remove(
            reinterpret_cast<void*>(::GetProcAddress(user32, "ClipCursor")));
    }
    g_orig_set_pos = nullptr;
    g_orig_clip = nullptr;
    g_orig_show = nullptr;
    g_is_active = nullptr;
    g_installed = false;
    log::infof("커서 가드 원복");
}

bool cursor_guard_installed() { return g_installed; }

void cursor_guard_sync(bool overlay_visible) {
    if (!g_installed) return;
    g_blocked = 0;      // 예산은 프레임마다 되돌린다
    if (overlay_visible == g_hidden_by_us) return;
    if (overlay_visible) {
        g_saved_count = probe_count();
        g_orig_clip(nullptr);      // 게임이 걸어 둔 가두기를 푼다
        // OS 커서를 확실히 띄우고 그걸 쓴다. 숨겨 놓고 ImGui가
        // 따로 그리게 했더니 게임이 다시 띄워 둘로 보였다.
        // ShowCursor 는 후킹할 수 없으므로(게임이 멎는다) 게임과
        // 다투는 대신 하나로 합친다.
        drive_to(0);
        g_hidden_by_us = true;
    } else {
        drive_to(g_saved_count);
        g_hidden_by_us = false;
    }
}

}  // namespace cdtb::input
