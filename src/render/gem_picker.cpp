#include "render/gem_picker.h"

#include <imgui.h>

#include <string>

namespace cdtb::render {
namespace {

constexpr const char* kPopupId = "보석 고르기##gem_picker";

}  // namespace

void gem_picker_open(GemPicker* p) {
    p->open_requested = true;
    p->search[0] = '\0';
}

bool gem_picker_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out) {
    if (p->open_requested) {
        ImGui::OpenPopup(kPopupId);
        p->open_requested = false;
    }
    if (!ImGui::BeginPopup(kPopupId)) return false;

    bool chosen = false;
    ImGui::TextUnformatted(o.title);
    // 열리자마자 타자를 칠 수 있게 검색창에 포커스를 준다.
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##search", "이름으로 찾기", p->search,
                             sizeof(p->search));

    ImGui::BeginChild("list", ImVec2(320.0f, 280.0f));
    if (o.allow_empty) {
        if (ImGui::Selectable("(빈 칸으로 열기)", o.selected_key == 0)) {
            out->entry = nullptr;
            out->index = 0;
            chosen = true;
        }
        ImGui::Separator();
    }
    if (!chosen) {
        if (!game::items_ready()) {
            ImGui::TextDisabled("아이템 표를 아직 못 읽었습니다");
        } else {
            const auto& cat = game::item_catalog();
            int shown = 0;
            for (std::size_t i = 0; i < cat.size() && !chosen; ++i) {
                const auto& e = cat[i];
                if (!game::is_socket_gem(e)) continue;
                if (p->search[0] != '\0' &&
                    e.name.find(p->search) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(e.name.c_str(),
                                      e.key == o.selected_key)) {
                    out->entry = &e;
                    out->index = i;
                    chosen = true;
                }
                ImGui::PopID();
                // 190종이라 다 그려도 되지만, 표가 커지면 무거워진다.
                if (++shown >= 400) break;
            }
            if (shown == 0) ImGui::TextDisabled("맞는 보석이 없습니다");
        }
    }
    ImGui::EndChild();

    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return chosen;
}

}  // namespace cdtb::render
