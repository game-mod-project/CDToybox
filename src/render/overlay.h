#pragma once

#include <dxgi1_4.h>

#include "core/config.h"

namespace cdtb::overlay {

void set_config(const Config& cfg);

bool is_visible();
void toggle();

// 토글·언로드 키를 처리한다. 소비했으면 true.
bool handle_hotkey(int vk);

// ImGui와 D3D12 리소스를 해제하고 WndProc을 원복한다.
// 프록시 DLL은 정적 import되어 FreeLibrary 할 수 없으므로,
// "언로드"는 기능 비활성화를 뜻한다. 이후 재초기화가 가능해야 한다.
void shutdown();

}  // namespace cdtb::overlay
