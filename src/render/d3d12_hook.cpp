#include "render/d3d12_hook.h"

#include <windows.h>

#include "core/log.h"
#include "mem/hook.h"

namespace cdtb::render {
namespace {

// 배포 1에서 (큐, 스왑체인) 쌍 확보를 검증했다.
// CreateSwapChainForHwnd 훅이 잡은 스왑체인이 Present로 넘어오는 것과
// 일치함을 확인했으므로 렌더링을 켠다.
constexpr bool kRenderEnabled = true;

// IDXGIFactory vtable
//   IUnknown 0..2, IDXGIObject 3..6,
//   IDXGIFactory: EnumAdapters=7, MakeWindowAssociation=8,
//                 GetWindowAssociation=9, CreateSwapChain=10
constexpr int kIdxCreateSwapChain = 10;
// IDXGIFactory2 vtable
//   ...IDXGIFactory1: EnumAdapters1=12, IsCurrent=13,
//   IDXGIFactory2: IsWindowedStereoEnabled=14, CreateSwapChainForHwnd=15
constexpr int kIdxCreateSwapChainForHwnd = 15;

// IDXGISwapChain vtable
//   IUnknown 0..2, IDXGIObject 3..6, IDXGIDeviceSubObject 7,
//   IDXGISwapChain: Present=8 ... ResizeBuffers=13
constexpr int kIdxPresent = 8;
constexpr int kIdxResizeBuffers = 13;

using PFN_CreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*,
                                                        IUnknown*,
                                                        DXGI_SWAP_CHAIN_DESC*,
                                                        IDXGISwapChain**);
using PFN_CreateSwapChainForHwnd =
    HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
                                const DXGI_SWAP_CHAIN_DESC1*,
                                const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*,
                                IDXGIOutput*, IDXGISwapChain1**);
using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT,
                                                      UINT, UINT,
                                                      DXGI_FORMAT, UINT);

PFN_CreateSwapChain o_CreateSwapChain = nullptr;
PFN_CreateSwapChainForHwnd o_CreateSwapChainForHwnd = nullptr;
PFN_Present o_Present = nullptr;
PFN_ResizeBuffers o_ResizeBuffers = nullptr;

VTableAddresses g_addrs;
bool g_disabled = false;

// (스왑체인, 커맨드큐) 쌍. 게임이나 Streamline이 스왑체인을 여러 개
// 만들 수 있으므로 하나만 기억하지 않고 전부 담아 대조한다.
struct Pair {
    IDXGISwapChain* swap_chain = nullptr;
    ID3D12CommandQueue* queue = nullptr;
};
constexpr int kMaxPairs = 8;
Pair g_pairs[kMaxPairs];
int g_pair_count = 0;
SRWLOCK g_pairs_lock = SRWLOCK_INIT;

void record_pair(IUnknown* device, IDXGISwapChain* sc, const char* who) {
    if (device == nullptr || sc == nullptr) return;

    // D3D12에서 CreateSwapChain* 의 pDevice는 커맨드큐다.
    // D3D11이면 이 QueryInterface가 실패하므로 자연히 걸러진다.
    ID3D12CommandQueue* q = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&q))) || q == nullptr) {
        return;
    }
    // 게임이 큐의 수명을 소유한다. 우리는 대조용 주소만 들고 있으면
    // 되므로 QueryInterface가 올린 참조는 즉시 되돌린다.
    q->Release();

    ::AcquireSRWLockExclusive(&g_pairs_lock);
    bool known = false;
    for (int i = 0; i < g_pair_count; ++i) {
        if (g_pairs[i].swap_chain == sc) {
            g_pairs[i].queue = q;
            known = true;
            break;
        }
    }
    if (!known && g_pair_count < kMaxPairs) {
        g_pairs[g_pair_count++] = Pair{sc, q};
    }
    const int count = g_pair_count;
    ::ReleaseSRWLockExclusive(&g_pairs_lock);

    log::infof("스왑체인 쌍 확보 [{}]: swapchain={} queue={} (총 {}개)", who,
               static_cast<void*>(sc), static_cast<void*>(q), count);
}

// 예외 코드와 주소를 남긴다. 이것이 없으면 무엇이 터졌는지 알 수 없다.
DWORD g_exc_code = 0;
void* g_exc_addr = nullptr;
int g_exc_stage = 0;

