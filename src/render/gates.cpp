#include "render/gates.h"

#include <imgui.h>

#include "render/colors.h"

namespace cdtb::render {

bool loading_gate(bool ready, const char* what, std::size_t done,
                  std::size_t total) {
    if (ready) return true;
    char dots[5] = {0};
    const int nd = 1 + (static_cast<int>(ImGui::GetTime() * 3.0) % 3);
    for (int i = 0; i < nd; ++i) dots[i] = '.';
    if (total > 0) {
        ImGui::TextColored(col::kBusy, "%s 불러오는 중%s  (%zu / %zu)", what,
                           dots, done, total);
    } else {
        ImGui::TextColored(col::kBusy, "%s 읽는 중%s", what, dots);
    }
    ImGui::TextWrapped(
        "월드 진입 후 자동으로 채워집니다 (보통 5~10초, 상황에 따라 더 걸릴 수 "
        "있습니다). 이 표시가 사라지지 않고 계속 남아 있으면 로드 실패입니다.");
    return false;
}

bool table_empty_row(int text_column, const char* text, const char* action) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(text_column);
    ImGui::TextDisabled("%s", text);
    if (action == nullptr) return false;
    ImGui::SameLine();
    return ImGui::SmallButton(action);
}

}  // namespace cdtb::render
