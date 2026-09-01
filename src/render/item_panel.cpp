#include "render/item_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/item_view.h"
#include "game/items.h"

namespace cdtb::render {
namespace {

// 거르기·정렬·쪽나누기는 game::item_view 가 한다. 여기는 그리기만
// 한다 - 경계 계산을 UI 안에 두면 화면으로만 확인하게 된다.

char g_query[128] = "";
bool g_hide_unnamed = false;
int g_per_page_idx = 1;                      // 아래 표의 첨자
game::ItemSort g_sort = game::ItemSort::Key;
bool g_ascending = true;
std::size_t g_page = 0;

std::vector<const game::ItemCatalogEntry*> g_view;
bool g_dirty = true;
std::size_t g_built_from = 0;

constexpr std::size_t kPerPage[] = {20, 40, 60, 100};
constexpr const char* kPerPageLabel = "20\0" "40\0" "60\0" "100\0";

std::size_t per_page() { return kPerPage[g_per_page_idx]; }

void rebuild() {
    const auto& all = game::item_catalog();
    game::ItemFilter f;
    f.query = g_query;
    f.hide_unnamed = g_hide_unnamed;
    g_view = game::filter_items(all, f);
    game::sort_items(g_view, g_sort, g_ascending);
    g_built_from = all.size();
    g_dirty = false;
}

void draw_filter_bar() {
    ImGui::SetNextItemWidth(-190.0f);
    if (ImGui::InputTextWithHint("##query", "이름 또는 키로 검색", g_query,
                                 sizeof(g_query))) {
        g_dirty = true;
        g_page = 0;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("지우기")) {
        g_query[0] = '\0';
        g_dirty = true;
        g_page = 0;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("이름 없는 것 감추기", &g_hide_unnamed)) {
        g_dirty = true;
        g_page = 0;
    }
}

void draw_pager(std::size_t total) {
    const std::size_t pages = game::page_count(total, per_page());
    if (g_page >= pages) g_page = pages - 1;

    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::Combo("쪽당", &g_per_page_idx, kPerPageLabel)) g_page = 0;

    ImGui::SameLine();
    ImGui::BeginDisabled(g_page == 0);
    if (ImGui::Button("◀")) --g_page;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("%zu / %zu 쪽", g_page + 1, pages);

    ImGui::SameLine();
    ImGui::BeginDisabled(g_page + 1 >= pages);
    if (ImGui::Button("▶")) ++g_page;
    ImGui::EndDisabled();

    ImGui::SameLine();
    int jump = static_cast<int>(g_page + 1);
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::InputInt("##jump", &jump, 0, 0,
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (jump < 1) jump = 1;
        if (static_cast<std::size_t>(jump) > pages) {
            jump = static_cast<int>(pages);
        }
        g_page = static_cast<std::size_t>(jump) - 1;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("쪽 이동");
}

// 표의 정렬 지정이 바뀌었으면 받아 둔다.
void apply_sort_specs() {
    ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || !specs->SpecsDirty || specs->SpecsCount == 0) {
        return;
    }
    const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
    switch (s.ColumnIndex) {
        case 1: g_sort = game::ItemSort::Name; break;
        case 2: g_sort = game::ItemSort::NameKey; break;
        default: g_sort = game::ItemSort::Key; break;
    }
    g_ascending = (s.SortDirection == ImGuiSortDirection_Ascending);
    g_dirty = true;
    specs->SpecsDirty = false;
}

}  // namespace

void draw_item_panel() {
    ImGui::SetNextWindowSize(ImVec2(640, 500), ImGuiCond_FirstUseEver);
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
    if (g_dirty) rebuild();

    draw_filter_bar();
    draw_pager(g_view.size());

    ImGui::Text("%zu / %zu", g_view.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("(줄을 누르면 키가 클립보드로 · 머리글을 누르면 정렬)");
    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_SortTristate;
    if (ImGui::BeginTable("items", 3, kFlags)) {
        ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_DefaultSort,
                                90.0f);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("이름키", ImGuiTableColumnFlags_WidthFixed,
                                160.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        apply_sort_specs();
        if (g_dirty) rebuild();

        const auto r = game::page_range(g_view.size(), g_page, per_page());
        for (std::size_t i = r.begin; i < r.end; ++i) {
            const auto& e = *g_view[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            char key[32];
            std::snprintf(key, sizeof(key), "%u", e.key);
            ImGui::PushID(static_cast<int>(i));
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

            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%llu",
                                static_cast<unsigned long long>(e.name_key));
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace cdtb::render
