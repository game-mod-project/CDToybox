#include "render/scan_panel.h"

#include <windows.h>

#include <imgui.h>

#include <string>

#include "game/camera.h"
#include "game/freecam.h"
#include "render/colors.h"
#include "render/layout.h"

namespace cdtb::render {

// 이 패널은 읽기 전용이다. 버튼이 없다.
//
// 이전에는 메모리 스캔과 카메라 탐색을 사용자가 눌러 진행하는
// UI였다. 그건 조작자가 사람이라는 전제이고, 그러면 모드를 만든
// 의미가 없다. 분석은 game::start_auto_analysis() 가 백그라운드에서
// 스스로 하고, 여기서는 그 결과만 보여준다.
void draw_camera_panel(bool* open) {
    if (!begin_window(Win::Camera, open)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("개발 진단용입니다 - 평소에는 열 필요 없습니다.");

    if (!game::discovered()) {
        // GetTime 은 오버레이가 뜬 뒤의 누적 시간이다. 분석 시작 시각은 안 잡으므로
        // 무엇을 센 숫자인지 문구에 적는다 - 멈춘 건지 가리는 용도로는 충분하다.
        ImGui::TextColored(col::kBusy, "분석 중... (오버레이 켜진 뒤 %.0f초)",
                           ImGui::GetTime());
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
    // 가변폭 폰트라 공백 패딩으로는 열이 안 맞는다. 표로 맞춘다.
    if (ImGui::BeginTable("cams", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("0x%llX", static_cast<unsigned long long>(row.addr));
        }
        ImGui::EndTable();
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

    ImGui::Separator();
    const auto fc = game::freecam_state();
    if (fc.deferred) {
        ImGui::TextDisabled("프리카메라: 보류 - 렌더가 읽는 값을 아직 못 찾았습니다");
    } else if (!fc.hooked) {
        ImGui::TextDisabled("프리카메라: 훅을 기다리는 중입니다");
    } else {
        ImGui::TextColored(fc.active ? col::kOk
                                     : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                           "프리카메라: %s", fc.active ? "켜짐" : "꺼짐");
        ImGui::Text("  갱신 함수 0x%llX",
                    static_cast<unsigned long long>(fc.update_fn));
        ImGui::Text("  변환 객체 0x%llX",
                    static_cast<unsigned long long>(fc.script));
        ImGui::Text("  좌표 %9.2f %9.2f %9.2f   속도 %.1f", fc.pos[0],
                    fc.pos[1], fc.pos[2], fc.speed);
        ImGui::TextDisabled("  넘패드 8/2=Z  4/6=X  9/3=Y  +/-=속도");
        ImGui::TextDisabled("  Shift=빠르게  Ctrl=느리게");
    }

    ImGui::End();
}

void shutdown_scan_panel() { game::stop_auto_analysis(); }

}  // namespace cdtb::render
