#include "render/overlay.h"

#include <windows.h>

#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <vector>

#include "core/guard.h"
#include "core/log.h"
#include "input/wndproc.h"
#include "render/d3d12_hook.h"
#include "render/diagnostics.h"

// 상태와 헬퍼는 detail에 둔다. cdtb::render::on_frame 이 이 상태에
// 접근해야 하므로 익명 네임스페이스를 쓸 수 없다.
namespace cdtb::overlay::detail {

struct FrameCtx {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
};

Config g_cfg;
bool g_visible = false;
bool g_ready = false;

// 핫키는 게임의 메시지 스레드에서 온다. ImGui와 D3D12 리소스의 수명
// 조작을 거기서 하면 렌더 스레드의 Present와 경합해 크래시한다.
// 요청만 세워두고 실제 해체는 on_frame(렌더 스레드)에서 처리한다.
volatile bool g_teardown_requested = false;

// ImGui 초기화는 세 단계이고 각각 따로 되돌려야 한다. 중간에서 실패했을
// 때 이미 만든 것만 정확히 해체하기 위해 단계별로 기록한다.
bool g_ctx_created = false;
bool g_win32_ready = false;
bool g_dx12_ready = false;

ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;
ID3D12GraphicsCommandList* g_cmd_list = nullptr;
std::vector<FrameCtx> g_frames;

// ImGui 1.92부터 백엔드는 폰트 아틀라스 외 텍스처에도 SRV 디스크립터를
// 요구한다. 단일 디스크립터로는 부족하므로 작은 프리리스트를 둔다.
constexpr UINT kSrvHeapSize = 64;
std::vector<UINT> g_srv_free;
UINT g_srv_increment = 0;

void srv_alloc(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
               D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
    // 계측: 고갈되면 back()/pop_back()이 UB다. RelWithDebInfo는 NDEBUG라
    // IM_ASSERT가 no-op이 되어 조용히 크래시하므로 직접 확인한다.
    log::infof("srv_alloc 요청: 남은 {}개", g_srv_free.size());
    if (g_srv_free.empty()) {
        log::errorf("SRV 디스크립터 고갈 - 힙 크기 {}개로는 부족하다",
                    kSrvHeapSize);
        cpu->ptr = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
        gpu->ptr = g_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr;
        return;
    }
    const UINT idx = g_srv_free.back();
    g_srv_free.pop_back();
    cpu->ptr = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr +
               static_cast<SIZE_T>(idx) * g_srv_increment;
    gpu->ptr = g_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
               static_cast<UINT64>(idx) * g_srv_increment;
}

void srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
              D3D12_GPU_DESCRIPTOR_HANDLE) {
    const SIZE_T base = g_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr;
    g_srv_free.push_back(
        static_cast<UINT>((cpu.ptr - base) / g_srv_increment));
}

void release_resources() {
    g_srv_free.clear();
    for (auto& f : g_frames) {
        if (f.allocator != nullptr) f.allocator->Release();
        if (f.back_buffer != nullptr) f.back_buffer->Release();
    }
    g_frames.clear();

    if (g_cmd_list != nullptr) { g_cmd_list->Release(); g_cmd_list = nullptr; }
    if (g_srv_heap != nullptr) { g_srv_heap->Release(); g_srv_heap = nullptr; }
    if (g_rtv_heap != nullptr) { g_rtv_heap->Release(); g_rtv_heap = nullptr; }
    if (g_device != nullptr) { g_device->Release(); g_device = nullptr; }
}

// ImGui와 D3D12 리소스를 전부 해체한다. g_visible은 건드리지 않으므로
// 해상도 변경 후 재초기화해도 사용자가 열어둔 상태가 유지된다.
void teardown() {
    if (g_dx12_ready) { ImGui_ImplDX12_Shutdown(); g_dx12_ready = false; }
    if (g_win32_ready) { ImGui_ImplWin32_Shutdown(); g_win32_ready = false; }
    if (g_ctx_created) { ImGui::DestroyContext(); g_ctx_created = false; }
    input::remove();
    release_resources();
    g_ready = false;
}

