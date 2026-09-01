#include "render/item_panel.h"

#include <imgui.h>

#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/item_view.h"
#include "render/icon_atlas.h"
#include "game/items.h"

namespace cdtb::render {
namespace {

// 거르기·정렬·쪽나누기는 game::item_view 가 한다. 여기는 그리기만
// 한다 - 경계 계산을 UI 안에 두면 화면으로만 확인하게 된다.

char g_query[128] = "";
// 기본으로 켜 둔다. 이름이 안 풀린 72개는 대개 개발용이라 목록에
// 있어도 쓸모가 없다. 필요하면 체크를 풀면 된다.
bool g_hide_unnamed = true;
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

constexpr float kIconSize = 22.0f;
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

// 분류 번호(+0xA3, 74종)의 이름.
//
// crimsondb.gg 의 카드 유형과 대조해 얻었다. 표본 1,379개를 아이템
// 표와 이름으로 맞춘 뒤, 분류값마다 어떤 유형이 걸리는지 셌다.
// 37종은 한 유형이 100% 를 차지했고, 몇 종은 여러 유형이 섞였다.
// 섞인 것은 묶어서 적는다 - 게임의 분류가 사이트보다 거칠어서다.
//
// **나머지 30종은 번호로 둔다.** 재료·소비·기타 카테고리는 사이트
// 카드에 유형 자체가 실려 있지 않아 대조할 것이 없다. 이름 표본으로
// 짐작할 수는 있으나(19=약초, 26=곤충, 48=조리법 ...) 추측으로 붙이면
// 조용히 틀린 표가 된다.
const char* category_name(std::uint8_t c) {
    switch (c) {
        // --- 방어구 ---
        case 3: return "갑옷·망토";      // 갑옷155 / 망토97 이 섞인다
        case 24: return "투구";
        case 22: return "장갑";
        case 9: return "신발";
        case 21: return "안경";
        case 30: return "복면";
        // --- 무기 ---
        case 5: return "한손도끼";
        case 10: return "활";
        case 11: return "석궁";
        case 12: return "단검";
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
        case 72: return "양손검";
        case 73: return "양손 워해머";
        case 18: return "한손 특수";     // 대포·부채·주먹·드릴·건틀렛
        case 70: return "양손창·파이크";
        case 71: return "방사기";
        // --- 방패 ---
        case 52: return "한손방패";
        case 60: return "대형방패";
        // --- 악세서리 ---
        case 15: return "귀걸이";
        case 34: return "목걸이";
        case 49: return "반지";
        // --- 탈것·펫 ---
        case 25: return "탈것 장비";     // 마갑·등자·안장·편자·마면
        case 14: return "드래곤갑옷";
        case 38: return "펫의상";
        case 39: return "펫투구";
        case 104: return "펫악세사리";
        // --- 소비·재료 (게임 안 항목을 직접 확인해 붙였다) ---
        case 0: return "탄환";          // 편전·화살·포탄·총탄
        case 16: return "낚시";         // 송사리·미꾸라지·참서대
        case 19: return "재료·요리";    // 약초·고기·곡물
        case 26: return "곤충";         // 잠자리·나비·거미
        case 28: return "투척품";       // 연막 폭탄·디코이 소환 볼
        case 41: return "비약";         // 성수·하급/중급 비약
        case 48: return "조리법·제작법";
        case 61: return "포장 교역품";  // 포장된 치즈·밀가루·양모
        case 62: return "교역품";       // 치즈·밀가루·소금·후추
        // --- 그 외 ---
        case 8: return "서적";          // 세계의 무기 일람·제작법
        case 27: return "열쇠";
        case 31: return "하우징";       // 평작·걸작·습작, 요리용 솥
        case 63: return "보물 지도";
        case 74: return "심연 장비";    // 파괴 I·간파 I. 개수 190 이 사이트와 같다
        case 102: return "어비스 장치"; // 전송 장치·유적 기둥·동력핵
        case 50: return "A.T.A.G.";
        case 54: return "가방·보금자리";
        // --- 남은 것도 내용을 보고 붙였다. 번호만 남으면 무엇인지
        //     알 수 없어 목록에서 쓸모가 없다. ---
        case 1: return "양서류";        // 독개구리·청개구리·두꺼비
        case 2: return "동물";          // 다람쥐·두더지·도마뱀·새
        case 4: return "어비스 아티팩트";
        case 7: return "기억";          // 망국의 기억
        case 13: return "어비스 효과";  // 어비스의 숨결·생명 증폭
        case 32: return "화폐·자원";    // 동화·캠프 자금·톱니
        case 35: return "묶음·주머니";  // 화살 묶음·동화 주머니
        case 36: return "편지·일지";
        case 37: return "허가증";       // 에르난드 통행 허가증
        case 42: return "의뢰서";       // 잃어버린 소·초대장
        case 43: return "기록";         // 대서고의 기록·관찰일지
        case 44: return "지역 열쇠";    // 저택·감옥·요새 열쇠
        case 45: return "게시물";       // 토벌 소식·목격담·경고문
        case 46: return "서신·장부";    // 영수증·보고서·증명서
        case 103: return "제작 부품";   // 동력핵·드릴 부품
        // --- 도구·기타 ---
        case 6: return "가방";
        case 17: return "낚싯대";
        case 57: return "랜턴";
        case 58: return "도구";          // 나팔·도끼·갈퀴 등
        case 59: return "횃불";
        case 55: return "분무기 등짐";
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

// 다음 항목이 창 오른쪽을 넘지 않으면 같은 줄에 이어 붙인다.
//
// ImGui 데모의 줄바꿈 관용구다. SameLine 을 무조건 걸면 창을 좁혔을 때
// 오른쪽이 잘려 나가고, 무조건 줄을 바꾸면 넓은 창에서 빈 줄이 남는다.
// 남은 폭을 보고 정한다.
void flow(float next_width) {
    const float right = ImGui::GetWindowPos().x +
                        ImGui::GetWindowContentRegionMax().x;
    const float end = ImGui::GetItemRectMax().x +
                      ImGui::GetStyle().ItemSpacing.x + next_width;
    if (end < right) ImGui::SameLine();
}

float text_w(const char* s) { return ImGui::CalcTextSize(s).x; }

// 라벨이 오른쪽에 붙는 위젯(Combo 등)이 실제로 차지하는 폭.
float labeled_w(float item_w, const char* label) {
    return item_w + ImGui::GetStyle().ItemInnerSpacing.x + text_w(label);
}

void draw_filter_bar() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float clear_w = text_w("지우기") + st.FramePadding.x * 2.0f;
    const float check_w = text_w("이름 없는 것 감추기") +
                          ImGui::GetFrameHeight() + st.ItemInnerSpacing.x;

    // 검색창은 남은 폭을 쓰되 상한을 둔다. 상한이 없으면 창을 넓혔을
    // 때 검색창만 늘어나 오른쪽 항목이 전부 밀려 잘린다.
    float query_w = ImGui::GetContentRegionAvail().x - clear_w -
                    st.ItemSpacing.x;
    if (query_w > 420.0f) query_w = 420.0f;
    if (query_w < 140.0f) query_w = 140.0f;
    ImGui::SetNextItemWidth(query_w);
    if (ImGui::InputTextWithHint("##query", "이름 또는 키로 검색", g_query,
                                 sizeof(g_query))) {
        g_dirty = true;
        g_page = 0;
    }

    flow(clear_w);
    if (ImGui::Button("지우기")) {
        g_query[0] = '\0';
        g_dirty = true;
        g_page = 0;
    }

    flow(check_w);
    if (ImGui::Checkbox("이름 없는 것 감추기", &g_hide_unnamed)) {
        g_dirty = true;
        g_page = 0;
    }

    // 라벨을 위젯 **앞**에 둔다. ImGui 기본은 뒤에 붙는데, 그러면
    // "전체 ▼ 등급" 처럼 읽혀 무엇을 고르는 칸인지 헷갈린다.
    const float grade_w = text_w("등급") + st.ItemInnerSpacing.x + 120.0f;
    const float cat_w = text_w("분류") + st.ItemInnerSpacing.x + 230.0f;

    flow(grade_w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("등급");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    // 목록이 길다. 잘리지 않도록 펼침 높이를 넉넉히 준다.
    if (ImGui::Combo("##grade", &g_grade_idx, kGradeLabel, 12)) {
        g_dirty = true;
        g_page = 0;
    }

    flow(cat_w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("분류");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230.0f);
    if (ImGui::Combo("##category", &g_category_idx,
                     g_category_labels.c_str(), 20)) {
        g_dirty = true;
        g_page = 0;
    }
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
    const float nav_w = btn * 2.0f + text_w(label) + 70.0f +
                        st.ItemSpacing.x * 3.0f;

    flow(text_w("쪽당") + st.ItemInnerSpacing.x + 70.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("쪽당");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::Combo("##perpage", &g_per_page_idx, kPerPageLabel)) g_page = 0;

    flow(nav_w);
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
    ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_FirstUseEver);
    // 너무 좁히면 표가 읽히지 않는다. 아래로는 못 내려가게 막는다.
    ImGui::SetNextWindowSizeConstraints(ImVec2(430.0f, 240.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
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
