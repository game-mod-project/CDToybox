#include "render/inventory_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "game/inventory.h"
#include "mem/reader.h"
#include "game/items.h"
#include "render/grant_panel.h"
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
    std::int64_t count = 0;
    std::uint32_t temper = 0;
    std::uint32_t sharpness = 0;
    std::uint32_t socket_count = 0;
    std::vector<std::uint32_t> gem_keys;
    game::InventoryRowText text;
};

std::vector<Row> g_rows;
std::string g_status = "아직 안 읽었습니다";
char g_filter[64]{};
int g_kind_idx = 0;                  // 0 = 전체
std::vector<std::uint32_t> g_kinds;

const game::ItemCatalogEntry* entry_of(std::uint32_t key) {
    if (key == 0 || !game::items_ready()) return nullptr;
    for (const auto& e : game::item_catalog()) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

void refresh(const mem::Reader& reader) {
    g_rows.clear();
    g_kinds.clear();

    if (!game::inventory_ready()) {
        g_status = "인벤토리 컴포넌트를 아직 못 찾았습니다";
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
        g_kinds.push_back(c.kind);

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
            r.text = game::format_inventory_row(rec.endurance, rec.sharpness,
                                                gems);
            g_rows.push_back(std::move(r));
        }
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "가방 %d개, 아이템 %zu개", containers,
                  g_rows.size());
    g_status = buf;
}

}  // namespace

void draw_inventory_panel() {
    ImGui::SetNextWindowSize(ImVec2(680.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("인벤토리")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("다시 읽기")) {
        const mem::LocalReader reader;
        refresh(reader);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("컴포넌트 다시 찾기")) {
        game::forget_inventory();
        g_rows.clear();
        g_status = "다시 찾는 중입니다 - 잠시 뒤 '다시 읽기'";
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_status.c_str());

    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##invfilter", "이름으로 찾기", g_filter,
                             sizeof(g_filter));

    // 가방 종류 고르기. 읽은 뒤에만 뜻이 있다.
    if (!g_kinds.empty()) {
        ImGui::SameLine();
        std::string labels = "전체";
        labels.push_back('\0');
        for (const auto k : g_kinds) {
            labels += "종류 " + std::to_string(k);
            labels.push_back('\0');
        }
        labels.push_back('\0');
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##invkind", &g_kind_idx, labels.c_str(), 12);
    }

    const std::uint32_t want_kind =
        (g_kind_idx > 0 && g_kind_idx <= static_cast<int>(g_kinds.size()))
            ? g_kinds[static_cast<std::size_t>(g_kind_idx - 1)]
            : 0xFFFFFFFFu;

    if (ImGui::BeginTable("inv", 7,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("개수", ImGuiTableColumnFlags_WidthFixed, 50.0f);
        ImGui::TableSetupColumn("담금질", ImGuiTableColumnFlags_WidthFixed,
                                50.0f);
        ImGui::TableSetupColumn("내구도", ImGuiTableColumnFlags_WidthFixed,
                                50.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed,
                                45.0f);
        ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < g_rows.size(); ++i) {
            const Row& r = g_rows[i];
            if (want_kind != 0xFFFFFFFFu && r.kind != want_kind) continue;
            if (g_filter[0] != 0 &&
                r.name.find(g_filter) == std::string::npos) {
                continue;
            }

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(grade_color(r.grade), "%s", r.name.c_str());
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
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled("제자리 수정은 게임이 되쓴다 - 값을 지급 칸으로"
                        " 옮겨 고친 뒤 새로 지급하고 원본은 버린다");
    ImGui::End();
}

}  // namespace cdtb::render
