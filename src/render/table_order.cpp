#include "render/table_order.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiTable: 설정 적재 · 순서 초기화 요청

namespace cdtb::render {

void table_keep_natural_order() {
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (table == nullptr) return;
    // 사용자가 옮길 수 있는 표는 옮긴 순서가 저장값이다 - 건드리지 않는다.
    if ((table->Flags & ImGuiTableFlags_Reorderable) != 0) return;
    // 설정을 읽는 프레임에만 건다. 요청은 같은 프레임의 TableUpdateLayout 에서 적재
    // (TableLoadSettingsForColumns) 뒤에 처리된다(imgui_tables.cpp TableApplyQueuedRequests).
    if (table->IsSettingsRequestLoad) table->IsResetDisplayOrderRequest = true;
}

}  // namespace cdtb::render
