#include "mem/hook.h"

#include <MinHook.h>

#include "core/log.h"

namespace cdtb::mem {
namespace {
bool g_initialized = false;
}  // namespace

bool hook_init() {
    if (g_initialized) return true;
    const MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        log::errorf("MH_Initialize 실패: {}", static_cast<int>(s));
        return false;
    }
    g_initialized = true;
    return true;
}

void hook_shutdown() {
    if (!g_initialized) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_initialized = false;
}

bool hook_install(void* target, void* detour, void** original) {
    if (target == nullptr || detour == nullptr || original == nullptr) {
        return false;
    }
    if (!hook_init()) return false;

    MH_STATUS s = MH_CreateHook(target, detour, original);
    const bool created = (s == MH_OK);
    if (s == MH_ERROR_ALREADY_CREATED) {
        // hook_disable 로 꺼 둔 훅이다. 트램폴린은 그대로라 *original 을
        // 건드리지 않고 다시 켜기만 한다.
    } else if (s != MH_OK) {
        log::errorf("MH_CreateHook({}) 실패: {}", target, static_cast<int>(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK && s != MH_ERROR_ENABLED) {
        log::errorf("MH_EnableHook({}) 실패: {}", target, static_cast<int>(s));
        if (created) MH_RemoveHook(target);
        return false;
    }
    return true;
}

bool hook_disable(void* target) {
    if (target == nullptr || !g_initialized) return false;
    const MH_STATUS s = MH_DisableHook(target);
    return s == MH_OK || s == MH_ERROR_DISABLED;
}

bool hook_remove(void* target) {
    if (target == nullptr || !g_initialized) return false;
    MH_DisableHook(target);
    return MH_RemoveHook(target) == MH_OK;
}

Hook::Hook(void* target, void* detour, void** original) : target_(target) {
    installed_ = hook_install(target, detour, original);
}

Hook::~Hook() {
    // 해제가 아니라 끄기 - 디투어에 남은 스레드가 트램폴린을 부를 수 있다.
    if (installed_) hook_disable(target_);
}

}  // namespace cdtb::mem
