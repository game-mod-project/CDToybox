#include "render/equip_panel.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

#include "game/equip.h"
#include "game/items.h"
#include "mem/reader.h"

namespace cdtb::render {
namespace {

std::uint64_t g_gem_inst = 0;   // 보석을 박을 대상 아이템 인스턴스
int g_gem_k = -1;               // 그 아이템의 소켓 칸
bool g_open_gem = false;
char g_gem_search[64]{};
char g_msg[160]{};
std::map<std::uint64_t, int> g_refine_edit;

// 염색 팝업 대상과 편집값. 편집값은 (인스턴스,rec)별로 유지한다 -
// 매 프레임 스냅샷으로 덮으면 드래그 중 값이 리셋된다.
std::uint64_t g_dye_inst = 0;
bool g_open_dye = false;
std::map<std::pair<std::uint64_t, int>, std::array<float, 3>> g_dye_edit;

// 순번(catalog 인덱스)으로 이름을 얻는다. 카탈로그는 순번 순서다.
const char* name_of_sunbeon(std::uint32_t sunbeon) {
    if (!game::items_ready()) return nullptr;
    const auto& cat = game::item_catalog();
    if (sunbeon >= cat.size()) return nullptr;
    const auto& e = cat[sunbeon];
    return e.name.empty() ? nullptr : e.name.c_str();
}

void draw_gem_popup(const mem::Reader& reader) {
    if (g_open_gem) {
        ImGui::OpenPopup("보석 고르기##equip");
        g_open_gem = false;
    }
    if (!ImGui::BeginPopup("보석 고르기##equip")) return;
    ImGui::TextUnformatted("소켓에 박을 강화 보석(분류 74)");
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##gs", "이름으로 찾기", g_gem_search,
                             sizeof(g_gem_search));
    ImGui::BeginChild("gl", ImVec2(320.0f, 280.0f));
    if (game::items_ready()) {
        const auto& cat = game::item_catalog();
        for (std::size_t i = 0; i < cat.size(); ++i) {
            const auto& e = cat[i];
            if (e.category != game::kSocketGemCategory || e.name.empty())
                continue;
            if (g_gem_search[0] != 0 &&
                e.name.find(g_gem_search) == std::string::npos) {
                continue;
            }
            char lbl[128];
            std::snprintf(lbl, sizeof(lbl), "%s##%zu", e.name.c_str(), i);
            if (ImGui::Selectable(lbl)) {
                // 소켓에 박는 값은 그 보석의 순번(= 카탈로그 인덱스).
                const int w = game::eq_write_socket(
                    reader, g_gem_inst, g_gem_k,
                    static_cast<std::uint16_t>(i));
                std::snprintf(g_msg, sizeof(g_msg),
                              w > 0 ? "소켓 %d에 '%s' 박음 (%d realm). RE-EQUIP"
                                      " 하면 보입니다."
                                    : "쓰기 실패 (잠긴 소켓이거나 대상 없음).",
                              g_gem_k, e.name.c_str(), w);
                game::equip_refresh_pieces(reader);
                ImGui::CloseCurrentPopup();
                break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

// 염색 고르기. 이 조각이 가진 zone 레코드마다 색을 바꾼다. zone 이
// 정체성이라 행 번호가 아니라 zone 을 그대로 보여 준다. 기존 레코드에만
// 쓴다 - 없는 zone 은 새로 못 만든다(게임 소켓/염색 화면 필요). 0,0,0 은
// 검정이라 "미염색" 과 구분되지 않으므로 그런 판정은 하지 않는다.
void draw_dye_popup(const mem::Reader& reader,
                    const std::vector<game::WornPiece>& pieces) {
    if (g_open_dye) {
        ImGui::OpenPopup("염색 고르기##equip");
        g_open_dye = false;
    }
    if (!ImGui::BeginPopup("염색 고르기##equip")) return;

    const game::WornPiece* piece = nullptr;
    for (const auto& p : pieces) {
        if (p.instance == g_dye_inst) {
            piece = &p;
            break;
        }
    }
    if (piece == nullptr || piece->dyes.empty()) {
        ImGui::TextDisabled("이 조각엔 염색 레코드가 없습니다.");
        ImGui::EndPopup();
        return;
    }

    ImGui::TextUnformatted("zone 별 색. 재장착(RE-EQUIP) 해야 보입니다.");
    for (const auto& d : piece->dyes) {
        ImGui::PushID(d.rec);
        auto key = std::make_pair(g_dye_inst, d.rec);
        std::array<float, 3> init{d.r / 255.0f, d.g / 255.0f, d.b / 255.0f};
        float* col = g_dye_edit.try_emplace(key, init).first->second.data();
        ImGui::SetNextItemWidth(200.0f);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "zone %d", d.zone);
        ImGui::ColorEdit3(lbl, col,
                          ImGuiColorEditFlags_NoInputs |
                              ImGuiColorEditFlags_DisplayRGB);
        ImGui::SameLine();
        if (ImGui::SmallButton("적용")) {
            const auto rr = static_cast<std::uint8_t>(col[0] * 255.0f + 0.5f);
            const auto gg = static_cast<std::uint8_t>(col[1] * 255.0f + 0.5f);
            const auto bb = static_cast<std::uint8_t>(col[2] * 255.0f + 0.5f);
            const int w =
                game::eq_write_dye(reader, g_dye_inst, d.rec, rr, gg, bb);
            std::snprintf(g_msg, sizeof(g_msg),
                          w > 0 ? "zone %d 염색 적용 (%d realm). RE-EQUIP 하면"
                                  " 보입니다."
                                : "염색 쓰기 실패.",
                          d.zone, w);
            game::equip_refresh_pieces(reader);
        }
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

}  // namespace

void draw_equip_panel(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("장비 소켓 · 연마 · 염색", open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    if (ImGui::Button("다시 읽기")) {
        g_refine_edit.clear();
        g_dye_edit.clear();
        game::equip_request_refresh();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("잠긴 칸은 '열기' 로 엽니다. 툴팁에 다 보이려면"
                        " 인벤토리 창의 '소켓 상한' 도 올려야 합니다.");

    std::vector<game::WornPiece> pieces;
    if (!game::equip_snapshot(&pieces)) {
        ImGui::TextDisabled("월드에 들어가 장비를 착용하면 읽힙니다 (자동, 최대"
                            " 수십 초).");
        ImGui::End();
        return;
    }

    // 착용 장비 전부 최대 연마(10). 착용 목록은 전부 장비라 연마 불가
    // 판정이 필요 없다(참고 모드는 인벤 전체라 gear 만 골랐다). both-realms.
    if (ImGui::Button("전부 연마 10")) {
        int done = 0, part = 0;
        for (const auto& w : pieces) {
            const int wc = game::eq_write_refine(reader, w.instance, 10);
            if (wc >= 2) {
                ++done;
            } else if (wc == 1) {
                ++part;
            }
        }
        g_refine_edit.clear();
        game::equip_refresh_pieces(reader);
        std::snprintf(g_msg, sizeof(g_msg),
                      "%d개 both-realms, %d개 한쪽만 연마 10. RE-EQUIP 하면"
                      " 보입니다.",
                      done, part);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(착용 장비 전체를 +10 으로)");

    // 착용 장비 전부 5칸 개방. 이미 열린 칸과 박힌 보석은 안 건드린다.
    ImGui::SameLine();
    if (ImGui::Button("전부 소켓 5칸")) {
        int done = 0, part = 0;
        for (const auto& w : pieces) {
            const int wc = game::eq_unlock_sockets(reader, w.instance, 5);
            if (wc >= 2) {
                ++done;
            } else if (wc == 1) {
                ++part;
            }
        }
        game::equip_refresh_pieces(reader);
        std::snprintf(g_msg, sizeof(g_msg),
                      "%d개 both-realms, %d개 한쪽만 소켓 5칸. RE-EQUIP 하면"
                      " 보입니다.",
                      done, part);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(잠긴 칸까지 연다)");

    if (g_msg[0]) {
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "%s", g_msg);
    }

    constexpr ImGuiTableFlags kF = ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("worn", 4, kF)) {
        ImGui::TableSetupColumn("장비", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("염색", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        int rowid = 0;
        for (const auto& w : pieces) {
            ImGui::PushID(rowid++);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            const char* nm = name_of_sunbeon(w.key);
            if (nm) {
                ImGui::TextUnformatted(nm);
            } else {
                ImGui::Text("(순번 %u)", w.key);
            }

            ImGui::TableNextColumn();
            // 편집값은 인스턴스별로 유지한다. 매 프레임 스냅샷으로 덮으면
            // 입력이 리셋돼 값이 안 바뀐다(연마가 안 먹던 원인).
            int& rf = g_refine_edit.try_emplace(w.instance, w.refine)
                          .first->second;
            ImGui::SetNextItemWidth(55.0f);
            ImGui::InputInt("##rf", &rf, 1, 1);
            if (rf < 0) rf = 0;
            if (rf > 2000) rf = 2000;
            ImGui::SameLine();
            if (ImGui::SmallButton("적용")) {
                const int wc = game::eq_write_refine(
                    reader, w.instance, static_cast<std::uint16_t>(rf));
                std::snprintf(g_msg, sizeof(g_msg),
                              wc > 0 ? "연마 %d 적용 (%d realm). RE-EQUIP 하면"
                                       " 보입니다."
                                     : "연마 쓰기 실패.",
                              rf, wc);
                game::equip_refresh_pieces(reader);
            }

            ImGui::TableNextColumn();
            for (int k = 0; k < 5; ++k) {
                const auto& s = w.sockets[k];
                ImGui::PushID(k);
                if (s.index == 0xFF) {
                    // 잠긴 칸도 열 수 있다(실측 2026-09-08). 게임의 지급
                    // 코드가 하는 것과 같은 두 줄 - 레코드 +0x70 과 칸[4].
                    // 앞칸이 잠겨 있으면 그 칸부터 순서대로 열린다.
                    ImGui::TextDisabled("%d:", k);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("열기")) {
                        const int n = game::eq_unlock_sockets(reader,
                                                              w.instance, k + 1);
                        std::snprintf(g_msg, sizeof(g_msg),
                                      n > 0 ? "소켓 %d칸까지 열었다 (%d realm)."
                                              " RE-EQUIP 하면 보입니다."
                                            : "열기 실패 (대상 없음).",
                                      k + 1, n);
                        game::equip_refresh_pieces(reader);
                    }
                } else if (s.marker == 0xFFFF && s.gem != 0xFFFF) {
                    const char* gn = name_of_sunbeon(s.gem);
                    ImGui::Text("%d: %s", k, gn ? gn : "(보석)");
                    if (gn && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gn);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("비우기")) {
                        game::eq_write_socket(reader, w.instance, k, 0xFFFF);
                        std::snprintf(g_msg, sizeof(g_msg),
                                      "소켓 %d 비움. RE-EQUIP.", k);
                        game::equip_refresh_pieces(reader);
                    }
                } else {
                    ImGui::Text("%d:", k);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("채우기")) {
                        g_gem_inst = w.instance;
                        g_gem_k = k;
                        g_gem_search[0] = 0;
                        g_open_gem = true;
                    }
                }
                ImGui::PopID();
            }

            ImGui::TableNextColumn();
            if (w.dyes.empty()) {
                ImGui::TextDisabled("없음");
            } else {
                char db[24];
                std::snprintf(db, sizeof(db), "%d개##dye",
                              static_cast<int>(w.dyes.size()));
                if (ImGui::SmallButton(db)) {
                    g_dye_inst = w.instance;
                    g_open_dye = true;
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    draw_gem_popup(reader);
    draw_dye_popup(reader, pieces);
    ImGui::End();
}

}  // namespace cdtb::render
