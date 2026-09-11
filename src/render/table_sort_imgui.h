#pragma once

#include <imgui.h>

#include "render/table_sort.h"

namespace cdtb::render {

// BeginTable 안(TableHeadersRow 뒤)에서 부른다. 사양이 바뀐 프레임에만 true 를
// 내고 SpecsDirty 를 내린다. 해제(세 번째 클릭)면 column = -1. tristate 표는
// DefaultSort 열이 없으면 처음 뜬 프레임에 SpecsCount == 0(정렬 없음)이고, 있으면
// 그 열로 정렬된 채 뜬다 - 기본 정렬을 원하는 열에만 DefaultSort 를 단다.
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