bool initialize(IDXGISwapChain3* sc) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) {
        log::errorf("SwapChain GetDesc 실패");
        return false;
    }
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) {
        log::errorf("SwapChain GetDevice 실패");
        return false;
    }

    const UINT count = desc.BufferCount;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = count;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc,
                                              IID_PPV_ARGS(&g_rtv_heap)))) {
        log::errorf("RTV 힙 생성 실패");
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = kSrvHeapSize;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srv_desc,
                                              IID_PPV_ARGS(&g_srv_heap)))) {
        log::errorf("SRV 힙 생성 실패");
        return false;
    }
    g_srv_increment = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_srv_free.clear();
    g_srv_free.reserve(kSrvHeapSize);
    for (UINT i = kSrvHeapSize; i > 0; --i) g_srv_free.push_back(i - 1);

    const UINT rtv_size = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        g_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    g_frames.resize(count);
    for (UINT i = 0; i < count; ++i) {
        if (FAILED(g_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_frames[i].allocator)))) {
            log::errorf("CommandAllocator {} 생성 실패", i);
            return false;
        }
        if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].back_buffer)))) {
            log::errorf("백버퍼 {} 획득 실패", i);
            return false;
        }
        g_device->CreateRenderTargetView(g_frames[i].back_buffer, nullptr,
                                         handle);
        g_frames[i].rtv = handle;
        handle.ptr += rtv_size;
    }

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           g_frames[0].allocator, nullptr,
                                           IID_PPV_ARGS(&g_cmd_list)))) {
        log::errorf("CommandList 생성 실패");
        return false;
    }
    g_cmd_list->Close();

    ImGui::CreateContext();
    g_ctx_created = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // 기본 폰트(ProggyClean)에는 한글 글리프가 없어 ??로 표시된다.
    // 1.92부터 글리프는 필요할 때 동적으로 래스터화되므로 범위를 지정할
    // 필요 없이 한글이 든 폰트를 얹기만 하면 된다. 파일이 없을 때
    // AddFontFromFileTTF가 단언에 걸리므로 존재를 먼저 확인한다.
    constexpr const char* kKoreanFont = "C:\\Windows\\Fonts\\malgun.ttf";
    if (::GetFileAttributesA(kKoreanFont) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(kKoreanFont, 18.0f);
        log::infof("한글 폰트 로드: {}", kKoreanFont);
    } else {
        log::warnf("맑은 고딕을 찾지 못했다 - 기본 폰트를 쓴다 (한글 깨짐)");
    }

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(desc.OutputWindow)) {
        log::errorf("ImGui_ImplWin32_Init 실패");
        return false;
    }
    g_win32_ready = true;

    // 1.91.5에서 위치인자 초기화가 obsolete 되었다. InitInfo를 쓴다.
    // RTVFormat은 게임의 실제 스왑체인 포맷을 그대로 넘긴다. 상수로
    // 박으면 HDR이나 sRGB 스왑체인에서 렌더가 깨진다.
    ImGui_ImplDX12_InitInfo info{};
    info.Device = g_device;
    info.CommandQueue = cdtb::render::captured_queue();
    info.NumFramesInFlight = static_cast<int>(count);
    info.RTVFormat = desc.BufferDesc.Format;
    info.SrvDescriptorHeap = g_srv_heap;
    info.SrvDescriptorAllocFn = srv_alloc;
    info.SrvDescriptorFreeFn = srv_free;
    if (!ImGui_ImplDX12_Init(&info)) {
        log::errorf("ImGui_ImplDX12_Init 실패");
        return false;
    }
    g_dx12_ready = true;

    input::install(desc.OutputWindow);
    log::infof("오버레이 초기화 완료: 백버퍼 {}개, 포맷 {}, hwnd={}", count,
               static_cast<int>(desc.BufferDesc.Format),
               static_cast<void*>(desc.OutputWindow));
    return true;
}

void draw_ui() {
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("CDToybox — 0단계");

    ImGui::Text("Crimson Desert 2.00.01 / %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("쓰기 기능: %s",
                cdtb::guard::is_safe_to_modify() ? "허용" : "차단 (0단계)");

    if (!g_cfg.show_diagnostics) {
        ImGui::End();
        return;
    }

    const auto& d = cdtb::render::diagnostics();
    ImGui::Separator();

    if (!d.error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "오류: %s",
                           d.error.c_str());
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("모듈", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("베이스    0x%llX",
                    static_cast<unsigned long long>(d.game_base));
        ImGui::Text("이미지    %.1f MB",
                    static_cast<double>(d.game_size) / (1024.0 * 1024.0));
        ImGui::Text("실행 섹션 %zu개", d.game_exec.size());
        ImGui::Indent();
        for (const auto& r : d.game_exec) {
            ImGui::Text("0x%llX  %.1f MB",
                        static_cast<unsigned long long>(r.begin),
                        static_cast<double>(r.size) / (1024.0 * 1024.0));
        }
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("스캐너 진단",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool exact = d.self_marker_found &&
                           d.self_marker_found_at == d.self_marker_expected;
        ImGui::TextColored(exact ? ImVec4(0.4f, 1, 0.4f, 1)
                                 : ImVec4(1, 0.4f, 0.4f, 1),
                           "자기 모듈 마커: %s", exact ? "일치" : "불일치");
        ImGui::Text("  기대 0x%llX / 발견 0x%llX",
                    static_cast<unsigned long long>(d.self_marker_expected),
                    static_cast<unsigned long long>(d.self_marker_found_at));
        ImGui::Text("게임 프롤로그: %zu회 / %.1f ms", d.prologue_hits,
                    d.prologue_ms);
    }

    ImGui::Separator();
    ImGui::Text("Insert 토글 · End 비활성화");
    ImGui::End();
}

}  // namespace cdtb::overlay::detail

