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
#include "render/colors.h"
#include "render/confirm.h"
#include "render/gates.h"
#include "render/grant_panel.h"
#include "render/layout.h"
#include "render/notice.h"
#include "render/overlay.h"
#include "render/stash_panel.h"
#include "render/table_sort_imgui.h"
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
Notice g_notice;   // 마지막 동작 결과. g_status 는 상태(가방·개수)만

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

    // 순번->키를 해시맵으로 한 번만 만든다. 예전엔 칸마다
    // 선형탐색(6,810 x 500 x 2)이라 "다시 읽기" 가 느렸다. O(1) 조회로 바꾼다.
    // 키->엔트리는 game::item_by_key 가 같은 일을 한다.
    std::unordered_map<std::uint32_t, std::uint32_t> id2key;
    if (game::items_ready()) {
        const auto& cat = game::item_catalog();
        id2key.reserve(cat.size());
        for (const auto& e : cat) {
            const std::uint32_t id = game::item_id_for_key(e.key);
            if (id != game::kNoItemId) id2key.emplace(id, e.key);
        }
    }
    const auto key_for = [&](std::uint32_t index) -> std::uint32_t {
        const auto it = id2key.find(index);
        return it != id2key.end() ? it->second : 0;
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

            if (const auto* e = game::item_by_key(r.key)) {
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
                    const auto* ge = game::item_by_key(gk);
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

// 마지막 결과를 잠깐 남긴다(Notice 는 10초에 회색, 60초에 사라진다).
// 표 6813개를 훑는 일이라 눌렀는지 아닌지가 안 보이면 사람이 두 번 누른다.
Notice g_cap_notice;   // 걸기·되돌리기·저장 결과

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
// 가방·보관함 용량. **2026-09-05 에 되돌렸던 기능을 원인 규명 후 다시 붙인 것**
// 이다(inventory.h 의 긴 주석 참고). 그때 사고는 "임시 버퍼를 건드려서" 가 아니라
// ① 기본 슬롯을 한 칸(+0x1A)만 보고 유도해 가방에서 240(정답 50)이 나왔고
// ② 목표 기본값이 999 인데 하드 상한이 없었기 때문이다.
void draw_bag_expand() {
    if (!ImGui::CollapsingHeader("가방·보관함 용량")) return;

    // **종류마다 목표를 따로 둔다.** 예전에는 슬라이더 하나에 "보관함도 함께"
    // 체크였는데, 상한을 종류별로 빼 놓고 목표는 하나라 실제로는 종류별 조절이
    // 전혀 안 됐다(상한이 넷 다 700 이라 더 그랬다). 0 이면 그 종류는 안 건드린다.
    //
    // 기본값: 가방만 300, 나머지는 0. 이 기능은 게임 메모리에 값을 박는 기능이라
    // **기본값이 곧 안전 설계**다 - 전부 700 을 놓고 "낮게 해 보라" 고 적으면
    // 아무 의미가 없다.
    static int s_target[game::kBagKindCount] = {300, 0, 0, 0};
    // 확장을 어느 칸에 적을지. **기본은 종류별**이다 - 가방은 +0x18, 보관함류는
    // +0x1A 에 제 확장을 단다. 강제는 시험용으로만 둔다.
    static int s_branch = game::kBagBranchAuto;
    // 로드 뒤 자동 다시 적용. 게임이 인벤토리를 새로 만들기 때문에, 저장에 남든
    // 안 남든 이게 없으면 화면 숫자는 로드마다 원래대로 돌아간다.
    static bool s_auto = true;
    // 되돌리기에 **성공**한 적이 있나. 기록이 비는 이유가 셋이라 툴팁을 가른다.
    static bool s_restored = false;
    static std::string s_msg;

    const mem::LocalReader reader;
    const auto rules = game::bag_kind_rules();

    // 지금 용량을 옆에 낸다. 이게 없으면 슬라이더가 무엇을 기준으로 움직이는지
    // 알 수 없다(종류마다 기본 슬롯도 천장도 다르다).
    int cur[game::kBagKindCount] = {0, 0, 0, 0};
    game::bag_current_caps(reader, cur);

    for (std::size_t i = 0; i < rules.size() && i < game::kBagKindCount; ++i) {
        const auto& rule = rules[i];
        ImGui::SetNextItemWidth(200.0f);
        // 라벨이 곧 ImGui ID 다. 표의 이름이 서로 달라야 하고, 시험이 그걸 본다.
        ImGui::SliderInt(rule.name, &s_target[i], 0, rule.cap);
        ImGui::SameLine();
        if (s_target[i] <= 0) {
            ImGui::TextDisabled("안 건드림  (지금 %d · 상한 %d)", cur[i],
                                rule.cap);
        } else {
            ImGui::TextDisabled("지금 %d · 상한 %d", cur[i], rule.cap);
        }
    }
    ImGui::TextDisabled("0 으로 두면 그 종류는 건드리지 않습니다.");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "상한 700 은 참고 모드 둘이 쓰는 실전값이고, 732 초과는 엔진이\n"
            "깨진다고 둘 다 적습니다(그 상태로 저장하면 로드에서 죽습니다).\n"
            "보관함만의 상한은 상류 문서에도 실행 파일에도 없어, 같은 선을\n"
            "씁니다. 작은 칸(용량 5·10·20·50)은 어느 쪽이든 안 건드립니다.");
    }

    // 순서가 열거값과 같아야 한다(A=0, B=1, 자동=2).
    ImGui::SetNextItemWidth(220.0f);
    static const char* kBranchNames[] = {"A: +0x18 강제 (시험용)",
                                         "B: +0x1A 강제 (시험용)",
                                         "종류별 (권장)"};
    ImGui::Combo("확장 칸", &s_branch, kBranchNames, 3);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "확장을 어느 칸에 적을지입니다. 반대 칸은 건드리지 않습니다.\n"
            "\n"
            "종류마다 제 확장이 들어 있는 칸이 다릅니다 - 가방은 190 을\n"
            "+0x18 에, 보관함은 200 을 +0x1A 에 답니다(실측). 참고 모드\n"
            "소스도 +0x1A 를 보관함 세이브의 _varyExpandSlotCount 로 적고,\n"
            "그것이 '가방과는 다른 것' 이라고 못박습니다.\n"
            "강제 선택은 그 가설을 시험할 때만 쓰십시오.");
    }

    // 체크박스는 **의도**이고, 실제 무장은 적용을 눌러야 걸린다. 둘을 한 줄에 같이
    // 보여 주지 않으면 켜진 체크박스가 아무것도 보장하지 않는다.
    const bool armed = game::bag_auto_on();
    if (ImGui::Checkbox("로드 후 자동 다시 적용", &s_auto) && !s_auto) {
        game::bag_auto_clear();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "게임은 세이브를 불러올 때 인벤토리를 통째로 새로 만듭니다.\n"
            "그래서 저장에 남든 안 남든, 로드하면 화면 숫자가 원래대로\n"
            "돌아갑니다. 켜 두면 새 인벤토리가 잡힐 때 방금 누른 것과\n"
            "똑같은 설정으로 한 번 더 겁니다(스스로 값을 정하지는 않습니다).\n"
            "되돌리기를 누르면 함께 풀립니다.");
    }
    ImGui::SameLine();
    if (armed) {
        ImGui::TextColored(col::kWarn,
                           "무장됨 - 지금 용량은 모드가 다시 건 값일 수 있습니다");
    } else if (s_auto) {
        ImGui::TextDisabled("(적용을 눌러야 무장됩니다)");
    }

    if (ImGui::Button("적용")) {
        const auto r = game::bag_expand(reader, s_target, s_branch);
        s_msg = "바꾼 것 " + std::to_string(r.changed) + "개(" +
                std::to_string(r.realms) + " realm), 건너뜀 " +
                std::to_string(r.skip) + ", 실패 " + std::to_string(r.fail);
        if (r.changed > 0 && r.realms < 2) {
            // 한쪽 realm 에만 갔으면 화면이 안 바뀔 수 있다. 그걸 모르면 사용자가
            // 헛되이 다시 누른다(2026-09-05 "999 넣었는데 변경 안 보임" 이 이것으로
            // 설명된다, 리뷰 지적 4).
            s_msg += " - 한쪽 realm 에만 썼습니다(화면이 안 바뀔 수 있습니다)";
        }
        if (r.changed == 0 && r.skip > 0) s_msg += std::string(" - ") + r.last_skip;
        if (r.unknown > 0) {
            // 사실만 말한다. unknown 이 하나라도 있으면 **다 된 경우에도** "잠시 뒤
            // 다시" 가 붙던 것을 좁혔다(리뷰 B-3).
            s_msg += " - 컨테이너 " + std::to_string(r.unknown) +
                     "개는 모양을 못 알아봤습니다";
            if (r.changed == 0) {
                s_msg += " (아직 만들어지는 중이면 잠시 뒤 다시 눌러 보십시오)";
            }
        }
        if (r.changed > 0) s_restored = false;
        // **누른 사실 자체로 무장을 갱신한다.** 예전에는 바꾼 것이 있을 때만
        // 갱신해, 목표를 **내리고** 다시 누르면("이미 목표보다 크다" 로 건너뛴다)
        // 화면은 새 값을 보여 주는데 자동 재적용은 옛 값을 걸었다(검토 경미 4).
        if (s_auto) {
            game::bag_auto_set(s_target, s_branch);
        } else {
            game::bag_auto_clear();
        }
        refresh(reader);   // 용량 표시를 바로 새로 읽는다
    }
    ImGui::SameLine();
    // 한 프레임에 한 번만 읽는다. 예전에는 세 번 읽어, 되돌리기가 성공해 백업이
    // 비는 프레임에 BeginDisabled 없이 EndDisabled 만 불렸다(리뷰 지적 9).
    const bool has_backup = game::bag_has_backup();
    if (!has_backup) ImGui::BeginDisabled();
    if (ImGui::Button("되돌리기")) {
        const auto r = game::bag_restore(reader);
        s_msg = "되돌린 것 " + std::to_string(r.changed) + "개, 건너뜀 " +
                std::to_string(r.skip) + ", 실패 " + std::to_string(r.fail);
        if (r.skip > 0) s_msg += std::string(" - ") + r.last_skip;
        if (r.changed > 0) s_restored = true;
        refresh(reader);
    }
    if (!has_backup) ImGui::EndDisabled();
    if (!has_backup &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        // "확장한 적이 없습니다" 와 "기록이 사라졌습니다" 와 "되돌렸습니다" 는
        // 사용자에게 전혀 다른 말이다.
        ImGui::SetTooltip(
            game::bag_backup_dropped()
                ? "되돌릴 기록이 남아 있지 않습니다 - 인벤토리가 새로 생겨 옛"
                  " 컨테이너가 사라졌습니다."
            : s_restored ? "이미 되돌렸습니다."
                         : "이번 실행에서 확장한 적이 없습니다.");
    }

    // **합계 칸 정리 - 이제는 미용이다.** 새 산술은 +0x16 을 읽지도 쓰지도 않으므로
    // 낡은 합계가 기능을 막지 않는다. 다만 옛 코드가 세이브에 남긴 값이라(실측:
    // 게임 재시작을 넘어 464·650 이 살아 왔다) 치울 수단은 있어야 한다.
    // **경고색으로 내지 않는다** - 급한 일이 아니고, 급한 것처럼 보이면 사용자가
    // 필요도 없는 쓰기를 누른다(검토 중대 2).
    const auto broken = game::bag_broken_count(reader);
    if (broken.fixable > 0) {
        ImGui::SameLine();
        // **기록에서 원본을 아는 것만 바로 쓴다.** 기록이 없으면 모양만 보고
        // 미루어 짐작하는 것이라(우리가 어긋뜨린 합계인지, 우리가 모르는 제3의
        // 갈래인지 코드는 가릴 수 없다), 한 번 더 묻는다. 이 창에서 혈통 검사
        // 없이 쓰는 유일한 길이라 화면이 관문 노릇을 한다(검토 중대 1).
        const bool all_known = broken.from_record == broken.fixable;
        const bool go = all_known ? ImGui::Button("합계 정리")
                                  : confirm_small_button("합계 정리", false);
        if (go) {
            const auto r = game::bag_repair(reader);
            s_msg = "고친 것 " + std::to_string(r.changed) + "개, 실패 " +
                    std::to_string(r.fail);
            refresh(reader);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "확장 합계(+0x16)가 두 갈래의 합과 어긋난 컨테이너가 있습니다.\n"
                "예전 판이 그 칸에 쓴 값이 세이브에 남아 생깁니다.\n"
                "\n"
                "지금은 눌러도 되고 안 눌러도 됩니다 - 지금 판은 그 칸을\n"
                "읽지도 쓰지도 않아 기능에 아무 영향이 없습니다. 세이브를\n"
                "깔끔하게 두고 싶을 때만 쓰십시오.\n"
                "\n"
                "이번 실행에 적용한 기록이 있으면 그 원본 그대로 되돌립니다.\n"
                "기록이 없으면 모양만 보고 합계를 갈래 합으로 맞춥니다 - 그건\n"
                "짐작이라 한 번 더 묻습니다. 용량(+0x14)은 어느 쪽이든 절대\n"
                "건드리지 않습니다.");
        }
        ImGui::SameLine();
        if (all_known) {
            ImGui::TextDisabled("정리할 컨테이너 %d개 (기록 있음, 급하지 않습니다)",
                                broken.fixable);
        } else {
            ImGui::TextDisabled("정리할 컨테이너 %d개 (그중 기록 없음 %d,"
                                " 급하지 않습니다)",
                                broken.fixable,
                                broken.fixable - broken.from_record);
        }
    }
    if (broken.stuck > 0) {
        // 우리가 모양을 못 알아본 컨테이너다. 대개는 **아직 채워지는 중**이라
        // 잠시 뒤면 저절로 풀린다(예전 문구는 "게임을 껐다 켜야 한다" 였는데,
        // 그건 낡은 합계가 막던 시절의 조치라 지금은 틀린 지시다).
        ImGui::TextDisabled("컨테이너 %d개는 아직 모양을 못 알아봤습니다 - 대개"
                            " 만들어지는 중이니 잠시 뒤 다시 보십시오.",
                            broken.stuck);
    }

    if (!s_msg.empty()) ImGui::TextWrapped("%s", s_msg.c_str());

    ImGui::TextWrapped(
        "되돌리기는 이번 실행 동안에만 됩니다. 게임을 끄면 원래 값으로 돌아갈 수 "
        "없습니다 - 먼저 세이브 파일을 복사해 두십시오.");
    ImGui::TextWrapped(
        "가방은 어느 칸에 써도 저장에 남지 않는 것이 실측으로 확인됐습니다(게임이 "
        "로드에서 되돌립니다). 보관함은 아직 미확정입니다 - 확인하려면 적용·저장 "
        "뒤 게임을 완전히 껐다 켜고, '로드 후 자동 다시 적용' 을 끈 채로 보십시오.");
    ImGui::TextColored(col::kWarn,
                       "게임 안에서 불러오기로는 확인할 수 없습니다 - 자동 다시 "
                       "적용이 새 인벤토리에 값을 다시 걸어 놓기 때문입니다.");
}

