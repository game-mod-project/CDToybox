#include <windows.h>

#include <string>

#include "core/config.h"
#include "core/log.h"
#include "proxy/xinput_proxy.h"
#include "render/d3d12_hook.h"
#include "game/camera.h"
#include "render/overlay.h"
#include "mem/watchpoint.h"

namespace {
HMODULE g_self = nullptr;
}

namespace cdtb {

// 우리 DLL이 있는 디렉터리. 뒤에 역슬래시가 붙는다.
std::wstring self_directory() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".\\";
    std::wstring p(buf, n);
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

}  // namespace cdtb

namespace {

DWORD WINAPI init_thread(LPVOID) {
    const std::wstring dir = cdtb::self_directory();

    cdtb::log::init(dir + L"CDToybox.log");
    cdtb::log::infof("CDToybox 0단계 시작");

    const std::wstring ini = dir + L"CDToybox.ini";
    const cdtb::Config cfg = cdtb::config::load(ini);
    cdtb::log::infof("설정: toggle=0x{:X} unload=0x{:X} diagnostics={}"
                     " socket_cap={}",
                     cfg.toggle_key, cfg.unload_key, cfg.show_diagnostics,
                     cfg.socket_cap);

    cdtb::overlay::set_config(cfg, ini);

    if (!cdtb::render::install_hooks()) {
        cdtb::log::errorf("렌더 훅 설치 실패 - 오버레이 없이 계속한다");
    }

    // 사용자가 버튼을 누를 필요 없이 스스로 분석한다.
    cdtb::game::start_auto_analysis();

    cdtb::log::infof("초기화 완료");
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        ::DisableThreadLibraryCalls(module);

        // 실패해도 TRUE를 반환한다. 스텁이 ERROR_DEVICE_NOT_CONNECTED를
        // 돌려주므로 게임은 컨트롤러 없음으로 인식하고 진행한다.
        // FALSE를 반환하면 게임 로드 자체가 중단된다.
        cdtb::proxy::load_original();

        // 로더 락 안에서는 무거운 작업을 하지 않는다.
        const HANDLE t =
            ::CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr);
        if (t != nullptr) ::CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        // 로더 락 안이다. 스레드를 join 하면 교착할 수 있으므로
        // 하지 않는다. 디버그 레지스터만 확실히 내린다 - 남겨 두면
        // 핸들러가 사라진 뒤 처리되지 않은 단일 스텝 예외로 게임이
        // 죽는다. 2026-08-31 실측에서 정확히 그렇게 죽었다.
        cdtb::mem::WriteWatch::shutdown();
        cdtb::log::shutdown();
    }
    return TRUE;
}
