#include "render/inventory_panel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "game/inventory.h"
#include "mem/reader.h"
#include "game/items.h"
#include "game/stash.h"
#include "render/grant_panel.h"
#include "render/stash_panel.h"
#include "render/item_style.h"

namespace cdtb::render {
namespace {

// 한 줄. 읽을 때 만들어 두고 그릴 때는 문자열만 쓴다 - 그리는
// 동안 게임 메모리를 다시 읽지 않는다.
struct Row {
    std::uint32_t kind = 0;          // 가방 종류
    std::uint32_t slot = 0;
    std::uint32_t key = 0;           // 0 이면 대응표에 없다
    std::string name;
    std::uint8_t grade = 0;
    std::uint8_t category = 0;
    std::int64_t count = 0;
    std::uint32_t temper = 0;
    std::uint32_t sharpness = 0;
    std::uint32_t socket_count = 0;
    std::uint32_t endurance = 0;     // 정렬용 원값. 0xFFFF 면 없는 것
    std::uint32_t max_endurance = 0xFFFF;   // 표의 값. 담기에 쓴다
    std::vector<std::uint32_t> gem_keys;
    game::InventoryRowText text;
};

std::vector<Row> g_rows;
std::string g_status = "아직 안 읽었습니다";

// 컴포넌트가 생기기를 기다리는 중인가. 생기는 순간 스스로 읽는다.
// 참으로 시작한다 - 창을 처음 열면 아무 버튼도 안 누르고 내용이
// 차 있어야 한다. 예전에는 빈 표에 "아직 안 읽었습니다" 만 떠서,
// 창이 고장 난 것인지 인벤토리가 빈 것인지 구별이 안 됐다.
bool g_waiting = true;

// 걸러 내기는 아이템 목록과 같은 모양이다 - 검색 · 등급 · 분류.
// 헬퍼는 item_style 에 함께 둔다. 한쪽만 고치면 두 창이 달라진다.
char g_query[64]{};
int g_grade_idx = 0;                 // 0 = 전체
int g_category_idx = 0;              // 0 = 전체
std::vector<std::uint8_t> g_categories;
std::string g_category_labels;
int g_containers = 0;

// 헤더를 눌러 정렬한다. 정렬은 그리기 전에 한 번만 하고, 그린
// 뒤에는 g_rows 를 건드리지 않는다.
int g_sort_col = -1;
bool g_sort_asc = true;

const game::ItemCatalogEntry* entry_of(std::uint32_t key) {
    if (key == 0 || !game::items_ready()) return nullptr;
    for (const auto& e : game::item_catalog()) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

void refresh(const mem::Reader& reader) {
    g_rows.clear();

    if (!game::inventory_ready()) {
        // 실패로 끝내지 않는다 - 찾으라고 알리고, 생기면 저절로 읽는다.
        game::request_inventory_rescan();
        g_waiting = true;
        g_status = "컴포넌트를 찾는 중입니다 - 찾으면 저절로 읽습니다";
        return;
    }
    if (!game::item_ids_ready()) {
        g_status = "아이템 대응표를 아직 못 읽었습니다";
        return;
    }

    const std::uintptr_t comp = game::inventory_component();
    std::vector<game::InventoryContainer> conts;
    if (!game::read_inventory_containers(reader, comp, &conts)) {
        g_status = "컨테이너 배열을 읽지 못했습니다";
        return;
    }

    // 순번 -> 키를 한 번만 만든다. 칸마다 표를 훑으면 6,810 x 500 이다.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> id_to_key;
    if (game::items_ready()) {
        id_to_key.reserve(game::item_catalog().size());
        for (const auto& e : game::item_catalog()) {
            const std::uint32_t id = game::item_id_for_key(e.key);
            if (id != game::kNoItemId) {
                id_to_key.emplace_back(id, e.key);
            }
        }
    }
    const auto key_for = [&](std::uint32_t index) -> std::uint32_t {
        for (const auto& p : id_to_key) {
            if (p.first == index) return p.second;
        }
        return 0;
    };

    int containers = 0;
    for (const auto& c : conts) {
        std::vector<game::InventoryRecord> recs;
        if (!game::read_inventory_records(reader, c, &recs)) continue;
        if (recs.empty()) continue;
        ++containers;

        for (const auto& rec : recs) {
            Row r;
            r.kind = c.kind;
            r.slot = rec.slot;
            r.key = key_for(rec.index);
            r.count = rec.count;
            r.temper = rec.temper;
            r.sharpness = rec.sharpness;
            r.socket_count = rec.socket_count;

            if (const auto* e = entry_of(r.key)) {
                r.name = e->name;
                r.grade = e->grade;
                r.category = e->category;
                r.max_endurance = e->max_endurance;
            }
            if (r.name.empty()) {
                char buf[48];
                std::snprintf(buf, sizeof(buf), "(순번 %u)", rec.index);
                r.name = buf;
            }

            std::vector<std::string> gems;
            std::vector<game::InventorySocket> socks;
            std::uint32_t filled = 0;
            if (game::read_inventory_sockets(reader, rec, &socks)) {
                for (const auto& s : socks) {
                    if (s.empty()) continue;
                    ++filled;
                    const std::uint32_t gk = key_for(s.index);
                    r.gem_keys.push_back(gk);
                    const auto* ge = entry_of(gk);
                    gems.push_back((ge != nullptr && !ge->name.empty())
                                       ? ge->name
                                       : std::string("?"));
                }
            }
            r.socket_count = filled;
            r.endurance = rec.endurance;
            r.text = game::format_inventory_row(rec.endurance, rec.sharpness,
                                                gems);
            g_rows.push_back(std::move(r));
        }
    }

    g_containers = containers;
    build_category_labels(&g_categories, &g_category_labels);

    char buf[128];
    std::snprintf(buf, sizeof(buf), "가방 %d개, 아이템 %zu개", containers,
                  g_rows.size());
    g_status = buf;
}

// ImGui 가 알려 준 정렬 상태를 g_rows 에 적용한다.
//
// 열마다 무엇으로 견주는지 다르다 - 이름 · 분류는 글자, 나머지는
// 숫자다. 내구도가 없는 아이템(0xFFFF)은 늘 뒤로 보낸다. 그대로
// 견주면 65535 라 가장 큰 값이 되어 목록 맨 앞에 몰린다.
void apply_sort() {
    ImGuiTableSortSpecs* spec = ImGui::TableGetSortSpecs();
    if (spec == nullptr || spec->SpecsCount == 0) return;
    const int col = spec->Specs[0].ColumnIndex;
    const bool asc = spec->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
    if (!spec->SpecsDirty && col == g_sort_col && asc == g_sort_asc) return;
    g_sort_col = col;
    g_sort_asc = asc;
    spec->SpecsDirty = false;

    const auto endu_key = [](const Row& r) -> long long {
        // 없는 것은 맨 뒤로.
        return (r.endurance == game::kNoEndurance)
                   ? 0x7FFFFFFFLL
                   : static_cast<long long>(r.endurance);
    };
    std::stable_sort(g_rows.begin(), g_rows.end(),
                     [&](const Row& a, const Row& b) {
                         int c = 0;
                         switch (col) {
                             case 0: c = a.name.compare(b.name); break;
                             case 1:
                                 c = (a.category < b.category)   ? -1
                                     : (a.category > b.category) ? 1
                                                                 : 0;
                                 break;
                             case 2:
                                 c = (a.count < b.count)   ? -1
                                     : (a.count > b.count) ? 1
                                                           : 0;
                                 break;
                             case 3:
                                 c = (a.temper < b.temper)   ? -1
                                     : (a.temper > b.temper) ? 1
                                                             : 0;
                                 break;
                             case 4: {
                                 const long long x = endu_key(a);
                                 const long long y = endu_key(b);
                                 c = (x < y) ? -1 : (x > y) ? 1 : 0;
                                 break;
                             }
                             case 5:
                                 c = (a.sharpness < b.sharpness)   ? -1
                                     : (a.sharpness > b.sharpness) ? 1
                                                                   : 0;
                                 break;
                             case 6:
                                 c = (a.socket_count < b.socket_count)   ? -1
                                     : (a.socket_count > b.socket_count) ? 1
                                                                         : 0;
                                 break;
                             default: return false;
                         }
                         return asc ? (c < 0) : (c > 0);
                     });
}

// 아이템 목록과 같은 줄 구성이다 - 검색 · 지우기 · 등급 · 분류.
// 폭 계산과 줄바꿈도 같은 헬퍼를 쓴다. 한쪽만 고치면 두 창이
// 서로 다르게 생긴다.
void draw_filter_bar() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float clear_w = text_width("지우기") + st.FramePadding.x * 2.0f;
    const float grade_w = text_width("등급") + st.ItemInnerSpacing.x + 120.0f;
    const float cat_w = text_width("분류") + st.ItemInnerSpacing.x + 230.0f;

    // 검색창은 남은 폭을 쓰되 상한을 둔다. 상한이 없으면 창을 넓혔을
    // 때 검색창만 늘어나 오른쪽 항목이 밀려 잘린다.
    float query_w = ImGui::GetContentRegionAvail().x - clear_w -
                    st.ItemSpacing.x;
    if (query_w > 420.0f) query_w = 420.0f;
    if (query_w < 140.0f) query_w = 140.0f;
    ImGui::SetNextItemWidth(query_w);
    ImGui::InputTextWithHint("##invquery", "이름으로 검색", g_query,
                             sizeof(g_query));

    flow_same_line(clear_w);
    if (ImGui::Button("지우기")) g_query[0] = 0;

    // 라벨을 위젯 앞에 둔다. ImGui 기본은 뒤에 붙는데 그러면
    // "전체 ▼ 등급" 처럼 읽혀 무엇을 고르는 칸인지 헷갈린다.
    flow_same_line(grade_w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("등급");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("##invgrade", &g_grade_idx, kGradeLabels, 12);

    // 분류는 읽은 뒤에야 만들어진다.
    if (!g_category_labels.empty()) {
        flow_same_line(cat_w);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("분류");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230.0f);
        ImGui::Combo("##invcategory", &g_category_idx,
                     g_category_labels.c_str(), 20);
    }
}

}  // namespace

void draw_inventory_panel(bool* open) {
    ImGui::SetNextWindowPos(ImVec2(400, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(680.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("인벤토리", open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("다시 읽기")) {
        const mem::LocalReader reader;
        refresh(reader);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("컴포넌트 다시 찾기")) {
        game::forget_inventory();   // 요청까지 함께 남는다
        g_rows.clear();
        g_waiting = true;
        g_status = "다시 찾는 중입니다 - 찾으면 저절로 읽습니다";
    }

    // 컴포넌트를 기다리는 중이면 생기는 순간 스스로 읽는다. 예전에는
    // 사람이 "10초쯤 뒤에 다시 읽기를 눌러" 야 했는데, 언제가 그때인지
    // 알 방법이 없었다.
    if (g_waiting && game::inventory_ready()) {
        g_waiting = false;
        const mem::LocalReader reader;
        refresh(reader);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_status.c_str());

    draw_filter_bar();

    const int want_grade = (g_grade_idx == 0) ? -1 : g_grade_idx - 1;
    const int want_cat =
        (g_category_idx == 0 ||
         g_category_idx > static_cast<int>(g_categories.size()))
            ? -1
            : static_cast<int>(g_categories[static_cast<std::size_t>(
                  g_category_idx - 1)]);

    // 표에 바깥 창 스크롤이 생기지 않도록 아래 안내문 두 줄만큼 높이를
    // 남기고, 표가 그 안에서 스스로 스크롤하게 한다. 필터·헤더는 위에,
    // 안내문은 아래에 늘 보이고 목록만 표 안에서 움직인다.
    const float inv_footer_h =
        ImGui::GetTextLineHeightWithSpacing() * 2.0f +
        ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginTable("inv", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_SortMulti,
                          ImVec2(0.0f, -inv_footer_h))) {
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("분류", ImGuiTableColumnFlags_WidthFixed,
                                120.0f);
        ImGui::TableSetupColumn("개수", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("담금질", ImGuiTableColumnFlags_WidthFixed,
                                55.0f);
        ImGui::TableSetupColumn("내구도", ImGuiTableColumnFlags_WidthFixed,
                                55.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed,
                                45.0f);
        ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed |
                                        ImGuiTableColumnFlags_NoSort,
                                190.0f);
        // 헤더 행을 고정한다 - 스크롤해도 열 이름이 위에 남는다.
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        apply_sort();

        for (std::size_t i = 0; i < g_rows.size(); ++i) {
            const Row& r = g_rows[i];
            if (want_grade >= 0 && r.grade != want_grade) continue;
            if (want_cat >= 0 && r.category != want_cat) continue;
            if (g_query[0] != 0 &&
                r.name.find(g_query) == std::string::npos) {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(grade_color(r.grade), "%s", r.name.c_str());
            ImGui::TableNextColumn();
            if (const char* nm = category_name(r.category)) {
                ImGui::TextUnformatted(nm);
            } else if (r.category != 0) {
                ImGui::TextDisabled("%u", r.category);
            }
            ImGui::TableNextColumn();
            ImGui::Text("%lld", static_cast<long long>(r.count));
            ImGui::TableNextColumn();
            if (r.temper != 0) ImGui::Text("%u", r.temper);
            ImGui::TableNextColumn();
            if (!r.text.endurance.empty()) {
                ImGui::TextUnformatted(r.text.endurance.c_str());
            }
            ImGui::TableNextColumn();
            if (!r.text.sharpness.empty()) {
                ImGui::TextUnformatted(r.text.sharpness.c_str());
            }
            ImGui::TableNextColumn();
            if (!r.text.sockets.empty()) {
                ImGui::TextUnformatted(r.text.sockets.c_str());
            }
            ImGui::TableNextColumn();
            // 제자리 수정은 게임이 되쓴다. 대신 값을 지급 칸에 채워
            // 주고, 고쳐서 새로 지급하게 한다.
            ImGui::BeginDisabled(r.key == 0);
            if (ImGui::SmallButton("지급 칸으로")) {
                set_grant_item(r.key, r.count, r.temper, r.sharpness,
                               r.gem_keys.data(),
                               static_cast<int>(r.gem_keys.size()));
            }
            ImGui::EndDisabled();

            // 보관함으로 담기. 어디에 담을지는 보관함에서 펼쳐 둔
            // 세트로 정한다 - 인벤토리 창에 세트 고르기를 또 두면
            // 두 곳이 어긋난다.
            ImGui::SameLine();
            const int set = stash_open_set();
            ImGui::BeginDisabled(r.key == 0 || set < 0);
            if (ImGui::SmallButton("보관함에")) {
                game::StashEntry e;
                e.key = r.key;
                e.count = r.count;
                e.temper = r.temper;
                e.sharpness = r.sharpness;
                // 내구도가 없는 아이템은 안 적는다 - 적어 봐야
                // 뜻이 없고, 꺼낼 때 최대치를 받게 두면 된다.
                if (r.max_endurance != 0xFFFF) e.endurance = r.endurance;
                for (std::size_t g = 0; g < r.gem_keys.size(); ++g) {
                    game::StashSocket ss;
                    ss.slot = static_cast<std::uint32_t>(g);
                    ss.key = r.gem_keys[g];
                    // 파일에 남길 6바이트도 지급 때와 같은 조립기로.
                    game::socket_bytes_for_key(ss.key, ss.raw);
                    e.sockets.push_back(ss);
                }
                stash_add_entry(set, e);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (stash_open_set() < 0) {
        ImGui::TextDisabled("보관함에 담으려면 보관함 창에서 세트를 먼저"
                            " 펼치세요");
    }
    ImGui::TextDisabled("제자리 수정은 게임이 되쓴다 - 값을 지급 칸으로"
                        " 옮겨 고친 뒤 새로 지급하고 원본은 버린다");
    ImGui::End();
}

}  // namespace cdtb::render
