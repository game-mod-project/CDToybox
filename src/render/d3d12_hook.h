#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

namespace cdtb::render {

// 후킹 대상 함수의 주소.
struct VTableAddresses {
    void* create_swap_chain = nullptr;
    void* create_swap_chain_for_hwnd = nullptr;
    void* present = nullptr;
    void* resize_buffers = nullptr;
};

// 더미 D3D12/DXGI 객체를 만들어 vtable에서 함수 주소를 읽는다.
bool acquire_vtable_addresses(VTableAddresses& out);

bool install_hooks();

// 훅 본문에서 예외가 나면 그리기를 영구히 끈다. 그 뒤에도 입력을
// 가로채면 게임 조작이 통째로 막힌다 - 실제로 그렇게 막혔다.
bool render_disabled();
void remove_hooks();

// 이 스왑체인과 짝지어진 커맨드큐. 모르는 스왑체인이면 nullptr.
//
// ExecuteCommandLists에서 처음 본 DIRECT 큐를 잡는 방식은 쓰지 않는다.
// NVIDIA Streamline 문서가 명시하듯 DLSS-G가 활성이면 커맨드큐와
// present가 복수일 수 있어 오버레이는 그런 가정을 해서는 안 된다.
// 대신 CreateSwapChain / CreateSwapChainForHwnd를 가로채 (큐, 스왑체인)
// 쌍을 사실로 확보한다. D3D12에서 이 함수들의 pDevice 인자가 커맨드큐다.
ID3D12CommandQueue* queue_for(IDXGISwapChain* swap_chain);

// 예외 발생 지점을 좁히기 위한 단계 마커. on_frame이 진행하며 갱신하고,
// SEH 핸들러가 예외 코드·주소와 함께 기록한다.
enum FrameStage {
    kStageIdle = 0,
    kStageTeardown = 1,
    kStageInitialize = 2,
    kStageWaitFence = 3,
    kStageNewFrame = 4,
    kStageDrawUi = 5,
    kStageImGuiRender = 6,
    kStageAllocatorReset = 7,
    kStageBarrierToRT = 8,
    kStageOMSetRenderTargets = 9,
    kStageSetDescriptorHeaps = 10,
    kStageRenderDrawData = 11,
    kStageBarrierToPresent = 12,
    kStageCloseList = 13,
    kStageExecute = 14,
    kStageSignal = 15,
};
extern volatile int g_frame_stage;

// 아래 둘은 overlay.cpp가 정의한다.
void on_frame(IDXGISwapChain3* swap_chain, ID3D12CommandQueue* queue);
void on_resize(IDXGISwapChain3* swap_chain, ID3D12CommandQueue* queue);

}  // namespace cdtb::render
