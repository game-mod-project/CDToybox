#include "render/item_panel.h"

#include "render/grant_panel.h"
#include "render/item_style.h"
#include "render/stash_panel.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/item_view.h"
#include "render/filter_bar.h"
#include "render/gates.h"
#include "render/icon_atlas.h"
#include "game/items.h"

namespace cdtb::render {
namespace {

// 거르기·정렬·쪽나누기는 game::item_view 가 한다. 여기는 그리기만
// 한다 - 경계 계산을 UI 안에 두면 화면으로만 확인하게 된다.

// 검색·등급·분류 줄. 인벤토리 창과 같은 위젯(render/filter_bar)을 쓴다.
// '이름 없는 것 감추기' 는 기본으로 켜 둔다. 이름이 안 풀린 72개는 대개
// 개발용이라 목록에 있어도 쓸모가 없다. 필요하면 체크를 풀면 된다.
FilterBar g_bar = [] { FilterBar b; b.hide_unnamed = true; return b; }();
// 이 창의 필터바 옵션. 힌트("이름 또는 키로 검색")와 match_key 가 한 쌍이다.
const FilterBarOpts g_opts = [] {
    FilterBarOpts o;
    o.id = "items";
    o.hint = "이름 또는 키로 검색";
    o.show_hide_unnamed = true;
    o.match_key = true;
    return o;
}();
int g_per_page_idx = 1;                      // 아래 표의 첨자
game::ItemSort g_sort = game::ItemSort::Key;
bool g_ascending = true;
std::size_t g_page = 0;

std::vector<const game::ItemCatalogEntry*> g_view;
bool g_dirty = true;
std::size_t g_built_from = 0;
// 카탈로그는 이름이 뒤늦게(현지화 후) 채워지며 새 판으로 갈린다.
// 이때 개수(6813)는 그대로라 크기만으로는 갱신을 놓친다 - 목록이
// '이름 없는 것 감추기' 로 0개에 갇힌다. 판의 실체(포인터)로 가른다.
const void* g_built_ptr = nullptr;

constexpr float kIconSize = 22.0f;
constexpr std::size_t kPerPage[] = {20, 40, 60, 100};
constexpr const char* kPerPageLabel = "20\0" "40\0" "60\0" "100\0";

std::size_t per_page() { return kPerPage[g_per_page_idx]; }

// 등급 색은 crimsondb.gg 의 배지 색을 그대로 쓴다. 게임 툴팁의
// 보라색은 등급이 아니라 '중요물품' 표시였다.

void rebuild_categories() { filter_bar_rebuild_categories(&g_bar); }

void rebuild() {
    const auto& all = game::item_catalog();
    g_view = game::filter_items(all, to_filter(g_bar, g_opts));
    game::sort_items(g_view, g_sort, g_ascending);
    g_built_from = all.size();
    g_built_ptr = all.data();
    g_dirty = false;
}

void draw_pager(std::size_t total) {
    const std::size_t pages = game::page_count(total, per_page());
    if (g_page >= pages) g_page = pages - 1;

    // 쪽 이동은 한 덩어리다. 중간에서 줄이 바뀌면 읽기 나쁘므로
    // 통째로 들어갈 자리가 있을 때만 같은 줄에 붙인다.
    char label[64];
    std::snprintf(label, sizeof(label), "%zu / %zu 쪽", g_page + 1, pages);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float btn = ImGui::GetFrameHeight();
    const float nav_w = btn * 2.0f + text_width(label) + 70.0f +
                        st.ItemSpacing.x * 3.0f;

    flow_same_line(text_width("쪽당") + st.ItemInnerSpacing.x + 70.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("쪽당");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::Combo("##perpage", &g_per_page_idx, kPerPageLabel)) g_page = 0;

    flow_same_line(nav_w);
    ImGui::BeginDisabled(g_page == 0);
    if (ImGui::Button("<")) --g_page;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextUnformatted(label);

    ImGui::SameLine();
    ImGui::BeginDisabled(g_page + 1 >= pages);
    if (ImGui::Button(">")) ++g_page;
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("쪽 번호를 넣고 Enter");
}

void apply_sort_specs() {
    ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || !specs->SpecsDirty || specs->SpecsCount == 0) {
        return;
    }
    const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
    // 0번은 별표 칸이다(정렬 없음). 나머지가 한 칸씩 밀렸다.
    switch (s.ColumnIndex) {
        case 2: g_sort = game::ItemSort::Grade; break;
        case 3: g_sort = game::ItemSort::Category; break;
        case 4: g_sort = game::ItemSort::Name; break;
        default: g_sort = game::ItemSort::Key; break;
    }
    g_ascending = (s.SortDirection == ImGuiSortDirection_Ascending);
    g_dirty = true;
    specs->SpecsDirty = false;
}

}  // namespace

