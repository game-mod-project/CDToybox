#pragma once

namespace cdtb::mem {

// 프로세스당 1회. 중복 호출은 성공으로 취급한다.
bool hook_init();
void hook_shutdown();

// target을 detour로 바꾸고 원본 트램폴린을 *original에 넣은 뒤 활성화한다.
bool hook_install(void* target, void* detour, void** original);
bool hook_remove(void* target);

// 스코프를 벗어나면 해제하는 RAII 래퍼.
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
