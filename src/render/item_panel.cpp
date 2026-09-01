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
int g_grade_idx = 0;                         // 0=전체, 1=없음, 2..6=T1..T5
int g_category_idx = 0;                      // 0=전체, 그 뒤는 g_categories
game::ItemSort g_sort = game::ItemSort::Key;
bool g_ascending = true;
std::size_t g_page = 0;

std::vector<const game::ItemCatalogEntry*> g_view;
std::vector<std::uint8_t> g_categories;      // 표에 실제로 있는 분류 값
std::string g_category_labels;               // Combo 용 널 구분 문자열
bool g_dirty = true;
std::size_t g_built_from = 0;

constexpr std::size_t kPerPage[] = {20, 40, 60, 100};
constexpr const char* kPerPageLabel = "20\0" "40\0" "60\0" "100\0";
constexpr const char* kGradeLabel = "전체\0" "등급 없음\0" "T1\0" "T2\0" "T3\0"
                                    "T4\0" "T5\0";

std::size_t per_page() { return kPerPage[g_per_page_idx]; }

// 등급 색은 crimsondb.gg 의 배지 색을 그대로 쓴다. 게임 툴팁의
// 보라색은 등급이 아니라 '중요물품' 표시였다.
ImVec4 grade_color(std::uint8_t grade) {
    switch (grade) {
        case 1: return ImVec4(155 / 255.f, 155 / 255.f, 155 / 255.f, 1.f);
        case 2: return ImVec4(94 / 255.f, 170 / 255.f, 94 / 255.f, 1.f);
        case 3: return ImVec4(91 / 255.f, 141 / 255.f, 217 / 255.f, 1.f);
        case 4: return ImVec4(168 / 255.f, 85 / 255.f, 247 / 255.f, 1.f);
        case 5: return ImVec4(245 / 255.f, 158 / 255.f, 11 / 255.f, 1.f);
        default: return ImVec4(0.55f, 0.55f, 0.55f, 1.f);
    }
}

// 분류 번호의 이름.
//
// 무기 20종은 crimsondb.gg 의 유형과 대조해 확정했다 - 유형마다
// 표본 2~5개를 모아 +0xA3 값을 봤더니 전부 한 값으로 모였다.
// 갑옷·장갑·도구는 게임 툴팁으로 확인했다.
//
// 세 값은 사이트 유형 여럿이 한 값을 쓴다. 게임의 분류가 사이트보다
// 거칠기 때문이라 묶어서 적는다.
//
// **확인한 것만 이름을 붙인다.** 나머지 50여 종은 번호로 둔다 -
// 추측으로 붙이면 조용히 틀린 표가 된다.
const char* category_name(std::uint8_t c) {
    switch (c) {
        // 게임 툴팁으로 확인
        case 3: return "갑옷";
        case 22: return "장갑";
        case 58: return "도구";
        // 사이트 유형과 대조해 확정
        case 5: return "한손도끼";
        case 10: return "활";
        case 11: return "석궁";
        case 12: return "단검";
        case 18: return "한손 특수";      // 대포·드릴·건틀렛·부채·전기톱
        case 23: return "양손대포";
        case 29: return "한손둔기";
        case 33: return "장총";
        case 40: return "피스톨";
        case 47: return "레이피어";
        case 53: return "샷건";
        case 56: return "한손검";
        case 64: return "양손할버드";
        case 65: return "양손도끼";
        case 68: return "거대양손검";
        case 69: return "양손망치";
        case 70: return "양손창·파이크";
        case 71: return "방사기·침봉";
        case 72: return "양손검";
        case 73: return "양손 워해머";
        default: return nullptr;
    }
}

void rebuild_categories() {
    const auto& all = game::item_catalog();
    bool seen[256] = {};
    for (const auto& e : all) seen[e.category] = true;
    g_categories.clear();
    g_category_labels.clear();
    g_category_labels.append("전체").push_back('\0');
    for (int c = 0; c < 256; ++c) {
        if (!seen[c]) continue;
        g_categories.push_back(static_cast<std::uint8_t>(c));
        char buf[48];
        const char* nm = category_name(static_cast<std::uint8_t>(c));
        if (nm != nullptr) {
            std::snprintf(buf, sizeof(buf), "%d (%s)", c, nm);
        } else {
            std::snprintf(buf, sizeof(buf), "%d", c);
        }
        g_category_labels.append(buf).push_back('\0');
    }
    g_category_labels.push_back('\0');
}

void rebuild() {
    const auto& all = game::item_catalog();
    game::ItemFilter f;
    f.query = g_query;
    f.hide_unnamed = g_hide_unnamed;
    f.grade = (g_grade_idx == 0) ? -1 : g_grade_idx - 1;
    f.category = (g_category_idx == 0 ||
                  g_category_idx > static_cast<int>(g_categories.size()))
                     ? -1
                     : g_categories[g_category_idx - 1];
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

    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("등급", &g_grade_idx, kGradeLabel)) {
        g_dirty = true;
        g_page = 0;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    if (ImGui::Combo("분류", &g_category_idx, g_category_labels.c_str())) {
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
    if (ImGui::Button("<")) --g_page;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Text("%zu / %zu 쪽", g_page + 1, pages);

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
    ImGui::SameLine();
    ImGui::TextDisabled("쪽 이동");
}

void apply_sort_specs() {
    ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
    if (specs == nullptr || !specs->SpecsDirty || specs->SpecsCount == 0) {
        return;
    }
    const ImGuiTableColumnSortSpecs& s = specs->Specs[0];
    switch (s.ColumnIndex) {
        case 1: g_sort = game::ItemSort::Grade; break;
        case 2: g_sort = game::ItemSort::Category; break;
        case 3: g_sort = game::ItemSort::Name; break;
        default: g_sort = game::ItemSort::Key; break;
    }
    g_ascending = (s.SortDirection == ImGuiSortDirection_Ascending);
    g_dirty = true;
    specs->SpecsDirty = false;
}

}  // namespace

void draw_item_panel() {
    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);
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
    if (g_built_from != all.size()) {
        rebuild_categories();
        g_dirty = true;
    }
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
    if (ImGui::BeginTable("items", 4, kFlags)) {
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
            ImGui::TextColored(grade_color(e.grade), "%s",
                               game::grade_label(e.grade));

            ImGui::TableSetColumnIndex(2);
            if (const char* nm = category_name(e.category)) {
                ImGui::TextUnformatted(nm);
            } else {
                ImGui::TextDisabled("%u", e.category);
            }

            ImGui::TableSetColumnIndex(3);
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
