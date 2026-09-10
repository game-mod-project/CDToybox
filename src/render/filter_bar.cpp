#include "render/filter_bar.h"

#include <imgui.h>

#include "render/item_style.h"

namespace cdtb::render {

bool draw_filter_bar(FilterBar* s, const FilterBarOpts& o) {
    bool changed = false;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float clear_w = text_width("지우기") + st.FramePadding.x * 2.0f;
    const float check_w = text_width("이름 없는 것 감추기") +
                          ImGui::GetFrameHeight() + st.ItemInnerSpacing.x;
    const float grade_w = text_width("등급") + st.ItemInnerSpacing.x + 120.0f;
    const float cat_w = text_width("분류") + st.ItemInnerSpacing.x + 230.0f;

    ImGui::PushID(o.id);

    // 검색창은 남은 폭을 쓰되 상한을 둔다. 상한이 없으면 창을 넓혔을
    // 때 검색창만 늘어나 오른쪽 항목이 전부 밀려 잘린다.
    float query_w = ImGui::GetContentRegionAvail().x - clear_w -
                    st.ItemSpacing.x;
    if (query_w > 420.0f) query_w = 420.0f;
    if (query_w < 140.0f) query_w = 140.0f;
    ImGui::SetNextItemWidth(query_w);
    if (ImGui::InputTextWithHint("##query", o.hint, s->query,
                                 sizeof(s->query))) {
        changed = true;
    }

    flow_same_line(clear_w);
    if (ImGui::Button("지우기")) {
        s->query[0] = '\0';
        changed = true;
    }

    if (o.show_hide_unnamed) {
        flow_same_line(check_w);
        if (ImGui::Checkbox("이름 없는 것 감추기", &s->hide_unnamed)) {
            changed = true;
        }
    }

    // 라벨을 위젯 **앞**에 둔다. ImGui 기본은 뒤에 붙는데, 그러면
    // "전체 ▼ 등급" 처럼 읽혀 무엇을 고르는 칸인지 헷갈린다.
    flow_same_line(grade_w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("등급");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    // 목록이 길다. 잘리지 않도록 펼침 높이를 넉넉히 준다.
    if (ImGui::Combo("##grade", &s->grade_idx, kGradeLabels, 12)) {
        changed = true;
    }

    // 분류는 표를 읽은 뒤에야 만들어진다.
    if (!s->category_labels.empty()) {
        flow_same_line(cat_w);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("분류");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230.0f);
        if (ImGui::Combo("##category", &s->category_idx,
                         s->category_labels.c_str(), 20)) {
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

game::ItemFilter to_filter(const FilterBar& s) {
    return game::make_filter(s.query, s.grade_idx, s.category_idx,
                             s.hide_unnamed, s.categories);
}

}  // namespace cdtb::render
