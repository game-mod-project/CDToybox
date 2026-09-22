#include "render/equip_panel.h"

#include <windows.h>  // GetTickCount64

#include <imgui.h>

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "game/equip.h"
#include "game/equip_bag.h"
#include "game/items.h"
#include "game/roster.h"
#include "mem/reader.h"
#include "render/confirm.h"
#include "render/gem_picker.h"
#include "render/item_style.h"
#include "render/layout.h"
#include "render/level_edit.h"
#include "render/notice.h"
#include "render/overlay.h"
#include "render/table_order.h"
#include "render/table_sort_imgui.h"

namespace cdtb::render {
namespace {

std::uint64_t g_sock_inst = 0;   // 칸 팝업의 대상 장비 인스턴스
int g_sock_k = -1;               // 그 장비의 칸
bool g_sock_open_req = false;    // 다음 프레임에 팝업을 연다
GemPicker g_sock_picker;         // 칸 팝업 안의 보석 목록 상태
Notice g_notice;
// 담금질·연마 편집값. 인스턴스별로 유지한다 - 매 프레임 스냅샷으로 덮으면 입력이
// 리셋돼 값이 안 바뀐다. 다만 게임 값이 바뀐 프레임에는 다시 씨앗을 넣는다(level_edit.h).
std::map<std::uint64_t, LevelEdit> g_temper_edit;
std::map<std::uint64_t, LevelEdit> g_sharp_edit;
// 마지막으로 발견을 요청한 시각. 창이 열린 동안 20초마다 자동 요청하되, 콤보·"다시 읽기" 의
// 수동 요청도 이 시각을 갱신해 힙 스캔이 연이어 두 번 돌지 않게 한다(리뷰 R-4).
ULONGLONG g_refresh_ms = 0;
constexpr ULONGLONG kAutoRefreshMs = 20000;

void request_refresh_now() {
    g_refresh_ms = ::GetTickCount64();
    game::equip_request_refresh();
}

// 염색 팝업 대상과 편집값. 편집값은 (인스턴스,rec)별로 유지한다 -
// 매 프레임 스냅샷으로 덮으면 드래그 중 값이 리셋된다.
std::uint64_t g_dye_inst = 0;
bool g_open_dye = false;
std::map<std::pair<std::uint64_t, int>, std::array<float, 3>> g_dye_edit;

// 성공 문구의 "어디에 썼나". 가방에도 있는 장비는 가방 기록, 입은 장비는 클라·서버.
const char* written_where(const game::EqWriteResult& r) {
    return r.in_bag ? "가방 기록 포함" : "클라·서버 모두";
}

// 순번(catalog 인덱스)으로 이름을 얻는다. 카탈로그는 순번 순서다.
const char* name_of_sunbeon(std::uint32_t sunbeon) {
    if (!game::items_ready()) return nullptr;
    const auto& cat = game::item_catalog();
    if (sunbeon >= cat.size()) return nullptr;
    const auto& e = cat[sunbeon];
    return e.name.empty() ? nullptr : e.name.c_str();
}

// 소켓 칸 팝업. 표에는 칸마다 버튼 하나만 두고 열기·비우기·보석 고르기는
// 여기서 한다 - 한 줄에 열기·비우기·채우기가 다섯 칸씩 늘어서 창을 넓혀도
// 다 안 보였다. 문구는 1단계의 것 그대로다.
void draw_socket_popup(const mem::Reader& reader,
                       const std::vector<game::WornPiece>& pieces) {
    if (g_sock_open_req) {
        ImGui::OpenPopup("소켓##equip_socket");
        g_sock_open_req = false;
    }
    if (!ImGui::BeginPopup("소켓##equip_socket")) return;
    const game::WornPiece* w = nullptr;
    for (const auto& p : pieces) {
        if (p.instance == g_sock_inst) {
            w = &p;
            break;
        }
    }
    if (w == nullptr || g_sock_k < 0 || g_sock_k >= 5) {
        ImGui::TextDisabled("대상을 잃었습니다 - 장비를 바꿨거나 스냅샷이"
                            " 갱신됐습니다");
        ImGui::EndPopup();
        return;
    }
    // 확인 버튼의 무장 상태는 ImGui ID 로 키잉된다 - 대상(장비·칸)마다 ID 를
    // 밀어야 A 장비에서 무장한 3초가 B 장비로 넘어가지 않는다(1단계와 같은 격리).
    ImGui::PushID(reinterpret_cast<const void*>(
        static_cast<std::uintptr_t>(w->instance)));
    ImGui::PushID(g_sock_k);
    const char* nm = name_of_sunbeon(w->key);
    ImGui::Text("%s 소켓 %d", nm != nullptr ? nm : "(이름 없음)", g_sock_k);
    ImGui::Separator();
    const game::WornSocket& s = w->sockets[g_sock_k];
    if (s.locked()) {
        // 잠긴 칸도 열 수 있다(실측 2026-09-08). 앞 칸이 잠겨 있으면 그 칸부터
        // 순서대로 열린다.
        ImGui::TextDisabled("잠긴 칸입니다. 앞 칸부터 순서대로 열립니다.");
        if (confirm_button("이 칸 열기")) {
            const game::EqWriteResult r =
                game::eq_unlock_sockets(reader, w->instance, g_sock_k + 1);
            if (game::eq_verdict(r) != game::EqVerdict::None) {
                notice_set(&g_notice, NoticeLevel::Ok,
                           "소켓 {}칸까지 열었습니다. 벗었다 다시 착용하면"
                           " 화면에 반영됩니다.",
                           g_sock_k + 1);
            } else {
                notice_set(&g_notice, NoticeLevel::Bad,
                           "열기 실패 (대상 없음).");
            }
            game::equip_refresh_pieces(reader);
        }
    } else {
        if (s.filled()) {
            const char* gn = name_of_sunbeon(s.gem);
            ImGui::Text("지금: %s", gn != nullptr ? gn : "(보석)");
            ImGui::SameLine();
            if (confirm_small_button("비우기")) {
                const game::EqWriteResult r =
                    game::eq_write_socket(reader, w->instance, g_sock_k, 0xFFFF);
                if (game::eq_verdict(r) != game::EqVerdict::None) {
                    notice_set(&g_notice, NoticeLevel::Ok,
                               "소켓 {} 을 비웠습니다. 벗었다 다시 착용하면"
                               " 화면에 반영됩니다.",
                               g_sock_k);
                } else {
                    notice_set(&g_notice, NoticeLevel::Bad, "비우기 실패.");
                }
                game::equip_refresh_pieces(reader);
            }
        } else {
            ImGui::TextDisabled("빈 칸입니다. 아래에서 골라 적용하세요.");
        }
        GemPickerOpts o;
        o.title = "박을 보석";
        const auto& cat = game::item_catalog();
        o.selected_key = (s.filled() && s.gem < cat.size()) ? cat[s.gem].key : 0;
        GemChoice c;
        if (gem_list_draw(&g_sock_picker, o, &c) && c.entry != nullptr) {
            // 소켓에 박는 값은 그 보석의 순번(= 카탈로그 인덱스).
            const game::EqWriteResult r =
                game::eq_write_socket(reader, w->instance, g_sock_k,
                                      static_cast<std::uint16_t>(c.index));
            const game::EqVerdict vd = game::eq_verdict(r);
            if (vd == game::EqVerdict::All) {
                notice_set(&g_notice, NoticeLevel::Ok,
                           "소켓 {}에 '{}' 을 박았습니다 ({})."
                           " 벗었다 다시 착용하면 화면에 반영됩니다.",
                           g_sock_k, c.entry->name, written_where(r));
            } else if (vd == game::EqVerdict::Partial) {
                notice_set(&g_notice, NoticeLevel::Warn,
                           "소켓 {}에 '{}' 을 한쪽만 박았습니다 - 다시"
                           " 시도하세요.",
                           g_sock_k, c.entry->name);
            } else {
                notice_set(&g_notice, NoticeLevel::Bad,
                           "쓰기 실패 (잠긴 소켓이거나 대상 없음).");
            }
            game::equip_refresh_pieces(reader);
        }
    }
    notice_draw(g_notice);
    ImGui::PopID();
    ImGui::PopID();
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

    // 가방에 든 장비의 염색은 장비 표 사본에만 들어간다 - 가방 레코드의 염색 자리를 확인하지
    // 않아 거기에는 안 쓴다. 게임이 가방 값으로 되맞출 수 있음을 미리 말한다.
    if (piece->in_bag) {
        ImGui::TextDisabled("가방에 든 장비라 염색은 장비 표에만 들어갑니다 - 게임이 되돌릴 수 있습니다.");
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

// 캐릭터 선택을 바꾼다 - 발견을 당기고, ini 에 남기고, 편집 중이던 값은 버린다(다른
// 캐릭터의 장비에 옛 입력이 붙지 않게).
void select_character(std::uint16_t row) {
    game::equip_select_character(row);
    request_refresh_now();
    overlay::set_equip_character_setting(
        row == game::kEquipAutoCharacter ? -1 : static_cast<int>(row));
    g_temper_edit.clear();
    g_sharp_edit.clear();
    notice_set(&g_notice, NoticeLevel::Ok,
               "캐릭터를 바꿨습니다 - 목록이 곧 갱신됩니다 (최대 수 초).");
}

// 캐릭터 행의 표시명. 로스터가 아직 없으면 행 번호로.
std::string character_label(std::uint16_t row) {
    if (row == game::kEquipAutoCharacter) return "자동 (착용 조각 최다)";
    const game::RosterEntry* e = game::character_by_row(row);
    if (e != nullptr && !e->display().empty()) return e->display();
    return "행 " + std::to_string(row);
}

// 캐릭터 콤보. 월드에 있는 플레이어형(정신력 풀) 캐릭터만 후보다 - 클리프·웅카·데미안은
// 번갈아 조종하는데 창은 "조각 최다" 규칙으로 늘 클리프만 보였다(사용자 보고 2026-09-12).
// 조종 중인 캐릭터를 자동으로 따라가는 것은 세션 전역 사슬이 2850 에서 끊겨 아직 없다
// (roster.h main_character_row) - 고르는 것은 사람 몫이고 선택은 ini 에 남는다.
void draw_character_picker() {
    // 후보 목록은 발견 때만 채워진다(첫 성공 뒤엔 요청 때만 돈다) - 창이 열려 있는 동안
    // 20초마다 한 번 다시 훑어 합류·이탈을 따라간다(리뷰 E-4). 힙 스캔이라 더 자주는 안 한다.
    if (::GetTickCount64() - g_refresh_ms > kAutoRefreshMs) request_refresh_now();
    const std::vector<game::EquipCharacter> chars = game::equip_characters();
    const std::uint16_t want = game::equip_selected_character();
    const std::uint16_t cur = game::equip_current_character();
    const std::uint16_t resolved = game::equip_resolved_character();
    const std::string preview = character_label(want);
    ImGui::SetNextItemWidth(240.0f);
    if (ImGui::BeginCombo("캐릭터", preview.c_str())) {
        if (ImGui::Selectable("자동 (착용 조각 최다)",
                              want == game::kEquipAutoCharacter)) {
            select_character(game::kEquipAutoCharacter);
        }
        for (const auto& c : chars) {
            char lb[160];
            // 표시명은 80바이트까지만 - 라벨이 잘려도 ID 접미사 ##c 는 남아야 한다(리뷰 E-6).
            std::snprintf(lb, sizeof(lb), "%.80s (조각 %d)##c%u",
                          character_label(c.row).c_str(), c.pieces,
                          static_cast<unsigned>(c.row));
            if (ImGui::Selectable(lb, want == c.row)) select_character(c.row);
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const std::string shown =
        cur == game::kEquipAutoCharacter ? std::string("?") : character_label(cur);
    // 발견이 아직 이 선택을 소화하지 않았으면 "갱신 중" - 정상 전환에서 2초 넘게 "월드에
    // 없음" 을 띄우던 것(리뷰 E-2). 그동안 표는 옛 캐릭터 것이라 그것도 적는다.
    if (resolved != want) {
        ImGui::TextDisabled("(갱신 중… 지금 보이는 것은 %s의 장비)", shown.c_str());
    } else if (!game::equip_ready()) {
        // 아직 한 번도 못 찾았으면 "월드에 없음" 이 아니다(리뷰 R-2) - 아래 본문이 "월드에
        // 들어가 장비를 착용하면 읽힙니다" 를 낸다.
        ImGui::TextDisabled("(아직 착용 장비를 못 찾았습니다)");
    } else if (want != game::kEquipAutoCharacter && cur != want) {
        ImGui::TextDisabled("(고른 캐릭터가 월드에 없어 자동으로 보입니다: %s)",
                            shown.c_str());
    } else {
        ImGui::TextDisabled("표시 중: %s (치트 표시·낙사 판정도 이 캐릭터; 목록은 20초마다"
                            " 갱신)",
                            shown.c_str());
    }
}

// 담금질·연마 상한. 표에서 못 찾으면(표 미준비 또는 표 밖 순번) -1 - 칸은 "-", 일괄은 건너뜀
// (두 경로가 같게, 리뷰 E-5). 그 값이 없는 아이템이면 0. u16 밖은 자른다(리뷰 E-7).
int level_cap(const game::ItemCatalogEntry* e, bool temper) {
    if (e == nullptr) return -1;
    long long cap = temper ? static_cast<long long>(e->max_temper)
                           : static_cast<long long>(e->max_sharpness);
    if (cap < 0) cap = 0;
    if (cap > 0xFFFF) cap = 0xFFFF;
    return static_cast<int>(cap);
}

// 담금질·연마 칸 하나. cap 이 0 이면 그 값이 없는 아이템(재료 등), 음수면 표가 아직 없는
// 것이라 "-". 입력은 인스턴스별로 유지한다. 클라·서버 모두 쓴다.
void draw_level_cell(const mem::Reader& reader, const game::WornPiece& w,
                     bool temper, int cap, std::map<std::uint64_t, LevelEdit>& edits) {
    const char* what = temper ? "담금질" : "연마";
    if (cap <= 0) {
        ImGui::TextDisabled("-");
        if (ImGui::IsItemHovered()) {
            if (cap < 0) {
                ImGui::SetTooltip("아이템 표에서 상한을 못 찾았습니다 (표 준비 중이거나 표 밖 순번).");
            } else {
                ImGui::SetTooltip("이 아이템에는 %s 값이 없습니다.", what);
            }
        }
        return;
    }
    const int cur = temper ? w.temper : w.sharpness;
    // 게임 값이 바뀌었으면(우리 쓰기 · 내구도 감소 · 착용 변경) 칸도 그 값으로 돌아간다.
    LevelEdit& st = edits[w.instance];
    level_edit_sync(&st, cur);
    int& v = st.v;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt(temper ? "##tp" : "##sh", &v, 1, 10);
    if (v < 0) v = 0;
    if (v > cap) v = cap;
    ImGui::SameLine();
    ImGui::TextDisabled("/%d", cap);
    ImGui::SameLine();
    if (ImGui::SmallButton(temper ? "적용##tp" : "적용##sh")) {
        const std::uint16_t lv = static_cast<std::uint16_t>(v);
        const game::EqWriteResult r =
            temper ? game::eq_write_temper(reader, w.instance, lv)
                   : game::eq_write_sharpness(reader, w.instance, lv);
        const game::EqVerdict vd = game::eq_verdict(r);
        if (vd == game::EqVerdict::All) {
            notice_set(&g_notice, NoticeLevel::Ok,
                       "{} {} 을 적용했습니다 ({}). 벗었다 다시 착용하면"
                       " 화면에 반영됩니다.",
                       what, v, written_where(r));
        } else if (vd == game::EqVerdict::Partial) {
            notice_set(&g_notice, NoticeLevel::Warn,
                       "{} {} 을 한쪽만 적용했습니다 - 다시 시도하세요.", what, v);
        } else {
            notice_set(&g_notice, NoticeLevel::Bad, "{} 쓰기 실패.", what);
        }
        game::equip_refresh_pieces(reader);
    }
}

// 착용 장비 전부를 표의 상한까지(담금질 max_temper / 연마 max_sharpness). 상한 0 인
// 것(그 값이 없는 아이템)은 건너뛴다. 클라·서버 모두 쓰고, 가방 장비는 가방 레코드에도 쓴다.
void bulk_write(const mem::Reader& reader, const std::vector<game::WornPiece>& pieces,
                bool temper) {
    const char* what = temper ? "담금질" : "연마";
    const auto& cat = game::item_catalog();
    int done = 0, part = 0, skipped = 0;
    // 가방 색인은 한 번만 만든다 - 장비마다 만들면 한 프레임에 수백 MB 를 복사한다.
    const game::EqBagIndex bag = game::eq_bag_index(reader);
    for (const auto& w : pieces) {
        const game::ItemCatalogEntry* e = w.key < cat.size() ? &cat[w.key] : nullptr;
        const int cap = level_cap(e, temper);
        if (cap <= 0) {
            ++skipped;
            continue;
        }
        const std::uint16_t lv = static_cast<std::uint16_t>(cap);
        const game::EqWriteResult r =
            temper ? game::eq_write_temper(reader, w.instance, lv, &bag)
                   : game::eq_write_sharpness(reader, w.instance, lv, &bag);
        const game::EqVerdict vd = game::eq_verdict(r);
        if (vd == game::EqVerdict::All) {
            ++done;
        } else if (vd == game::EqVerdict::Partial) {
            ++part;
        }
    }
    g_temper_edit.clear();
    g_sharp_edit.clear();
    game::equip_refresh_pieces(reader);
    if (done + part == 0) {
        notice_set(&g_notice, NoticeLevel::Warn,
                   "쓴 것이 없습니다 ({} 상한을 못 찾았거나 값이 없는 장비 {}개 건너뜀).",
                   what, skipped);
    } else if (part > 0) {
        notice_set(&g_notice, NoticeLevel::Warn,
                   "{}개 {} 최대, {}개는 한쪽만 적용됐습니다 - 다시 시도하세요.", done,
                   what, part);
    } else {
        notice_set(&g_notice, NoticeLevel::Ok,
                   "{}개 {} 최대 (건너뜀 {}개). 벗었다 다시 착용하면 화면에 반영됩니다.",
                   done, what, skipped);
    }
}

}  // namespace

void draw_equip_panel(bool* open) {
    if (!begin_window(Win::Equip, open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    if (ImGui::Button("다시 읽기")) {
        g_temper_edit.clear();
        g_sharp_edit.clear();
        g_dye_edit.clear();
        request_refresh_now();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("잠긴 칸은 '열기' 로 엽니다. 툴팁에 다 보이려면"
                        " 인벤토리 창의 '소켓 상한' 도 올려야 합니다.");
    draw_character_picker();

    std::vector<game::WornPiece> pieces;
    if (!game::equip_snapshot(&pieces)) {
        ImGui::TextDisabled("월드에 들어가 장비를 착용하면 읽힙니다 (자동, 최대"
                            " 수십 초).");
        ImGui::End();
        return;
    }

    // 착용 장비 전부 담금질·연마 최대. 상한은 아이템마다 표에서 온다(bulk_write).
    if (confirm_button("전부 담금질 최대")) bulk_write(reader, pieces, true);
    ImGui::SameLine();
    if (confirm_button("전부 연마 최대")) bulk_write(reader, pieces, false);
    ImGui::SameLine();
    ImGui::TextDisabled("(아이템 표의 상한까지)");

    // 착용 장비 전부 5칸 개방. 이미 열린 칸과 박힌 보석은 안 건드린다.
    ImGui::SameLine();
    if (confirm_button("전부 소켓 5칸")) {
        int done = 0, part = 0;
        // 가방 색인은 한 번만 만든다 - 장비마다 만들면 한 프레임에 수백 MB 를 복사한다.
        const game::EqBagIndex bag = game::eq_bag_index(reader);
        for (const auto& w : pieces) {
            const game::EqWriteResult r =
                game::eq_unlock_sockets(reader, w.instance, 5, &bag);
            const game::EqVerdict vd = game::eq_verdict(r);
            if (vd == game::EqVerdict::All) {
                ++done;
            } else if (vd == game::EqVerdict::Partial) {
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
    ImGui::TextDisabled("(잠긴 칸까지 엽니다)");

    notice_draw(g_notice);

    constexpr ImGuiTableFlags kF = ImGuiTableFlags_Borders |
                                   ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_ScrollY |
                                   ImGuiTableFlags_Resizable |
                                   ImGuiTableFlags_Sortable |
                                   ImGuiTableFlags_SortTristate;
    if (ImGui::BeginTable("worn", 6, kF)) {
        table_keep_natural_order();   // 다시 켤 때 정렬한 열이 맨 앞에 서지 않게(table_order.h)
        ImGui::TableSetupColumn("부위", ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_DefaultSort,
                                90.0f);
        ImGui::TableSetupColumn("장비", ImGuiTableColumnFlags_WidthStretch,
                                1.0f);
        ImGui::TableSetupColumn("담금질", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("연마", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch |
                                            ImGuiTableColumnFlags_NoSort,
                                2.0f);
        ImGui::TableSetupColumn("염색", ImGuiTableColumnFlags_WidthFixed |
                                            ImGuiTableColumnFlags_NoSort,
                                90.0f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // 12줄이라 매 프레임 정렬해도 된다. 스냅샷은 매 프레임 새 벡터다.
        static SortSpec sort;
        table_sort_pull(&sort);
        const auto& cat = game::item_catalog();
        const auto ent = [&](const game::WornPiece& w)
            -> const game::ItemCatalogEntry* {
            // w.key 는 카탈로그 순번
            return w.key < cat.size() ? &cat[w.key] : nullptr;
        };
        std::vector<const game::WornPiece*> view;
        view.reserve(pieces.size());
        for (const auto& w : pieces) view.push_back(&w);
        sort_view(view, sort, [&](const game::WornPiece* a,
                                  const game::WornPiece* b, int col) {
            switch (col) {
                case 0: {
                    const auto* ea = ent(*a);
                    const auto* eb = ent(*b);
                    return cmp3(
                        static_cast<long long>(ea != nullptr ? ea->category : 255),
                        static_cast<long long>(eb != nullptr ? eb->category : 255));
                }
                case 1: {
                    const char* na = name_of_sunbeon(a->key);
                    const char* nb = name_of_sunbeon(b->key);
                    return cmp3(std::string(na != nullptr ? na : ""),
                                std::string(nb != nullptr ? nb : ""));
                }
                case 2:
                    return cmp3(static_cast<long long>(a->temper),
                                static_cast<long long>(b->temper));
                default:
                    return cmp3(static_cast<long long>(a->sharpness),
                                static_cast<long long>(b->sharpness));
            }
        });

        for (const game::WornPiece* wp : view) {
            const game::WornPiece& w = *wp;
            // 행 ID 는 인스턴스 - 정렬로 줄이 옮겨도 무장 확인이 딴 장비로
            // 안 간다.
            ImGui::PushID(reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(w.instance)));
            ImGui::TableNextRow();

            ImGui::TableNextColumn();   // 부위
            const game::ItemCatalogEntry* e = ent(w);
            const char* part =
                e != nullptr ? category_name(e->category) : nullptr;
            if (part != nullptr) {
                ImGui::TextUnformatted(part);
            } else {
                ImGui::TextDisabled("-");
            }

            ImGui::TableNextColumn();   // 장비
            const char* nm = name_of_sunbeon(w.key);
            if (nm != nullptr) {
                ImGui::TextUnformatted(nm);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", nm);
            } else {
                ImGui::Text("(카탈로그 순번 %u)", w.key);
            }
            // 가방에도 있는 장비(게임 툴팁 "비활성화") - 값은 가방 레코드를 보이고 쓰기도
            // 가방 레코드에 같이 한다(equip_bag.h).
            if (w.in_bag) {
                ImGui::SameLine();
                ImGui::TextDisabled("(가방)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("게임에서 '비활성화' 로 보이는 장비입니다.\n"
                                      "가방 기록의 값을 보이고, 고치면 가방 기록에도 씁니다(염색은 장비 표에만).");
                }
            }

            // 담금질(+0x0A, 툴팁 게이지 10칸)과 장비 연마(+0x58, "장비 연마 N/100").
            // 상한은 표에서(level_cap) - 표가 아직 없으면 일괄처럼 "-" 로 둔다.
            ImGui::TableNextColumn();   // 담금질
            draw_level_cell(reader, w, true, level_cap(e, true), g_temper_edit);
            ImGui::TableNextColumn();   // 연마
            draw_level_cell(reader, w, false, level_cap(e, false), g_sharp_edit);

            ImGui::TableNextColumn();   // 소켓 - 칸마다 버튼 하나, 칸 안에서 흘린다
            for (int k = 0; k < 5; ++k) {
                const game::WornSocket& s = w.sockets[k];
                char lb[96];
                if (s.locked()) {
                    std::snprintf(lb, sizeof(lb), "%d 잠김##s%d", k, k);
                } else if (s.filled()) {
                    const char* gn = name_of_sunbeon(s.gem);
                    // 이름은 60바이트까지만 - 라벨이 잘려도 ID 접미사 ##s%d 는 남아야 한다.
                    std::snprintf(lb, sizeof(lb), "%d %.60s##s%d", k,
                                  gn != nullptr ? gn : "(보석)", k);
                } else {
                    std::snprintf(lb, sizeof(lb), "%d 비어 있음##s%d", k, k);
                }
                if (k > 0) {
                    flow_same_line(ImGui::CalcTextSize(lb, nullptr, true).x +
                                   ImGui::GetStyle().FramePadding.x * 2.0f);
                }
                if (ImGui::SmallButton(lb)) {
                    g_sock_inst = w.instance;
                    g_sock_k = k;
                    g_sock_open_req = true;
                    gem_picker_reset(&g_sock_picker);
                }
                if (s.filled() && ImGui::IsItemHovered()) {
                    const char* gn = name_of_sunbeon(s.gem);
                    if (gn != nullptr) ImGui::SetTooltip("%s", gn);
                }
            }

            ImGui::TableNextColumn();   // 염색 - 기존 코드 그대로
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

    draw_socket_popup(reader, pieces);
    draw_dye_popup(reader, pieces);
    ImGui::End();
}

}  // namespace cdtb::render
