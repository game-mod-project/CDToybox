#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

namespace cdtb::render {

struct VTableAddresses {
    void* present = nullptr;
    void* resize_buffers = nullptr;
    void* execute_command_lists = nullptr;
};

// 더미 D3D12 객체를 만들어 vtable에서 함수 주소를 읽는다.
// 실패해도 게임에는 영향이 없다.
bool acquire_vtable_addresses(VTableAddresses& out);

bool install_hooks();
void remove_hooks();

// ExecuteCommandLists 훅이 최초로 캡처한 게임의 커맨드큐.
// 캡처 전에는 nullptr.
ID3D12CommandQueue* captured_queue();

// 예외 발생 지점을 좁히기 위한 단계 마커. on_frame이 진행하며 갱신하고,
// SEH 핸들러가 예외 코드·주소와 함께 기록한다.
enum FrameStage {
    kStageIdle = 0,
    kStageTeardown = 1,
    kStageInitialize = 2,
    kStageNewFrame = 3,
    kStageDrawUi = 4,
    kStageImGuiRender = 5,
    kStageAllocatorReset = 6,
    kStageRecordCommands = 7,
    kStageExecute = 8,
    // stage 7 세분화 - 어느 호출에서 터지는지 좁힌다.
    kStageBarrierToRT = 71,
    kStageOMSetRenderTargets = 72,
    kStageSetDescriptorHeaps = 73,
    kStageRenderDrawData = 74,
    kStageBarrierToPresent = 75,
    kStageCloseList = 76,
};
extern volatile int g_frame_stage;

// 아래 둘은 overlay.cpp가 정의한다.
// Present 훅이 원본을 호출하기 직전에 부른다.
void on_frame(IDXGISwapChain3* swap_chain);
// ResizeBuffers 훅이 원본을 호출하기 직전에 부른다.
void on_resize();

}  // namespace cdtb::render