void draw_item_panel(bool* open) {
    ImGui::SetNextWindowPos(ImVec2(400, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    // 너무 좁히면 표가 읽히지 않는다. 아래로는 못 내려가게 막는다.
    ImGui::SetNextWindowSizeConstraints(ImVec2(430.0f, 240.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("아이템 목록", open)) {
        ImGui::End();
        return;
    }

    // 이름이 다 채워질 때까지 로딩을 보여 준다. 이름은 현지화가 올라온
    // 뒤에야(월드 진입 후 보통 5~10초, 더 걸리기도) 채워지므로, 그 전에
    // 목록을 열면 비어 보인다. 진행 수(N/M)를 함께 내 느린 로드인지
    // 멈춘 것인지 사람이 가릴 수 있게 한다.
    if (!loading_gate(game::items_ready(), "아이템 표") ||
        !loading_gate(game::items_named(), "이름", game::items_named_count(),
                      game::items_total_count())) {
        ImGui::End();
        return;
    }

    const auto& all = game::item_catalog();
    if (g_built_from != all.size() ||
        g_built_ptr != static_cast<const void*>(all.data())) {
        rebuild_categories();
        g_dirty = true;
    }
    if (g_dirty) rebuild();

    if (draw_filter_bar(&g_bar, g_opts)) {
        g_dirty = true;
        g_page = 0;
    }
    draw_pager(g_view.size());

    ImGui::Text("%zu / %zu", g_view.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("(줄을 누르면 지급 칸과 클립보드로 · 머리글을 누르면 정렬)");
    ImGui::Separator();

    constexpr ImGuiTableFlags kFlags =
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable |
        ImGuiTableFlags_SortTristate;
    if (ImGui::BeginTable("items", 5, kFlags)) {
        // 별표는 첫 칸에 따로 둔다. 키 칸에 겹쳐 놓았더니 줄 전체를
        // 덮는 Selectable 이 클릭을 가져가 눌리지 않았다.
        ImGui::TableSetupColumn("★", ImGuiTableColumnFlags_WidthFixed |
                                         ImGuiTableColumnFlags_NoSort,
                                26.0f);
        ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_DefaultSort,
                                90.0f);
        ImGui::TableSetupColumn("등급", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("분류", ImGuiTableColumnFlags_WidthFixed,
                                120.0f);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        apply_sort_specs();
        if (g_dirty) rebuild();

        const auto r = game::page_range(g_view.size(), g_page, per_page());
        for (std::size_t i = r.begin; i < r.end; ++i) {
            const auto& e = *g_view[i];
            ImGui::TableNextRow();

            // 첫 칸: 별표. 줄 선택과 겹치지 않는 자리다.
            //
            // 별표와 줄 선택은 서로 다른 ID 범위에 둔다. 한 범위에
            // 같이 넣었더니 ImGui 가 ID 충돌을 경고했다.
            ImGui::TableSetColumnIndex(0);
            const bool fav = stash_is_favorite(e.key);
            ImGui::PushID(static_cast<int>(e.key));
            if (ImGui::SmallButton(fav ? "★" : "☆")) {
                stash_toggle_favorite(e.key);
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            char key[40];
            std::snprintf(key, sizeof(key), "%u##row%zu", e.key, i);
            // 줄 전체를 눌러 고를 수 있게 하되, 별표 위에서는 별표가
            // 이긴다 - AllowOverlap 이 그 뜻이다.
            if (ImGui::Selectable(key, false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap)) {
                char just_key[32];
                std::snprintf(just_key, sizeof(just_key), "%u", e.key);
                ImGui::SetClipboardText(just_key);
                set_grant_item_key(e.key);   // 지급 칸에도 넣는다
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(grade_color(e.grade), "%s",
                               game::grade_label(e.grade));

            ImGui::TableSetColumnIndex(3);
            if (const char* nm = category_name(e.category)) {
                ImGui::TextUnformatted(nm);
            } else {
                ImGui::TextDisabled("%u", e.category);
            }

            ImGui::TableSetColumnIndex(4);
            // 아이콘은 있으면 그리고 없으면 자리만 비운다. 줄 높이가
            // 들쭉날쭉하지 않도록 없을 때도 같은 크기를 차지시킨다.
            const IconRef ico = icon_for(e.key);
            if (ico.valid) {
                ImGui::Image(ico.tex, ImVec2(kIconSize, kIconSize), ico.uv0,
                             ico.uv1);
            } else {
                ImGui::Dummy(ImVec2(kIconSize, kIconSize));
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (e.name.empty()) {
                ImGui::TextDisabled("(이름 없음)");
            } else {
                ImGui::TextColored(grade_color(e.grade), "%s", e.name.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

}  // namespace cdtb::render