LONG seh_filter(DWORD code, EXCEPTION_POINTERS* ep) {
    g_exc_code = code;
    g_exc_addr = (ep != nullptr && ep->ExceptionRecord != nullptr)
                     ? ep->ExceptionRecord->ExceptionAddress
                     : nullptr;
    g_exc_stage = g_frame_stage;
    return EXCEPTION_EXECUTE_HANDLER;
}

// __try/__except는 소멸자를 가진 C++ 객체와 같은 함수에 있을 수 없다(C2712).
// 그래서 SEH 껍데기와 본문을 분리한다.
void guarded_frame(IDXGISwapChain3* sc, ID3D12CommandQueue* q) {
    __try {
        on_frame(sc, q);
    } __except (seh_filter(GetExceptionCode(), GetExceptionInformation())) {
        g_disabled = true;
    }
}

void guarded_resize(IDXGISwapChain3* sc, ID3D12CommandQueue* q) {
    __try {
        on_resize(sc, q);
    } __except (seh_filter(GetExceptionCode(), GetExceptionInformation())) {
        g_disabled = true;
    }
}

const char* stage_name(int s) {
    switch (s) {
        case kStageTeardown:           return "해체";
        case kStageInitialize:         return "초기화";
        case kStageWaitFence:          return "펜스 대기";
        case kStageNewFrame:           return "NewFrame";
        case kStageDrawUi:             return "draw_ui";
        case kStageImGuiRender:        return "ImGui::Render";
        case kStageAllocatorReset:     return "allocator Reset";
        case kStageBarrierToRT:        return "배리어(→RT)";
        case kStageOMSetRenderTargets: return "OMSetRenderTargets";
        case kStageSetDescriptorHeaps: return "SetDescriptorHeaps";
        case kStageRenderDrawData:     return "RenderDrawData";
        case kStageBarrierToPresent:   return "배리어(→Present)";
        case kStageCloseList:          return "커맨드리스트 Close";
        case kStageExecute:            return "ExecuteCommandLists";
        case kStageSignal:             return "펜스 Signal";
        default:                       return "미상";
    }
}

}  // namespace

bool render_disabled() { return g_disabled; }

namespace {

void log_disabled_once() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    log::errorf("훅 본문 예외: code=0x{:08X} addr={} stage={}({})", g_exc_code,
                g_exc_addr, g_exc_stage, stage_name(g_exc_stage));

    HMODULE mod = nullptr;
    if (g_exc_addr != nullptr &&
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(g_exc_addr), &mod)) {
        wchar_t path[MAX_PATH]{};
        if (::GetModuleFileNameW(mod, path, MAX_PATH) != 0) {
            char narrow[MAX_PATH]{};
            ::WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, MAX_PATH,
                                  nullptr, nullptr);
            log::errorf("예외 모듈: {} (base={})", narrow,
                        static_cast<void*>(mod));
        }
    }
    log::errorf("오버레이를 영구 비활성화한다");
}

// ------------------------------------------------------------------- hooks

HRESULT STDMETHODCALLTYPE hk_CreateSwapChain(IDXGIFactory* self,
                                             IUnknown* device,
                                             DXGI_SWAP_CHAIN_DESC* desc,
                                             IDXGISwapChain** out) {
    const HRESULT hr = o_CreateSwapChain(self, device, desc, out);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
        record_pair(device, *out, "CreateSwapChain");
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_CreateSwapChainForHwnd(
    IDXGIFactory2* self, IUnknown* device, HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs_desc, IDXGIOutput* restrict_to,
    IDXGISwapChain1** out) {
    const HRESULT hr = o_CreateSwapChainForHwnd(self, device, hwnd, desc,
                                                fs_desc, restrict_to, out);
    if (SUCCEEDED(hr) && out != nullptr && *out != nullptr) {
        record_pair(device, *out, "CreateSwapChainForHwnd");
    }
    return hr;
}

// 렌더 핫패스다. 로그와 뮤텍스를 두지 않는다.
HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain3* sc, UINT interval,
                                     UINT flags) {
    static bool logged_first = false;
    ID3D12CommandQueue* q = queue_for(sc);

    if (!logged_first) {
        logged_first = true;
        log::infof("Present 최초 호출: swapchain={} 짝지어진 queue={} {}",
                   static_cast<void*>(sc), static_cast<void*>(q),
                   q != nullptr ? "(쌍 확인됨)" : "(쌍 미확보 - 렌더 안 함)");
    }

    if (kRenderEnabled && !g_disabled && q != nullptr) {
        guarded_frame(sc, q);
        if (g_disabled) log_disabled_once();
    }
    return o_Present(sc, interval, flags);
}

