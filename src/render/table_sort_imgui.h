#pragma once

#include <imgui.h>

#include "render/table_sort.h"

namespace cdtb::render {

// BeginTable 안(TableHeadersRow 뒤)에서 부른다. 사양이 바뀐 프레임에만 true 를
// 내고 SpecsDirty 를 내린다. 해제(세 번째 클릭)면 column = -1. 표가 처음 뜬
// 프레임에도 dirty 라 기본 정렬(첫 정렬 가능 열, 오름차순)이 들어온다.
inline bool table_sort_pull(SortSpec* out) {
    ImGuiTableSortSpecs* sp = ImGui::TableGetSortSpecs();
    if (sp == nullptr || !sp->SpecsDirty) return false;
    if (sp->SpecsCount == 0) {
        *out = SortSpec{};
    } else {
        out->column = sp->Specs[0].ColumnIndex;
        out->ascending = sp->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    }
    sp->SpecsDirty = false;
    return true;
}

}  // namespace cdtb::render
