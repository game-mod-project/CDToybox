#include "render/inventory_panel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "game/equip.h"
#include "game/inventory.h"
#include "mem/reader.h"
#include "game/items.h"
#include "game/item_view.h"
#include "game/stash.h"
#include "render/grant_panel.h"
#include "render/layout.h"
#include "render/overlay.h"
#include "render/stash_panel.h"
#include "render/item_style.h"
#include "render/filter_bar.h"

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

    // 제자리 언락에 쓴다. 레코드에 직접 쓰므로 주소가 필요하고,
    // 지금 몇 칸이 열려 있는지(레코드 +0x70) 알아야 버튼을 가린다.
    std::uintptr_t record = 0;
    std::uint32_t open_sockets = 0;
    std::uint32_t table_cap = 0;     // 아이템표의 상한(참고 표시용)
};

std::vector<Row> g_rows;
// g_rows 를 만들 때 쓴 카탈로그 판(포인터). 이름은 현지화 후 뒤늦게
// 채워지며 새 판으로 갈리는데, 개수는 그대로라 크기로는 못 가른다.
// 판이 바뀌면 자동으로 다시 읽어 '(순번 N)' 이 이름으로 채워진다.
const void* g_rows_cat_ptr = nullptr;
std::string g_status = "아직 안 읽었습니다";

// 컴포넌트가 생기기를 기다리는 중인가. 생기는 순간 스스로 읽는다.
// 참으로 시작한다 - 창을 처음 열면 아무 버튼도 안 누르고 내용이
// 차 있어야 한다. 예전에는 빈 표에 "아직 안 읽었습니다" 만 떠서,
// 창이 고장 난 것인지 인벤토리가 빈 것인지 구별이 안 됐다.
bool g_waiting = true;

// 걸러 내기는 아이템 목록과 같은 모양이다 - 검색 · 등급 · 분류. 같은
// 위젯(render/filter_bar)을 쓴다. '이름 없는 것 감추기' 는 이 창에 없다.
FilterBar g_bar;
// 이 창의 필터바 옵션. 이름만 본다 - 키 문자열까지 걸면 숫자를 쳤을 때
// 동작이 바뀐다. 힌트("이름으로 검색")와 match_key=false 가 한 쌍이다.
const FilterBarOpts g_opts = [] {
    FilterBarOpts o;
    o.id = "inv";
    o.hint = "이름으로 검색";
    o.match_key = false;
    return o;
}();
int g_containers = 0;