void draw_socket_cap() {
    if (!ImGui::CollapsingHeader("소켓 상한")) return;
    ImGui::Indent();

    if (g_parts.empty() || g_parts_cat_ptr != game::item_catalog().data()) {
        rebuild_parts();
    }

    if (game::socket_cap_active()) {
        ImGui::TextColored(col::kOk, "걸려 있습니다 - 부위 %zu개",
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
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Sortable |
                              ImGuiTableFlags_SortTristate,
                          outer)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("부위", ImGuiTableColumnFlags_WidthStretch |
                                          ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("종수", ImGuiTableColumnFlags_WidthFixed, 46.0f);
        ImGui::TableSetupColumn("소켓있음", ImGuiTableColumnFlags_WidthFixed,
                                62.0f);
        ImGui::TableSetupColumn("원래", ImGuiTableColumnFlags_WidthFixed, 40.0f);
        ImGui::TableSetupColumn("설정",
                                ImGuiTableColumnFlags_WidthFixed |
                                    ImGuiTableColumnFlags_NoSort,
                                92.0f);
        ImGui::TableHeadersRow();

        static SortSpec sort;
        table_sort_pull(&sort);
        std::vector<int> order;
        order.reserve(g_parts.size());
        for (std::size_t i = 0; i < g_parts.size(); ++i) {
            if (g_parts_only_socketed && g_parts[i].with_socket == 0) continue;
            order.push_back(static_cast<int>(i));
        }
        sort_view(order, sort, [&ambiguous](int a, int b, int col) {
            const auto& x = g_parts[static_cast<std::size_t>(a)];
            const auto& y = g_parts[static_cast<std::size_t>(b)];
            switch (col) {
                case 0: return cmp3(part_label(x, ambiguous(x.part.category)),
                                    part_label(y, ambiguous(y.part.category)));
                case 1: return cmp3(static_cast<long long>(x.count),
                                    static_cast<long long>(y.count));
                case 2: return cmp3(static_cast<long long>(x.with_socket),
                                    static_cast<long long>(y.with_socket));
                case 3: return cmp3(static_cast<long long>(x.table_cap),
                                    static_cast<long long>(y.table_cap));
                default: return 0;
            }
        });
        for (int oi : order) {
            const std::size_t i = static_cast<std::size_t>(oi);
            const auto& info = g_parts[i];
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

    const auto rules = rules_from_ui();
    ImGui::BeginDisabled(rules.empty());
    if (ImGui::Button("걸기", ImVec2(90.0f, 0.0f))) {
        const mem::LocalReader reader;
        const auto res = game::socket_cap_apply(reader, rules);
        if (res.ok) {
            notice_set(&g_cap_notice, NoticeLevel::Ok,
                       "부위 {}개 / 아이템 {}개를 올렸습니다", rules.size(),
                       res.changed);
        } else {
            notice_set(&g_cap_notice, NoticeLevel::Bad,
                       "걸지 못했습니다 (부위 {}개, {})", rules.size(),
                       res.changed);
        }
    }
    ImGui::EndDisabled();
    if (rules.empty() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("설정이 전부 0 이라 걸 것이 없습니다");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!game::socket_cap_active());
    if (ImGui::Button("되돌리기", ImVec2(90.0f, 0.0f))) {
        const mem::LocalReader reader;
        const auto res = game::socket_cap_restore(reader);
        notice_set(&g_cap_notice,
                   res.ok ? NoticeLevel::Ok : NoticeLevel::Bad,
                   "{}개 되돌렸습니다 (실패 {})", res.changed, res.skipped);
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
        if (ok) {
            notice_set(&g_cap_notice, NoticeLevel::Ok,
                       "설정에 저장했습니다 - 다음 실행부터 저절로 걸립니다");
        } else {
            notice_set(&g_cap_notice, NoticeLevel::Bad,
                       "설정을 저장하지 못했습니다");
        }
    }

    notice_draw(g_cap_notice);

    ImGui::TextWrapped(
        "원래 소켓이 없는 부위(망토·귀걸이·목걸이·반지)에도 달 수 있습니다 "
        "- 레코드에 5칸 벡터가 이미 있어서, 표 상한만 올리면 생깁니다.");
    ImGui::TextWrapped(
        "이미 갖고 있는 아이템은 표 아래 줄의 '소켓 5칸' 으로 레코드도 함께 "
        "열어야 합니다.");
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
    notice_draw(g_notice);

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
        ImGui::TextColored(col::kBusy, "%s%s", what, dots);
        ImGui::TextWrapped(
            "월드 진입 후 자동으로 채워집니다 (보통 5~10초, 상황에 따라 더 "
            "걸릴 수 있습니다). 이 표시가 사라지지 않고 계속 남아 있으면 "
            "로드 실패입니다.");
        ImGui::End();
        return;
    }

    draw_socket_cap();

    draw_bag_expand();

    draw_filter_bar(&g_bar, g_opts);   // 매 프레임 거르므로 반환값은 안 쓴다
    const game::ItemFilter filter = to_filter(g_bar, g_opts);

    // 표에 바깥 창 스크롤이 생기지 않도록 아래 안내문 줄 수만큼 높이를
    // 남기고, 표가 그 안에서 스스로 스크롤하게 한다. 필터·헤더는 위에,
    // 안내문은 아래에 늘 보이고 목록만 표 안에서 움직인다. 보관함 세트를
    // 안 펼쳐 둔 동안은 안내가 한 줄 더 붙어 세 줄이다.
    const float inv_footer_h =
        ImGui::GetTextLineHeightWithSpacing() *
            (stash_open_set() < 0 ? 3.0f : 2.0f) +
        ImGui::GetStyle().ItemSpacing.y;
    // 표를 그리는 중에 g_rows 를 재구축하면 r 이 죽은 원소를 가리킨다 -
    // 표를 닫은 뒤 읽는다.
    bool need_refresh = false;
    if (ImGui::BeginTable("inv", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_Sortable,
                          ImVec2(0.0f, -inv_footer_h))) {
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch,
                                2.0f);
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
                                160.0f);
        // 헤더 행을 고정한다 - 스크롤해도 열 이름이 위에 남는다.
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        apply_sort();

        std::size_t shown = 0;
        for (std::size_t i = 0; i < g_rows.size(); ++i) {
            const Row& r = g_rows[i];
            if (!game::passes(filter, r.name, r.grade, r.category, r.key)) {
                continue;
            }
            ++shown;

            // ID 는 레코드 주소 - 3초 안에 정렬을 바꿔도 무장이 다른
            // 아이템으로 안 넘어간다. 레코드가 없는 줄(장비 아님)은 전부
            // 0 이라 겹치므로 그때만 행 번호로 떨어뜨린다.
            ImGui::PushID(r.record != 0
                              ? reinterpret_cast<const void*>(r.record)
                              : reinterpret_cast<const void*>(
                                    static_cast<std::uintptr_t>(0x10000 + i)));
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
            if (ImGui::SmallButton("지급")) {
                // 소켓은 옮기지 않는다 - 지급 경로로는 못 넣는다.
                set_grant_item(r.key, r.count, r.temper, r.sharpness);
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("지급 칸으로 옮깁니다 (소켓은 안 옮깁니다)");
            }

            // 보관함으로 담기. 어디에 담을지는 보관함에서 펼쳐 둔
            // 세트로 정한다 - 인벤토리 창에 세트 고르기를 또 두면
            // 두 곳이 어긋난다.
            ImGui::SameLine();
            const int set = stash_open_set();
            ImGui::BeginDisabled(r.key == 0 || set < 0);
            if (ImGui::SmallButton("보관")) {
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
                if (stash_add_entry(set, e)) {
                    notice_set(&g_notice, NoticeLevel::Ok, "'{}' 을 {} 에 담았습니다",
                               r.name, stash_open_set_name());
                } else {
                    notice_set(&g_notice, NoticeLevel::Bad,
                               "담지 못했습니다 (세트 번호 밖)");
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                if (set < 0) ImGui::SetTooltip("보관함 창에서 세트를 펼쳐 두면 거기에 담습니다");
                else ImGui::SetTooltip("보관함의 '%s' 에 담습니다",
                                       stash_open_set_name());
            }

            // 제자리 소켓 열기. 인벤토리 레코드는 그 자체가 authoritative
            // 라 단일 쓰기로 저장까지 살아남는다(both-realms 불필요) -
            // 실측 2026-09-08: 열린 칸 0개짜리 검을 5칸으로 열어 저장·
            // 재시작을 넘겼다. 5칸이 엔진 천장이다. 장비가 아닌 것에는
            // 안 낸다 - 소켓 벡터가 뜻이 없다.
            if (r.record != 0 && r.open_sockets < game::kSocketSlotMax &&
                (r.table_cap > 0 || r.open_sockets > 0)) {
                ImGui::SameLine();
                if (confirm_small_button("소켓 5칸")) {
                    const mem::LocalReader rd;
                    const int n = game::socket_unlock_record(
                        rd, r.record, static_cast<int>(game::kSocketSlotMax));
                    if (n > 0) {
                        notice_set(&g_notice, NoticeLevel::Ok,
                                   "소켓 {}칸을 열었습니다", n);
                        need_refresh = true;
                    } else {
                        notice_set(&g_notice, NoticeLevel::Bad,
                                   "소켓을 열지 못했습니다");
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "잠긴 칸을 엽니다 (지금 %u칸).\n"
                        "박힌 보석은 그대로 둡니다.\n"
                        "표 상한이 %u 라 툴팁에는 그만큼만 보입니다"
                        " - '소켓 상한' 도 올리세요.\n"
                        "게임 세이브에 남고 되돌릴 수 없습니다.",
                        r.open_sockets, r.table_cap);
                }
            }
            ImGui::PopID();
        }

        if (shown == 0) {
            const bool has_query = g_bar.query[0] != 0;
            // 문구는 열 폭에 매이지 않는다(table_empty_row 가 표 폭으로 클립을
            // 넓힌다) - 저장된 배치에서 이름 열이 좁아 잘렸던 것(화면 검증 2026-09-11).
            if (table_empty_row(
                                g_rows.empty() ? "인벤토리가 비어 있습니다"
                                : has_query    ? "검색어 때문에 비어 있습니다"
                                               : "걸러진 결과가 없습니다",
                                has_query ? "지우기" : nullptr)) {
                g_bar.query[0] = 0;
            }
        }
        ImGui::EndTable();
        if (need_refresh) {
            const mem::LocalReader rd2;
            refresh(rd2);
        }
    }

    if (stash_open_set() < 0) {
        ImGui::TextDisabled("보관함에 담으려면 보관함 창에서 세트를 먼저"
                            " 펼치세요");
    }
    ImGui::TextDisabled("개수·담금질·연마는 게임이 되쓰므로 지급 칸으로 옮겨 새로"
                        " 지급하세요.");
    ImGui::TextDisabled("소켓 열기만은 제자리로 되고 저장까지 남습니다.");
    ImGui::End();
}

}  // namespace cdtb::render
