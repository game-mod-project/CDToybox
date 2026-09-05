#include "render/equip_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <map>
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

}  // namespace

void draw_equip_panel(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(560.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("장비 소켓 · 연마", open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    if (ImGui::Button("다시 읽기")) {
        g_refine_edit.clear();
        game::equip_request_refresh();
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f),
                       "이미 열린 소켓만 채웁니다. 잠긴 소켓은 못 엽니다.");

    std::vector<game::WornPiece> pieces;
    if (!game::equip_snapshot(&pieces)) {
        ImGui::TextDisabled("월드에 들어가 장비를 착용하면 읽힙니다 (자동, 최대"
                            " 수십 초).");
        ImGui::End();
        return;
    }
    if (g_msg[0]) {
        ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "%s", g_msg);
    }

    constexpr ImGuiTableFlags kF = ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("worn", 3, kF)) {
        ImGui::TableSetupColumn("장비", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch);
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
                    ImGui::TextDisabled("%d: [잠김]", k);
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
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    draw_gem_popup(reader);
    ImGui::End();
}

}  // namespace cdtb::render