namespace cdtb::overlay {

using namespace detail;

void set_config(const Config& cfg) { g_cfg = cfg; }

bool is_visible() { return g_visible && g_ready; }

void toggle() {
    g_visible = !g_visible;
    log::infof("오버레이 {}", g_visible ? "표시" : "숨김");
}

bool handle_hotkey(int vk) {
    if (vk == g_cfg.toggle_key) { toggle(); return true; }
    if (vk == g_cfg.unload_key) { shutdown(); return true; }
    return false;
}

// 메시지 스레드에서 불릴 수 있으므로 요청만 남긴다.
// 실제 해체는 on_frame이 렌더 스레드에서 수행한다.
void shutdown() {
    if (!g_ready && !g_ctx_created) return;
    g_teardown_requested = true;
    log::infof("오버레이 비활성화 요청 - 다음 프레임에 해체한다");
}

}  // namespace cdtb::overlay

// ------------------------------------------------ d3d12_hook.h 의 콜백 구현
namespace cdtb::render {

void on_frame(IDXGISwapChain3* sc) {
    using namespace cdtb::overlay::detail;

    // 해체는 반드시 이 스레드에서 한다 (overlay::shutdown 주석 참조).
    if (g_teardown_requested) {
        g_frame_stage = kStageTeardown;
        g_teardown_requested = false;
        g_visible = false;
        teardown();
        log::infof("오버레이 비활성화 완료 - 토글 키로 재초기화 가능");
        g_frame_stage = kStageIdle;
        return;
    }

    if (!g_ready) {
        g_frame_stage = kStageInitialize;
        if (!initialize(sc)) {
            log::errorf("오버레이 초기화 실패 - 만든 것만 해체하고 중단한다");
            teardown();
            g_frame_stage = kStageIdle;
            return;
        }
        g_ready = true;
    }
    if (!g_visible) { g_frame_stage = kStageIdle; return; }

    ID3D12CommandQueue* queue = captured_queue();
    if (queue == nullptr) { g_frame_stage = kStageIdle; return; }

    g_frame_stage = kStageNewFrame;
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    g_frame_stage = kStageDrawUi;
    draw_ui();

    g_frame_stage = kStageImGuiRender;
    ImGui::Render();

    const UINT idx = sc->GetCurrentBackBufferIndex();
    if (idx >= g_frames.size()) { g_frame_stage = kStageIdle; return; }
    FrameCtx& f = g_frames[idx];

    g_frame_stage = kStageAllocatorReset;
    f.allocator->Reset();
    g_cmd_list->Reset(f.allocator, nullptr);

    // 계측: 첫 프레임에 사용하는 객체 포인터를 남긴다. 손상 여부 판별용.
    static bool logged_ptrs = false;
    if (!logged_ptrs) {
        logged_ptrs = true;
        log::infof("렌더 객체: cmd_list={} back_buffer={} srv_heap={} rtv={:#x}",
                   static_cast<void*>(g_cmd_list),
                   static_cast<void*>(f.back_buffer),
                   static_cast<void*>(g_srv_heap),
                   static_cast<std::uintptr_t>(f.rtv.ptr));
    }

    g_frame_stage = kStageBarrierToRT;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = f.back_buffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_frame_stage = kStageOMSetRenderTargets;
    g_cmd_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);

    g_frame_stage = kStageSetDescriptorHeaps;
    g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);

    g_frame_stage = kStageRenderDrawData;
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);

    g_frame_stage = kStageBarrierToPresent;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_frame_stage = kStageCloseList;
    g_cmd_list->Close();

    g_frame_stage = kStageExecute;
    ID3D12CommandList* lists[] = {g_cmd_list};
    queue->ExecuteCommandLists(1, lists);

    g_frame_stage = kStageIdle;
}

void on_resize() {
    using namespace cdtb::overlay::detail;
    if (!g_ready) return;

    // 백버퍼 참조를 쥔 채로는 ResizeBuffers가 실패한다. 전체를 해체하고
    // 다음 Present에서 새 크기로 다시 만든다. g_visible은 유지된다.
    teardown();
    log::infof("ResizeBuffers 감지 - 다음 프레임에 재초기화한다");
}

}  // namespace cdtb::render
