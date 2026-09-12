#pragma once

namespace cdtb::mem {

// 프로세스당 1회. 중복 호출은 성공으로 취급한다.
bool hook_init();
void hook_shutdown();

// target을 detour로 바꾸고 원본 트램폴린을 *original에 넣은 뒤 활성화한다.
// 같은 target 에 전에 만든 훅이 hook_disable 로 꺼져 있으면 다시 켜기만 한다 -
// 그때 *original 은 건드리지 않으므로 부르는 쪽이 처음 받은 트램폴린을 그대로
// 쥐고 있어야 한다.
bool hook_install(void* target, void* detour, void** original);
// 훅을 끄고 트램폴린까지 해제한다. 디투어 안에 멈춰 있던 다른 스레드가 깨어나
// 해제된 트램폴린을 부를 수 있으므로(Codex 지적 2026-09-11) 프로세스가 사는
// 동안 되돌릴 훅에는 쓰지 말고 hook_disable 을 쓴다.
bool hook_remove(void* target);
// 훅만 끈다(원본 바이트 복구). 트램폴린은 남겨 디투어에 남은 스레드가 안전하게
// 원본으로 빠져나간다. hook_install 로 다시 켤 수 있다.
bool hook_disable(void* target);

// 스코프를 벗어나면 끄는(hook_disable) RAII 래퍼. 트램폴린은 남긴다.
class Hook {
public:
    Hook(void* target, void* detour, void** original);
    ~Hook();

    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;

    bool ok() const { return installed_; }

private:
    void* target_ = nullptr;
    bool installed_ = false;
};

}  // namespace cdtb::mem
