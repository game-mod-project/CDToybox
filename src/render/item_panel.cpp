#include "render/item_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/items.h"

namespace cdtb::render {
namespace {

char g_query[128] = "";
std::vector<const game::ItemCatalogEntry*> g_filtered;
bool g_dirty = true;
std::size_t g_built_from = 0;   // 목록을 걸러 담을 때의 원본 크기

// 목록은 준비된 뒤 다시 바뀌지 않으므로 포인터를 들고 있어도 된다.
void rebuild_filter() {
    const auto& all = game::item_catalog();
    g_filtered.clear();
    g_filtered.reserve(all.size());

    const std::size_t qlen = std::strlen(g_query);
    for (const auto& e : all) {
        if (qlen == 0) {
            g_filtered.push_back(&e);
            continue;
        }
        // ImGui 의 입력도, 게임의 이름도 UTF-8 이다. 그대로 비교하면
        // 된다 - probe 의 argv 와 달리 코드페이지 변환이 필요 없다.
        if (e.name.find(g_query) != std::string::npos) {
            g_filtered.push_back(&e);
            continue;
        }
        char key[16];
        std::snprintf(key, sizeof(key), "%u", e.key);
        if (std::strstr(key, g_query) != nullptr) g_filtered.push_back(&e);
    }
    g_built_from = all.size();
    g_dirty = false;
}

}  // namespace

void draw_item_panel() {
    ImGui::SetNextWindowSize(ImVec2(560, 440), ImGuiCond_FirstUseEver);
    ImGui::Begin("아이템 목록");

    if (!game::items_ready()) {
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1), "아이템 표를 읽는 중...");
        ImGui::TextWrapped(
            "게임이 아이템 표를 올리면 배경에서 자동으로 읽습니다. "
            "따로 하실 일은 없습니다.");
        ImGui::End();
        return;
    }

    const auto& all = game::item_catalog();
    if (g_built_from != all.size()) g_dirty = true;

    ImGui::SetNextItemWidth(-100.0f);
    if (ImGui::InputText("검색", g_query, sizeof(g_query))) g_dirty = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("지우기")) {
        g_query[0] = '\0';
        g_dirty = true;
    }
    if (g_dirty) rebuild_filter();

    ImGui::Text("%zu / %zu", g_filtered.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("(줄을 누르면 키가 클립보드로)");
    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY |
                                       ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("items", 2, kFlags)) {
        ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("이름");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // 6,810줄을 매 프레임 다 그리지 않는다. 보이는 줄만 그린다.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_filtered.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const auto& e = *g_filtered[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                char key[32];
                std::snprintf(key, sizeof(key), "%u", e.key);
                ImGui::PushID(i);
                if (ImGui::Selectable(key, false,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    ImGui::SetClipboardText(key);
                }
                ImGui::PopID();

                ImGui::TableSetColumnIndex(1);
                if (e.name.empty()) {
                    ImGui::TextDisabled("(이름 없음)");
                } else {
                    ImGui::TextUnformatted(e.name.c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace cdtb::render