HRESULT STDMETHODCALLTYPE hk_ResizeBuffers(IDXGISwapChain3* sc, UINT count,
                                           UINT w, UINT h, DXGI_FORMAT fmt,
                                           UINT flags) {
    ID3D12CommandQueue* q = queue_for(sc);
    if (kRenderEnabled && !g_disabled && q != nullptr) {
        guarded_resize(sc, q);
        if (g_disabled) log_disabled_once();
    }
    return o_ResizeBuffers(sc, count, w, h, fmt, flags);
}

}  // namespace

volatile int g_frame_stage = kStageIdle;

ID3D12CommandQueue* queue_for(IDXGISwapChain* sc) {
    ID3D12CommandQueue* found = nullptr;
    ::AcquireSRWLockShared(&g_pairs_lock);
    for (int i = 0; i < g_pair_count; ++i) {
        if (g_pairs[i].swap_chain == sc) {
            found = g_pairs[i].queue;
            break;
        }
    }
    ::ReleaseSRWLockShared(&g_pairs_lock);
    return found;
}

bool acquire_vtable_addresses(VTableAddresses& out) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CDToyboxDummyWnd";
    if (::RegisterClassExW(&wc) == 0) {
        log::errorf("더미 윈도우 클래스 등록 실패: {}", ::GetLastError());
        return false;
    }

    HWND hwnd =
        ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0,
                          1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDXGISwapChain* swap = nullptr;
    bool ok = false;

    do {
        if (hwnd == nullptr) {
            log::errorf("더미 윈도우 생성 실패: {}", ::GetLastError());
            break;
        }
        if (FAILED(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                       IID_PPV_ARGS(&device)))) {
            log::errorf("D3D12CreateDevice 실패");
            break;
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
            log::errorf("CreateCommandQueue 실패");
            break;
        }
        if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            log::errorf("CreateDXGIFactory1 실패");
            break;
        }

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 1;
        sd.BufferDesc.Height = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(queue, &sd, &swap))) {
            log::errorf("CreateSwapChain 실패");
            break;
        }

        void** f_vt = *reinterpret_cast<void***>(factory);
        void** sc_vt = *reinterpret_cast<void***>(swap);
        out.create_swap_chain = f_vt[kIdxCreateSwapChain];
        out.create_swap_chain_for_hwnd = f_vt[kIdxCreateSwapChainForHwnd];
        out.present = sc_vt[kIdxPresent];
        out.resize_buffers = sc_vt[kIdxResizeBuffers];
        ok = true;
    } while (false);

    if (swap != nullptr) swap->Release();
    if (factory != nullptr) factory->Release();
    if (queue != nullptr) queue->Release();
    if (device != nullptr) device->Release();
    if (hwnd != nullptr) ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (ok) {
        log::infof("vtable 획득: CreateSwapChain={} ForHwnd={}",
                   out.create_swap_chain, out.create_swap_chain_for_hwnd);
        log::infof("vtable 획득: Present={} ResizeBuffers={}", out.present,
                   out.resize_buffers);
    }
    return ok;
}

bool install_hooks() {
    if (!acquire_vtable_addresses(g_addrs)) return false;
    if (!mem::hook_init()) return false;

    bool ok = true;
    ok &= mem::hook_install(g_addrs.create_swap_chain,
                            reinterpret_cast<void*>(&hk_CreateSwapChain),
                            reinterpret_cast<void**>(&o_CreateSwapChain));
    ok &= mem::hook_install(
        g_addrs.create_swap_chain_for_hwnd,
        reinterpret_cast<void*>(&hk_CreateSwapChainForHwnd),
        reinterpret_cast<void**>(&o_CreateSwapChainForHwnd));
    ok &= mem::hook_install(g_addrs.present,
                            reinterpret_cast<void*>(&hk_Present),
                            reinterpret_cast<void**>(&o_Present));
    ok &= mem::hook_install(g_addrs.resize_buffers,
                            reinterpret_cast<void*>(&hk_ResizeBuffers),
                            reinterpret_cast<void**>(&o_ResizeBuffers));

    log::infof("훅 설치 {} (렌더링 {})", ok ? "성공" : "실패",
               kRenderEnabled ? "활성" : "비활성 - 쌍 확보만 검증");
    return ok;
}

}  // namespace cdtb::render
