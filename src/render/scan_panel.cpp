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
    const Row cams[] = {{"활성", c.active}, {"프리캠", c.free_cam}};
    for (const auto& cam : cams) {
        if (cam.addr == 0) continue;
        float fov = 0.0f, pos[3]{}, rot[4]{};
        std::string name;
        game::read_fov(cam.addr, &fov);
        game::read_position(cam.addr, pos);
        game::read_rotation(cam.addr, rot);
        game::read_name(cam.addr, &name);

        ImGui::Text("[%s] %s", cam.label, name.c_str());
        ImGui::Indent();
        ImGui::Text("fov %7.2f   pos %9.2f %9.2f %9.2f", fov, pos[0], pos[1],
                    pos[2]);
        ImGui::Text("rot %7.4f %7.4f %7.4f %7.4f", rot[0], rot[1], rot[2],
                    rot[3]);
        ImGui::Unindent();
    }

    ImGui::End();
}

void draw_scan_panel() {}   // 제거됨. 호출부 호환을 위해 남긴다.

void shutdown_scan_panel() { game::stop_auto_analysis(); }

}  // namespace cdtb::render
