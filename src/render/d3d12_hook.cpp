#include "render/d3d12_hook.h"

#include <windows.h>

#include "core/log.h"
#include "mem/hook.h"

namespace cdtb::render {
namespace {

// IDXGISwapChain vtable
//   IUnknown 0..2, IDXGIObject 3..6, IDXGIDeviceSubObject 7,
//   IDXGISwapChain: Present=8 ... ResizeBuffers=13
constexpr int kIdxPresent = 8;
constexpr int kIdxResizeBuffers = 13;

// ID3D12CommandQueue vtable
//   IUnknown 0..2, ID3D12Object 3..6, ID3D12DeviceChild 7,
//   ID3D12Pageable(추가 없음),
//   ID3D12CommandQueue: UpdateTileMappings=8, CopyTileMappings=9,
//                       ExecuteCommandLists=10
constexpr int kIdxExecuteCommandLists = 10;

using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT,
                                                      UINT, UINT,
                                                      DXGI_FORMAT, UINT);
using PFN_ExecuteCommandLists =
    void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                             ID3D12CommandList* const*);

PFN_Present o_Present = nullptr;
PFN_ResizeBuffers o_ResizeBuffers = nullptr;
PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;

VTableAddresses g_addrs;
ID3D12CommandQueue* g_queue = nullptr;
bool g_disabled = false;
bool g_logged_first_present = false;

// __try/__except는 소멸자를 가진 C++ 객체와 같은 함수에 있을 수 없다(C2712).
// 그래서 SEH 껍데기와 본문을 분리한다.
void guarded_frame(IDXGISwapChain3* sc) {
    __try {
        on_frame(sc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

void guarded_resize() {
    __try {
        on_resize();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

void log_disabled_once() {
    static bool logged = false;
    if (logged) return;
    logged = true;
    log::errorf("훅 본문에서 예외 발생 - 오버레이를 영구 비활성화한다");
}

HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain3* sc, UINT interval,
                                     UINT flags) {
    if (!g_logged_first_present) {
        g_logged_first_present = true;
        log::infof("Present 최초 호출: swapchain={} queue={}",
                   static_cast<void*>(sc), static_cast<void*>(g_queue));
    }
    if (!g_disabled && g_queue != nullptr) {
        guarded_frame(sc);
        if (g_disabled) log_disabled_once();
    }
    return o_Present(sc, interval, flags);
}

HRESULT STDMETHODCALLTYPE hk_ResizeBuffers(IDXGISwapChain3* sc, UINT count,
                                           UINT w, UINT h, DXGI_FORMAT fmt,
                                           UINT flags) {
    if (!g_disabled) {
        guarded_resize();
        if (g_disabled) log_disabled_once();
    }
    return o_ResizeBuffers(sc, count, w, h, fmt, flags);
}

void STDMETHODCALLTYPE hk_ExecuteCommandLists(ID3D12CommandQueue* q, UINT n,
                                              ID3D12CommandList* const* l) {
    if (g_queue == nullptr && q != nullptr) {
        const D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_queue = q;
            log::infof("커맨드큐 캡처: {}", static_cast<void*>(q));
        }
    }
    o_ExecuteCommandLists(q, n, l);
}

}  // namespace

ID3D12CommandQueue* captured_queue() { return g_queue; }

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
    IDXGIFactory4* factory = nullptr;
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

        void** sc_vt = *reinterpret_cast<void***>(swap);
        void** q_vt = *reinterpret_cast<void***>(queue);
        out.present = sc_vt[kIdxPresent];
        out.resize_buffers = sc_vt[kIdxResizeBuffers];
        out.execute_command_lists = q_vt[kIdxExecuteCommandLists];
        ok = true;
    } while (false);

    if (swap != nullptr) swap->Release();
    if (factory != nullptr) factory->Release();
    if (queue != nullptr) queue->Release();
    if (device != nullptr) device->Release();
    if (hwnd != nullptr) ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (ok) {
        log::infof("vtable 획득: Present={} ResizeBuffers={} Exec={}",
                   out.present, out.resize_buffers,
                   out.execute_command_lists);
    }
    return ok;
}

bool install_hooks() {
    if (!acquire_vtable_addresses(g_addrs)) return false;
    if (!mem::hook_init()) return false;

    bool ok = true;
    ok &= mem::hook_install(g_addrs.present,
                            reinterpret_cast<void*>(&hk_Present),
                            reinterpret_cast<void**>(&o_Present));
    ok &= mem::hook_install(g_addrs.resize_buffers,
                            reinterpret_cast<void*>(&hk_ResizeBuffers),
                            reinterpret_cast<void**>(&o_ResizeBuffers));
    ok &= mem::hook_install(g_addrs.execute_command_lists,
                            reinterpret_cast<void*>(&hk_ExecuteCommandLists),
                            reinterpret_cast<void**>(&o_ExecuteCommandLists));

    log::infof("훅 설치 {}", ok ? "성공" : "실패");
    return ok;
}

void remove_hooks() {
    mem::hook_remove(g_addrs.execute_command_lists);
    mem::hook_remove(g_addrs.resize_buffers);
    mem::hook_remove(g_addrs.present);
    g_queue = nullptr;
    log::infof("훅 해제 완료");
}

}  // namespace cdtb::render
