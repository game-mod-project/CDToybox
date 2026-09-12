#include "render/gem_picker.h"

#include <imgui.h>

#include <string>

#include "render/item_style.h"

namespace cdtb::render {
namespace {

constexpr const char* kPopupId = "보석 고르기##gem_picker";

}  // namespace

void gem_picker_reset(GemPicker* p) {
    p->search[0] = '\0';
    p->pick = -1;
}

void gem_picker_open(GemPicker* p) {
    p->open_requested = true;
    gem_picker_reset(p);
}

bool gem_list_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out) {
    bool chosen = false;
    ImGui::TextUnformatted(o.title);
    // 열리자마자 타자를 칠 수 있게 검색창에 포커스를 준다.
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##search", "이름으로 찾기", p->search,
                             sizeof(p->search));

    const game::ItemCatalogEntry* picked = nullptr;
    ImGui::BeginChild("list", ImVec2(320.0f, 280.0f));
    if (o.allow_empty) {
        if (ImGui::Selectable("(빈 칸으로 열기)", p->pick == -2)) p->pick = -2;
        ImGui::Separator();
    }
    if (!game::items_ready()) {
        ImGui::TextDisabled("아이템 표를 아직 못 읽었습니다");
    } else {
        const auto& cat = game::item_catalog();
        int shown = 0;
        for (std::size_t i = 0; i < cat.size(); ++i) {
            const auto& e = cat[i];
            if (!game::is_socket_gem(e)) continue;
            if (p->search[0] != '\0' &&
                e.name.find(p->search) == std::string::npos) {
                continue;
            }
            const bool selected = p->pick == static_cast<int>(i);
            if (selected) picked = &e;
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, grade_color(e.grade));
            const bool clicked = ImGui::Selectable(e.name.c_str(), selected);
            ImGui::PopStyleColor();
            if (clicked) {
                p->pick = static_cast<int>(i);
                picked = &e;
            }
            // 고른 것이 없으면 selected_key 가 0 이다 - 키 0 인 줄에
            // "(지금)" 이 붙는 것을 막는다.
            if (o.selected_key != 0 && e.key == o.selected_key) {
                ImGui::SameLine();
                ImGui::TextDisabled("(지금)");
            }
            ImGui::PopID();
            // 190종이라 다 그려도 되지만, 표가 커지면 무거워진다.
            if (++shown >= 400) break;
        }
        if (shown == 0) ImGui::TextDisabled("맞는 보석이 없습니다");
    }
    ImGui::EndChild();

    if (p->pick == -2) {
        ImGui::TextUnformatted("선택: (빈 칸)");
    } else if (picked != nullptr) {
        ImGui::Text("선택: %s", picked->name.c_str());
    } else {
        ImGui::TextDisabled("줄을 눌러 고르세요");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(p->pick == -1 || (p->pick >= 0 && picked == nullptr));
    if (ImGui::Button("적용")) {
        out->entry = picked;
        out->index = picked != nullptr ? static_cast<std::size_t>(p->pick) : 0;
        chosen = true;
    }
    ImGui::EndDisabled();
    return chosen;
}

bool gem_picker_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out) {
    if (p->open_requested) {
        ImGui::OpenPopup(kPopupId);
        p->open_requested = false;
    }
    if (!ImGui::BeginPopup(kPopupId)) return false;
    const bool chosen = gem_list_draw(p, o, out);
    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return chosen;
}

}  // namespace cdtb::render
