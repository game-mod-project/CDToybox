#include "harness.h"
#include "mem/hook.h"

using namespace cdtb::mem;

// MinHook은 최소 5바이트의 프롤로그를 필요로 한다.
// 인라인·폴딩을 막고 함수를 충분히 크게 유지한다.
__declspec(noinline) int target_fn(int x) {
    volatile int a = x;
    a += 1;
    a *= 2;
    a -= 2;
    return a;   // == x * 2
}

using TargetFn = int (*)(int);
static TargetFn o_target = nullptr;

__declspec(noinline) int detour_fn(int x) {
    return o_target(x) + 1;
}

TEST(hook_redirects_and_restores) {
    CHECK(hook_init());

    CHECK(hook_install(reinterpret_cast<void*>(&target_fn),
                       reinterpret_cast<void*>(&detour_fn),
                       reinterpret_cast<void**>(&o_target)));
    CHECK(o_target != nullptr);
    CHECK_EQ(target_fn(5), 11);        // 5*2 + 1

    CHECK(hook_remove(reinterpret_cast<void*>(&target_fn)));
    CHECK_EQ(target_fn(5), 10);        // 원복

    hook_shutdown();
}

TEST(hook_raii_releases_on_scope_exit) {
    CHECK(hook_init());
    {
        Hook h(reinterpret_cast<void*>(&target_fn),
               reinterpret_cast<void*>(&detour_fn),
               reinterpret_cast<void**>(&o_target));
        CHECK(h.ok());
        CHECK_EQ(target_fn(5), 11);
    }
    CHECK_EQ(target_fn(5), 10);        // 소멸자가 해제했다
    hook_shutdown();
}

TEST(hook_install_rejects_null_target) {
    CHECK(hook_init());
    void* dummy = nullptr;
    CHECK(!hook_install(nullptr, reinterpret_cast<void*>(&detour_fn), &dummy));
    hook_shutdown();
}