// 헤더를 눌러 정렬한다. 정렬은 그리기 전에 한 번만 하고, 그린
// 뒤에는 g_rows 를 건드리지 않는다.
int g_sort_col = -1;
bool g_sort_asc = true;

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

    // 순번->키, 키->엔트리를 해시맵으로 한 번만 만든다. 예전엔 칸마다
    // 선형탐색(6,810 x 500 x 2)이라 "다시 읽기" 가 느렸다. O(1) 조회로 바꾼다.
    std::unordered_map<std::uint32_t, std::uint32_t> id2key;
    std::unordered_map<std::uint32_t, const game::ItemCatalogEntry*> key2ent;
    if (game::items_ready()) {
        const auto& cat = game::item_catalog();
        id2key.reserve(cat.size());
        key2ent.reserve(cat.size());
        for (const auto& e : cat) {
            const std::uint32_t id = game::item_id_for_key(e.key);
            if (id != game::kNoItemId) id2key.emplace(id, e.key);
            key2ent.emplace(e.key, &e);
        }
    }
    const auto key_for = [&](std::uint32_t index) -> std::uint32_t {
        const auto it = id2key.find(index);
        return it != id2key.end() ? it->second : 0;
    };
    const auto ent_for =
        [&](std::uint32_t key) -> const game::ItemCatalogEntry* {
        const auto it = key2ent.find(key);
        return it != key2ent.end() ? it->second : nullptr;
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
            r.record = rec.address;
            r.open_sockets = rec.open_sockets;

            if (const auto* e = ent_for(r.key)) {
                r.name = e->name;
                r.grade = e->grade;
                r.category = e->category;
                r.max_endurance = e->max_endurance;
                r.table_cap = e->max_sockets;
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
                    const auto* ge = ent_for(gk);
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
    g_rows_cat_ptr = game::item_catalog().data();
    filter_bar_rebuild_categories(&g_bar);

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

}  // namespace

namespace {

// 마지막 결과를 화면에 남긴다. 표 6813개를 훑는 일이라 눌렀는지
// 아닌지가 안 보이면 사람이 두 번 누른다.
std::string g_cap_note;

// 부위 목록과 사람이 정한 칸 수. 목록은 카탈로그 판이 갈리면 다시 만든다.
std::vector<game::SocketPartInfo> g_parts;
std::vector<int> g_part_want;          // g_parts 와 같은 길이
const void* g_parts_cat_ptr = nullptr;
bool g_parts_only_socketed = true;     // 소켓이 원래 있는 부위만 보기

// 부위 이름. 분류 이름으로 대부분 갈리는데, 한 분류에 장비타입이 여럿이면
// (갑옷 3:5 · 망토 3:75 처럼) 이름만으로는 구분이 안 된다. 그때는 보기
// 아이템 이름을 함께 낸다 - 사람이 보고 무엇인지 알아야 한다.
std::string part_label(const game::SocketPartInfo& info, bool ambiguous) {
    const char* cat = category_name(info.part.category);
    std::string s = (cat != nullptr && cat[0] != 0) ? cat : "(분류 없음)";
    if (ambiguous) {
        s += " · ";
        s += info.sample.empty() ? "(이름 없음)" : info.sample;
    }
    return s;
}

void rebuild_parts() {
    g_parts = game::socket_parts();
    g_parts_cat_ptr = game::item_catalog().data();

    // 설정에 있던 값을 그대로 채운다. 없으면 0(=안 건드림).
    const auto saved = overlay::socket_cap_setting();
    g_part_want.assign(g_parts.size(), 0);
    for (std::size_t i = 0; i < g_parts.size(); ++i) {
        for (const auto& s : saved) {
            if (s.category == g_parts[i].part.category &&
                s.equip_type == g_parts[i].part.equip_type) {
                g_part_want[i] = s.want;
                break;
            }
        }
    }
    // 지금 걸려 있으면 그쪽이 사실이다.
    for (const auto& rule : game::socket_cap_rules()) {
        for (std::size_t i = 0; i < g_parts.size(); ++i) {
            if (g_parts[i].part == rule.part) {
                g_part_want[i] = static_cast<int>(rule.want);
                break;
            }
        }
    }
}

std::vector<game::SocketCapRule> rules_from_ui() {
    std::vector<game::SocketCapRule> out;
    for (std::size_t i = 0; i < g_parts.size(); ++i) {
        if (g_part_want[i] <= 0) continue;
        out.push_back(game::SocketCapRule{
            g_parts[i].part, static_cast<std::uint32_t>(g_part_want[i])});
    }
    return out;
}

// 소켓 상한 올리기.
//
// 아이템표(`ItemInfo+0x238`)가 **화면·사용 칸 수**를 정한다. 레코드의
// 열린 칸은 세이브에 남지만 표는 매 실행 exe 에서 다시 읽히므로, 표 상한을
// 넘긴 칸을 계속 보려면 세션마다 다시 걸어야 한다. 그래서 설정에 남기고
// 오버레이가 표가 올라온 뒤 자동으로 건다.
//
// 부위는 (분류 +0xA3, 장비타입 +0x42) 쌍이다 - 분류만으로는 갑옷과 망토가
// 안 갈린다. 근거: specs/2026-09-07-socket-grant-unlock-research.md 9·10절.
void draw_socket_cap() {
    if (!ImGui::CollapsingHeader("소켓 상한 (실험)")) return;
    ImGui::Indent();

    if (g_parts.empty() || g_parts_cat_ptr != game::item_catalog().data()) {
        rebuild_parts();
    }

    if (game::socket_cap_active()) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                           "걸려 있습니다 - 부위 %zu개",
                           game::socket_cap_rules().size());
    } else {
        ImGui::TextDisabled("안 걸려 있습니다 (아이템표 원래 값)");
    }

    ImGui::Checkbox("소켓이 원래 있는 부위만", &g_parts_only_socketed);
    ImGui::SameLine();
    if (ImGui::SmallButton("보이는 것 전부 5")) {
        for (std::size_t i = 0; i < g_parts.size(); ++i) {
            if (g_parts_only_socketed && g_parts[i].with_socket == 0) continue;
            g_part_want[i] = static_cast<int>(game::kSocketSlotMax);
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("전부 0")) {
        g_part_want.assign(g_parts.size(), 0);
    }

    // 분류가 겹치는 자리는 이름만으로 못 가린다. 미리 세어 둔다.
    auto ambiguous = [](std::uint8_t cat) {
        int n = 0;
        for (const auto& p : g_parts) {
            if (p.part.category == cat) ++n;
        }
        return n > 1;
    };

    const ImVec2 outer(0.0f, 220.0f);
    if (ImGui::BeginTable("socketcap", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          outer)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("부위", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("종수", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("소켓있음", ImGuiTableColumnFlags_WidthFixed,
                                62.0f);
        ImGui::TableSetupColumn("원래", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("설정", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < g_parts.size(); ++i) {
            const auto& info = g_parts[i];
            if (g_parts_only_socketed && info.with_socket == 0) continue;
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int>(i));

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(
                part_label(info, ambiguous(info.part.category)).c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", info.count);

            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            if (info.with_socket == 0) {
                ImGui::TextDisabled("0");
            } else {
                ImGui::Text("%zu", info.with_socket);
            }

            ImGui::TableSetColumnIndex(3);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%u", info.table_cap);

            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputInt("##want", &g_part_want[i], 1, 1);
            if (g_part_want[i] < 0) g_part_want[i] = 0;
            if (g_part_want[i] > static_cast<int>(game::kSocketSlotMax)) {
                g_part_want[i] = static_cast<int>(game::kSocketSlotMax);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("설정 0 = 그 부위는 안 건드립니다. 낮추지는 못합니다.");

    if (ImGui::Button("걸기", ImVec2(90.0f, 0.0f))) {
        const mem::LocalReader reader;
        const auto rules = rules_from_ui();
        const auto res = game::socket_cap_apply(reader, rules);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      res.ok ? "부위 %zu개 / 아이템 %d개를 올렸습니다"
                             : "걸지 못했습니다 (부위 %zu개, %d)",
                      rules.size(), res.changed);
        g_cap_note = buf;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!game::socket_cap_active());
    if (ImGui::Button("되돌리기", ImVec2(90.0f, 0.0f))) {
        const mem::LocalReader reader;
        const auto res = game::socket_cap_restore(reader);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d개 되돌렸습니다 (실패 %d)",
                      res.changed, res.skipped);
        g_cap_note = buf;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("설정에 저장", ImVec2(110.0f, 0.0f))) {
        std::vector<Config::SocketCapPart> save;
        for (std::size_t i = 0; i < g_parts.size(); ++i) {
            if (g_part_want[i] <= 0) continue;
            save.push_back(Config::SocketCapPart{
                static_cast<int>(g_parts[i].part.category),
                static_cast<int>(g_parts[i].part.equip_type), g_part_want[i]});
        }
        const bool ok = overlay::set_socket_cap_setting(save);
        g_cap_note = ok ? "설정에 저장했습니다 - 다음 실행부터 저절로 걸립니다"
                        : "설정을 저장하지 못했습니다";
    }

    if (!g_cap_note.empty()) ImGui::TextDisabled("%s", g_cap_note.c_str());

    ImGui::TextWrapped(
        "원래 소켓이 없는 부위(망토·귀걸이·목걸이·반지)에도 달 수 있습니다 "
        "- 레코드에 5칸 벡터가 이미 있어서, 표 상한만 올리면 생깁니다.");
    ImGui::TextWrapped(
        "늘어난 칸은 지급이나 장비 소켓 편집으로 채웁니다. 상한은 세이브에 안 "
        "남아 세션마다 다시 걸리지만, 이것이 정하는 것은 툴팁 목록뿐입니다 - "
        "스탯 계산은 아이템 자신의 소켓을 그대로 더하므로 모드를 빼도 효과는 "
        "그대로입니다.");

    ImGui::Unindent();
}

}  // namespace

void draw_inventory_panel(bool* open) {
    if (!begin_window(Win::Inventory, open)) {
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
    } else if (!g_waiting && game::inventory_ready() &&
               g_rows_cat_ptr != game::item_catalog().data()) {
        // 이름이 뒤늦게 풀려 카탈로그 판이 갈렸다 - 손 안 대도 다시 읽어
        // '(순번 N)' 을 이름으로 바꾼다.
        const mem::LocalReader reader;
        refresh(reader);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_status.c_str());

    // 컴포넌트·대응표·이름이 다 준비될 때까지 로딩을 보여 준다. 이름은
    // 현지화 후에야 채워지므로 그 전에는 '(순번 N)' 만 나온다. 진행
    // 단계를 명시해 느린 로드인지 멈춘 것인지 가릴 수 있게 한다.
    const bool fully_ready = game::inventory_ready() &&
                             game::item_ids_ready() && game::items_named();
    if (!fully_ready) {
        char dots[5] = {0};
        const int nd = 1 + (static_cast<int>(ImGui::GetTime() * 3.0) % 3);
        for (int i = 0; i < nd; ++i) dots[i] = '.';
        const char* what = !game::inventory_ready()
                               ? "인벤토리 컴포넌트를 찾는 중"
                               : !game::item_ids_ready()
                                     ? "아이템 대응표를 읽는 중"
                                     : "아이템 이름 불러오는 중";
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1), "%s%s", what, dots);
        ImGui::TextWrapped(
            "월드 진입 후 자동으로 채워집니다 (보통 5~10초, 상황에 따라 더 "
            "걸릴 수 있습니다). 이 표시가 사라지지 않고 계속 남아 있으면 "
            "로드 실패입니다.");
        ImGui::End();
        return;
    }

    draw_socket_cap();

    // 가방 확장은 되돌렸다. 등록 컨테이너 18개 전부에 무차별로 쓰면
    // 그중 임시 버퍼(게임이 유지하는 4개 평행 사본 중 2개)를 건드려
    // 게임이 크래시하고 지급 경로까지 손상됐다(2026-09-05 실측). CT 처럼
    // 어느 사본이 안전한지 구조로 식별한 뒤에 다시 붙인다.

    draw_filter_bar(&g_bar, g_opts);   // 매 프레임 거르므로 반환값은 안 쓴다
    const game::ItemFilter filter = to_filter(g_bar, g_opts);

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
            if (!game::passes(filter, r.name, r.grade, r.category, r.key)) {
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
                // 소켓은 옮기지 않는다 - 지급 경로로는 못 넣는다.
                set_grant_item(r.key, r.count, r.temper, r.sharpness);
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

            // 제자리 소켓 열기. 인벤토리 레코드는 그 자체가 authoritative
            // 라 단일 쓰기로 저장까지 살아남는다(both-realms 불필요) -
            // 실측 2026-09-08: 열린 칸 0개짜리 검을 5칸으로 열어 저장·
            // 재시작을 넘겼다. 5칸이 엔진 천장이다. 장비가 아닌 것에는
            // 안 낸다 - 소켓 벡터가 뜻이 없다.
            if (r.record != 0 && r.open_sockets < game::kSocketSlotMax &&
                (r.table_cap > 0 || r.open_sockets > 0)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("소켓 5칸")) {
                    const mem::LocalReader rd;
                    const int n = game::socket_unlock_record(
                        rd, r.record, static_cast<int>(game::kSocketSlotMax));
                    if (n > 0) {
                        refresh(rd);
                    } else {
                        g_status = "소켓을 열지 못했습니다";
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "잠긴 칸을 엽니다 (지금 %u칸).\n"
                        "박힌 보석은 그대로 둡니다.\n"
                        "표 상한이 %u 라 툴팁에는 그만큼만 보입니다"
                        " - '소켓 상한' 도 올리세요.",
                        r.open_sockets, r.table_cap);
                }
            }
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
