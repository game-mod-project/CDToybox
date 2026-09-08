#pragma once

#include <dxgi1_4.h>

#include <string>

#include "core/config.h"

namespace cdtb::overlay {

// ini 경로도 함께 받는다. 화면에서 바꾼 설정을 그 자리에서 저장하려면
// 어디에 쓸지 알아야 한다(소켓 상한처럼 세션마다 다시 걸어야 하는 것).
void set_config(const Config& cfg, const std::wstring& ini_path);

// 지금 설정의 소켓 상한(0=끔). 화면이 읽는다.
int socket_cap_setting();

// 소켓 상한 설정을 바꾸고 ini 에 저장한다. 저장에 성공하면 true.
// **거는 것은 부르는 쪽 일이다** - 여기서는 설정만 만진다.
bool set_socket_cap_setting(int value);

bool is_visible();
void toggle();

// 토글·언로드 키를 처리한다. 소비했으면 true.
bool handle_hotkey(int vk);

// ImGui와 D3D12 리소스를 해제하고 WndProc을 원복한다.
// 프록시 DLL은 정적 import되어 FreeLibrary 할 수 없으므로,
// "언로드"는 기능 비활성화를 뜻한다. 이후 재초기화가 가능해야 한다.
void shutdown();

}  // namespace cdtb::overlay
