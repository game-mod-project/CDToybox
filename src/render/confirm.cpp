#include "render/confirm.h"

#include <cstdio>

#include "core/guard.h"
#include "render/colors.h"
#include "render/confirm_state.h"

namespace cdtb::render {
namespace {

ConfirmState g_confirm;   // 한 번에 하나만 무장하므로 전역 하나로 족하다

bool draw(const char* label, bool is_small, ImVec2 size) {
    if (!guard::is_safe_to_modify()) {
        ImGui::BeginDisabled();
        if (is_small) {
            ImGui::SmallButton(label);
        } else {
            ImGui::Button(label, size);
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("쓰기 기능이 잠겨 있습니다");
        }
        return false;
    }

    // ID 는 라벨 범위 안의 고정 이름이다. 라벨을 "###label" 로 두 번 넣던 방식은
    // 라벨이 79바이트를 넘으면 무장 전후의 잘린 자리가 달라 ID 가 바뀌었다.
    ImGui::PushID(label);
    const unsigned id = static_cast<unsigned>(ImGui::GetID("confirm"));
    const double now = ImGui::GetTime();
    const double left = confirm_left(g_confirm, id, now);

    // 무장 중이면 라벨만 바꾼다. ### 뒤가 위젯 ID 라 신원은 그대로다.
    char shown[256];
    if (left > 0.0) {
        std::snprintf(shown, sizeof(shown), "정말? (%.0f초)###confirm",
                      left < 1.0 ? 1.0 : left);
        ImGui::PushStyleColor(ImGuiCol_Text, col::kWarn);
    } else {
        std::snprintf(shown, sizeof(shown), "%.200s###confirm", label);
    }
    const bool clicked = is_small ? ImGui::SmallButton(shown)
                                  : ImGui::Button(shown, size);
    if (left > 0.0) ImGui::PopStyleColor();
    ImGui::PopID();

    return confirm_step(&g_confirm, id, clicked, now) == ConfirmStep::Fired;
}

}  // namespace

bool confirm_button(const char* label, ImVec2 size) {
    return draw(label, false, size);
}

bool confirm_small_button(const char* label) {
    return draw(label, true, ImVec2(0, 0));
}

}  // namespace cdtb::render
