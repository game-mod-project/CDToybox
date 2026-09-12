#include "render/gates.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiTable: 표시 순서·안쪽 클립

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

bool table_empty_row(const char* text, const char* action) {
    ImGui::TableNextRow();
    ImGuiTable* table = ImGui::GetCurrentTable();
    // 화면에 첫째로 보이는 열. 사용자가 열 순서를 바꿔 두면 0번 열이 첫째가 아니다.
    int col = 0;
    if (table != nullptr) {
        for (int order = 0; order < table->ColumnsCount; ++order) {
            const int idx = table->DisplayOrderToIndex[order];
            if (table->Columns[idx].IsEnabled) {
                col = idx;
                break;
            }
        }
    }
    ImGui::TableSetColumnIndex(col);
    // 열 폭에 잘리지 않게 표 안쪽 전체로 클립을 넓힌다 - 저장된 창 배치에서 열이
    // 좁으면 한 열 안에서는 문구와 [지우기] 가 잘려 보이지 않았다(화면 검증
    // 2026-09-11, 열 번호를 옮기는 것으로는 안 고쳐진다 - Codex 지적).
    if (table != nullptr) {
        ImGui::PushClipRect(table->InnerClipRect.Min, table->InnerClipRect.Max,
                            false);
    }
    ImGui::TextDisabled("%s", text);
    bool pressed = false;
    if (action != nullptr) {
        ImGui::SameLine();
        pressed = ImGui::SmallButton(action);
    }
    if (table != nullptr) ImGui::PopClipRect();
    return pressed;
}

}  // namespace cdtb::render
