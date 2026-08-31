#include "render/scan_panel.h"

#include <windows.h>

#include <imgui.h>

#include <string>

#include "game/camera.h"

namespace cdtb::render {

// 이 패널은 읽기 전용이다. 버튼이 없다.
//
// 이전에는 메모리 스캔과 카메라 탐색을 사용자가 눌러 진행하는
// UI였다. 그건 조작자가 사람이라는 전제이고, 그러면 모드를 만든
// 의미가 없다. 분석은 game::start_auto_analysis() 가 백그라운드에서
// 스스로 하고, 여기서는 그 결과만 보여준다.
void draw_camera_panel() {
    ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("카메라 분석");

    if (!game::discovered()) {
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1), "분석 중...");
        ImGui::TextWrapped(
            "월드에 진입하면 자동으로 카메라를 찾습니다. "
            "따로 하실 일은 없습니다. 결과는 bin64\\CDToybox.log 에 "
            "남습니다.");
        ImGui::End();
        return;
    }

    const auto& c = game::cameras();

    struct Row { const char* label; std::uintptr_t addr; };
    const Row rows[] = {
        {"CameraManager", c.manager},
        {"FreeCamCamera", c.free_cam},
        {"PhotoCamera", c.photo_cam},
        {"PlayerCameraComponent", c.player_component},
        {"활성 카메라", c.active},
    };
    for (const auto& row : rows) {
        ImGui::Text("%-22s 0x%llX", row.label,
                    static_cast<unsigned long long>(row.addr));
    }

    ImGui::Separator();
    // 카메라 월드 좌표는 카메라 객체가 아니라 컴포넌트에 있다.
    // 카메라의 지역 변환은 항등이라 표시할 값이 없다.
    float world[3]{};
    if (game::read_world_position(c.player_component, world)) {
        ImGui::Text("월드좌표  %9.2f %9.2f %9.2f", world[0], world[1], world[2]);
    }

    const Row cams[] = {{"활성", c.active}, {"프리캠", c.free_cam}};
    for (const auto& cam : cams) {
        if (cam.addr == 0) continue;
        float fov = 0.0f;
        std::string name;
        game::read_fov(cam.addr, &fov);
        game::read_name(cam.addr, &name);
        ImGui::Text("[%s] %s   fov %6.2f", cam.label, name.c_str(), fov);
    }

    ImGui::End();
}

void draw_scan_panel() {}   // 제거됨. 호출부 호환을 위해 남긴다.

void shutdown_scan_panel() { game::stop_auto_analysis(); }

}  // namespace cdtb::render
