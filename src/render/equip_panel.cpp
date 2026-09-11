#include "render/equip_panel.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

#include "game/equip.h"
#include "game/items.h"
#include "mem/reader.h"
#include "render/confirm.h"
#include "render/gem_picker.h"
#include "render/layout.h"
#include "render/notice.h"

namespace cdtb::render {
namespace {

std::uint64_t g_gem_inst = 0;   // 보석을 박을 대상 아이템 인스턴스
int g_gem_k = -1;               // 그 아이템의 소켓 칸
char g_gem_title[96]{};         // 보석 팝업 제목 - 어느 장비의 몇 번 칸인지
GemPicker g_gem_picker;         // 보석 고르기 팝업 (지급 창과 같은 위젯)
Notice g_notice;
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
    GemPickerOpts o;   // 빈 칸에만 '채우기' 가 뜨므로 강조할 현재 보석은 없다
    o.title = g_gem_title;
    GemChoice c;
    if (!gem_picker_draw(&g_gem_picker, o, &c) || c.entry == nullptr) return;
    // 소켓에 박는 값은 그 보석의 순번(= 카탈로그 인덱스).
    const int w = game::eq_write_socket(reader, g_gem_inst, g_gem_k,
                                        static_cast<std::uint16_t>(c.index));
    if (w >= 2) {
        notice_set(&g_notice, NoticeLevel::Ok,
                   "소켓 {}에 '{}' 을 박았습니다 (클라·서버 모두). 벗었다 다시"
                   " 착용하면 화면에 반영됩니다.",
                   g_gem_k, c.entry->name);
    } else if (w == 1) {
        notice_set(&g_notice, NoticeLevel::Warn,
                   "소켓 {}에 '{}' 을 한쪽만 박았습니다 - 다시 시도하세요.",
                   g_gem_k, c.entry->name);
    } else {
        notice_set(&g_notice, NoticeLevel::Bad,
                   "쓰기 실패 (잠긴 소켓이거나 대상 없음).");
    }
    game::equip_refresh_pieces(reader);
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

    ImGui::TextUnformatted("zone 별 색입니다. 벗었다 다시 착용하면 화면에"
                           " 반영됩니다.");
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
            if (w > 0) {
                notice_set(&g_notice, NoticeLevel::Ok,
                           "zone {} 염색을 적용했습니다. 벗었다 다시 착용하면"
                           " 화면에 반영됩니다.",
                           d.zone);
            } else {
                notice_set(&g_notice, NoticeLevel::Bad, "염색 쓰기 실패.");
            }
            game::equip_refresh_pieces(reader);
        }
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

}  // namespace

void draw_equip_panel(bool* open) {
    if (!begin_window(Win::Equip, open)) {
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
    // 판정이 필요 없다(참고 모드는 인벤 전체라 gear 만 골랐다). 클라·서버
    // 모두 쓴다.
    if (confirm_button("전부 연마 10")) {
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
        if (done + part == 0) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "쓴 것이 없습니다 (대상 없음).");
        } else if (part > 0) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "{}개 연마 10, {}개는 한쪽만 적용됐습니다 - 다시"
                       " 시도하세요.",
                       done, part);
        } else {
            notice_set(&g_notice, NoticeLevel::Ok,
                       "{}개 연마 10. 벗었다 다시 착용하면 화면에 반영됩니다.",
                       done);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(착용 장비 전체를 +10 으로)");

    // 착용 장비 전부 5칸 개방. 이미 열린 칸과 박힌 보석은 안 건드린다.
    ImGui::SameLine();
    if (confirm_button("전부 소켓 5칸")) {
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
        if (done + part == 0) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "쓴 것이 없습니다 (대상 없음).");
        } else if (part > 0) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "{}개 소켓 5칸, {}개는 한쪽만 적용됐습니다 - 다시"
                       " 시도하세요.",
                       done, part);
        } else {
            notice_set(&g_notice, NoticeLevel::Ok,
                       "{}개 소켓 5칸. 벗었다 다시 착용하면 화면에 반영됩니다.",
                       done);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(잠긴 칸까지 연다)");

    notice_draw(g_notice);

    constexpr ImGuiTableFlags kF = ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("worn", 4, kF)) {
        ImGui::TableSetupColumn("장비", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed, 150.0f);
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
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputInt("##rf", &rf, 1, 1);
            if (rf < 0) rf = 0;
            if (rf > 2000) rf = 2000;
            ImGui::SameLine();
            if (ImGui::SmallButton("적용")) {
                const int wc = game::eq_write_refine(
                    reader, w.instance, static_cast<std::uint16_t>(rf));
                if (wc >= 2) {
                    notice_set(&g_notice, NoticeLevel::Ok,
                               "연마 {} 을 적용했습니다 (클라·서버 모두)."
                               " 벗었다 다시 착용하면 화면에 반영됩니다.",
                               rf);
                } else if (wc == 1) {
                    notice_set(&g_notice, NoticeLevel::Warn,
                               "연마 {} 을 한쪽만 적용했습니다 - 다시"
                               " 시도하세요.",
                               rf);
                } else {
                    notice_set(&g_notice, NoticeLevel::Bad, "연마 쓰기 실패.");
                }
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
                    if (confirm_small_button("열기")) {
                        const int n = game::eq_unlock_sockets(reader,
                                                              w.instance, k + 1);
                        if (n > 0) {
                            notice_set(&g_notice, NoticeLevel::Ok,
                                       "소켓 {}칸까지 열었습니다. 벗었다 다시"
                                       " 착용하면 화면에 반영됩니다.",
                                       k + 1);
                        } else {
                            notice_set(&g_notice, NoticeLevel::Bad,
                                       "열기 실패 (대상 없음).");
                        }
                        game::equip_refresh_pieces(reader);
                    }
                } else if (s.marker == 0xFFFF && s.gem != 0xFFFF) {
                    const char* gn = name_of_sunbeon(s.gem);
                    ImGui::Text("%d: %s", k, gn ? gn : "(보석)");
                    if (gn && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", gn);
                    ImGui::SameLine();
                    if (confirm_small_button("비우기")) {
                        const int wc = game::eq_write_socket(reader, w.instance,
                                                             k, 0xFFFF);
                        if (wc >= 1) {
                            notice_set(&g_notice, NoticeLevel::Ok,
                                       "소켓 {} 을 비웠습니다. 벗었다 다시"
                                       " 착용하면 화면에 반영됩니다.",
                                       k);
                        } else {
                            notice_set(&g_notice, NoticeLevel::Bad,
                                       "비우기 실패.");
                        }
                        game::equip_refresh_pieces(reader);
                    }
                } else {
                    ImGui::Text("%d:", k);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("채우기")) {
                        g_gem_inst = w.instance;
                        g_gem_k = k;
                        const char* nm = name_of_sunbeon(w.key);
                        std::snprintf(g_gem_title, sizeof(g_gem_title), "%s 소켓 %d",
                                      nm != nullptr ? nm : "(이름 없음)", k);
                        gem_picker_open(&g_gem_picker);
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
