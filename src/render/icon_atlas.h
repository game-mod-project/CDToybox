#pragma once

#include <cstdint>

#include <imgui.h>

namespace cdtb::render {

struct IconRef {
    ImTextureRef tex;
    ImVec2 uv0{0.0f, 0.0f};
    ImVec2 uv1{1.0f, 1.0f};
    bool valid = false;
};

// DLL 옆의 cdtoybox_icons.bin 을 읽어 ImGui 텍스처로 등록한다.
//
// 파일이 없으면 조용히 실패한다. 아이콘은 있으면 좋은 것이지 없다고
// 목록이 못 뜰 이유가 없다. 아틀라스는 tools/icons/build_icons.py 로
// 만든다 - 저장소에 커밋하지 않는다.
//
// ImGui 컨텍스트와 DX12 백엔드가 준비된 뒤에 부른다.
bool load_icon_atlas();

// ImGui::DestroyContext() **뒤에** 부른다. GPU 자원은 그 전에
// ImGui_ImplDX12_Shutdown 이 이미 정리한다.
void unload_icon_atlas();

bool icons_ready();
int icon_count();

// 키에 해당하는 아이콘. 없으면 valid=false.
IconRef icon_for(std::uint32_t key);

}  // namespace cdtb::render
