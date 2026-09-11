#include "render/roster_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/actors.h"
#include "core/log.h"
#include "core/slowlog.h"
#include "core/write_log.h"
#include "game/clan.h"
#include "mem/safe_read.h"
#include "game/camera.h"
#include "game/companion.h"
#include "game/grant.h"
#include "game/roster.h"
#include "render/colors.h"
#include "render/layout.h"
#include "render/notice.h"

namespace cdtb::render {
namespace {

char g_query[128] = "";

// ----------------------------------------------------------------------
// 동반자와 관계있어 보이는 아이템
//
// 아이템 표 6813개를 이름으로 훑어 모았다(실측 2026-09-07). 지급은
// 이미 검증된 경로라 키만 있으면 버튼 하나로 끝난다.
//
// **어디까지 확인됐는지를 줄마다 적는다.** 이름이 비슷하다고 절차가
// 같으리라 넘겨짚지 않는다.
//
// - 부적 6종: 지급 -> 인벤토리에서 사용 -> 등록. 이전 세션 실측.
// - 와이번의 알: 둥지에 올리고 5분 뒤 부화. 사용자가 직접 밟은 절차다
//   (2026-09-07). 그때 게임이 아이템 사용(2676) -> 거두기(2386) 를
//   보냈다.
// - 나머지 알·둥지·새끼 고슴도치: **아무것도 확인되지 않았다.** 동반자
//   아이템인지조차 모른다. 쿠쿠새는 알 껍질(1004431)이 따로 있어
//   요리·재료일 수도 있다. 지급해서 직접 확인할 것.
struct CompanionItem {
    std::uint32_t key;
    const char* name;
    const char* kind;
    const char* how;
    bool verified;  // 실제로 되는 것을 본 적이 있나
};

const CompanionItem kCompanionItems[] = {
    {1003843, "서릿발 백곰 동행의 부적", "부적", "인벤토리에서 사용", true},
    {1003844, "은빛 송곳니 동행의 부적", "부적", "인벤토리에서 사용", true},
    {1003845, "순백의 사슴 동행의 부적", "부적", "인벤토리에서 사용", true},
    {1003846, "서릿발 알파인 아이벡스 동행의 부적", "부적",
     "인벤토리에서 사용", true},
    {1003847, "바위엄니 혹멧돼지 동행의 부적", "부적", "인벤토리에서 사용",
     true},
    {1003921, "피닉스 동행의 부적", "부적", "인벤토리에서 사용", true},
    {1004389, "와이번의 알", "알", "둥지에 올리고 5분 뒤 부화", true},
    {1004388, "쿠쿠새의 알", "알", "절차 미상 - 알아봐야 한다", false},
    {1000146, "오래된 쿠쿠새의 알", "알", "절차 미상 - 알아봐야 한다", false},
    {1001252, "황금 거위 알", "알", "절차 미상 - 알아봐야 한다", false},
    {1004574, "돌 둥지 솟대", "설치물", "와이번 알을 올린 그 둥지인지 미확인",
     false},
    {1004660, "제작법 : 돌 둥지 솟대", "제작법", "위 설치물의 제작법", false},
    {1001784, "새끼 고슴도치", "미상", "동반자인지조차 확인 안 됨", false},
};

Notice g_item_notice;   // 동반자 아이템 탭의 지급 결과
// 탭. 값은 탭 선언 순서와 무관하다 - switch 로만 쓴다.
enum class RosterTab { Companion, Nearby, Vehicle, MercType, Character, Items, Mine };
RosterTab g_tab = RosterTab::Companion;
bool g_near_companion_only = true;
// 캐릭터 탭에서 등록 가능한 종만 보인다. 표 전체는 7250행이고
// 대부분 NPC·몬스터·시체라 고를 이유가 없다.
double g_near_last_refresh = 0.0;
Notice g_near_notice;   // 근처 탭의 획득 요청 결과
mem::LocalReader g_near_reader;
double g_clan_last_refresh = 0.0;
// 기본은 게임이 보여 주는 세 갈래만. 사람 용병·안 보이는 타입을
// 되돌려야 할 때를 위해 전체 보기를 남긴다.
bool g_clan_listed_only = true;
// 그리는 루프 한복판에서 명부를 다시 읽으면 안 된다.
//
// 표는 g_roster 항목의 **포인터**를 담아 순회하는데,
// refresh_clan_roster 가 g_roster 를 통째로 교체해 그 포인터가 전부
// 무효해진다. 종을 바꾸자마자 해제된 메모리를 읽어 게임이
// 팀겼다(사용자 증상 2026-09-09).
//
// 그래서 표시만 해 두고 **다음 프레임 맨 앞에서** 읽는다.
bool g_clan_needs_refresh = false;
// 종 바꾸기 대화상자 상태. 번호가 0이면 닫혀 있다.
std::uint64_t g_species_no = 0;
std::uint16_t g_species_type = 0xFFFF;   // 대상의 동반자 타입 행
char g_species_query[64] = "";
// 기본은 **꺼 둔다.** 타입을 넘는 교체가 이미 검증됐고
// (펫->특수 탑승물 성공), 켜 두면 골라야 할 것이 안 보여
// 이유를 알 수 없다 - 사용자가 새끼 와이번을 못 찾은 것이
// 이 기본값 때문이었다(2026-09-09).
bool g_species_same_type = false;
Notice g_species_notice;   // 종 바꾸기 팝업의 결과
int g_species_pick = -1;   // 팝업에서 고른 행. 음수면 없음
std::size_t g_species_hits = 0;

// 종을 바꿔 쓴다. 주소는 그 자리에서 다시 찾는다 - 들고 있다가 쓰면
// 안 된다(2026-09-09 사고, game/clan.h 설명).
bool apply_species(std::uint64_t merc_no, std::uint16_t row) {
    const mem::Rtti* rtti = game::clan_rtti();
    if (rtti == nullptr) {
        notice_set(&g_species_notice, NoticeLevel::Bad, "RTTI 준비 전입니다");
        return false;
    }
    game::SpeciesWriteTarget t;
    if (!game::resolve_species_write(*rtti, g_near_reader, merc_no, &t)) {
        notice_set(&g_species_notice, NoticeLevel::Bad,
                   "자리를 못 찾았습니다 - 월드 안인지 보세요");
        return false;
    }
    // 게임 상태를 바꾸는 일은 **반드시 로그에 남긴다.**
    //
    // 이것이 없어서 사용자가 "바꾸기 뒤 팅겼다" 고 했을 때 무엇을 무엇으로
    // 바꿨는지 로그로 알 수가 없었다(2026-09-09). 쓰기는 남기고 본다.
    const game::RosterEntry* from = game::character_by_row(t.server_row);
    const game::RosterEntry* to = game::character_by_row(row);
    log_write("동반자 종 번호 " + std::to_string(merc_no), t.server,
              "행 " + std::to_string(t.server_row) + "(" +
                  (from != nullptr ? from->name : std::string("?")) + ")",
              "행 " + std::to_string(row) + "(" +
                  (to != nullptr ? to->name : std::string("?")) + ")");

    const std::uint8_t buf[2] = {static_cast<std::uint8_t>(row & 0xFF),
                                 static_cast<std::uint8_t>(row >> 8)};
    // 클라·서버 양쪽에 써야 한다. 서버만 쓰면 게임이 보는 사본은
    // 그대로다(실측 2026-09-09).
    if (!mem::safe_write_bytes(t.server, buf, 2) ||
        !mem::safe_write_bytes(t.client, buf, 2)) {
        notice_set(&g_species_notice, NoticeLevel::Bad, "쓰기 실패");
        return false;
    }
    game::SpeciesWriteTarget after;
    if (game::resolve_species_write(*rtti, g_near_reader, merc_no, &after) &&
        after.server_row == row && after.client_row == row) {
        notice_set(&g_species_notice, NoticeLevel::Ok, "바꿨습니다 (행 {})", row);
        g_clan_needs_refresh = true;   // 그리는 루프 밖에서 읽는다
        return true;
    }
    notice_set(&g_species_notice, NoticeLevel::Bad,
               "쓴 뒤 확인이 어긋났습니다 - 다시 보세요");
    return false;
}
std::uint32_t g_selected_key = 0;   // 마지막으로 누른 줄의 키
char g_selected_name[128] = "";

// 동반자 탭 필터
int g_comp_type = -1;        // -1=동반자 전체, 그 외 용병 표 행 번호
bool g_comp_wild_only = true;
bool g_comp_hirable_only = false;

// 대소문자 없는 부분일치. 질의는 ImGui 가 UTF-8 로 주고 이름도 UTF-8
// 이라 한글은 바이트 그대로 비교하면 맞는다(아이템 검색과 같다).
bool contains_ci(const std::string& hay, const char* needle) {
    if (needle == nullptr || needle[0] == '\0') return true;
    const std::size_t nlen = std::strlen(needle);
    if (nlen > hay.size()) return false;
    for (std::size_t i = 0; i + nlen <= hay.size(); ++i) {
        std::size_t j = 0;
        for (; j < nlen; ++j) {
            char a = hay[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == nlen) return true;
    }
    return false;
}

bool matches_query(const game::RosterEntry& e) {
    if (g_query[0] == '\0') return true;
    char keybuf[16];
    std::snprintf(keybuf, sizeof(keybuf), "%u", e.key);
    const bool by_name = (!e.name.empty() && contains_ci(e.name, g_query)) ||
                         (!e.label.empty() && contains_ci(e.label, g_query));
    const bool by_key = std::strstr(keybuf, g_query) != nullptr;
    return by_name || by_key;
}

void select(const game::RosterEntry& e) {
    g_selected_key = e.key;
    std::snprintf(g_selected_name, sizeof(g_selected_name), "%s",
                  e.display().c_str());
    char just_key[16];
    std::snprintf(just_key, sizeof(just_key), "%u", e.key);
    ImGui::SetClipboardText(just_key);
}

// 용병 타입 이름을 짧은 한글로. 표에 없으면 내부 이름 그대로.
const char* type_label(std::uint16_t row, const std::string& internal) {
    if (internal == "Vehicle_Horse") return "말";
    if (internal == "Vehicle_Special") return "특수 탑승물";
    if (internal == "Vehicle") return "탑승물";
    if (internal == "Vehicle_Dragon") return "드래곤";
    if (internal == "Vehicle_Ship") return "배";
    if (internal.rfind("Vehicle_WarMachine", 0) == 0) return "전투기계";
    if (internal == "Wagon") return "마차";
    if (internal == "Pet") return "반려동물";
    if (internal == "Dokev") return "도깨비";
    if (internal == "Domestic") return "가축";
    if (internal == "Fish") return "물고기";
    if (internal == "Insect") return "곤충";
    // 사람 용병들. 플레이어블 캐릭터가 속한 용병대의 멤버다 - 칼·루소는
    // 상점, 루크·실반·알드릭은 근접, 로널드·오토·프리츠는 원거리.
    // 그동안 영문 그대로 나와서 명부에 왜 있는지 안 보였다.
    if (internal == "Mercenary_Main") return "플레이어블";
    if (internal == "Mercenary_Melee") return "용병(근접)";
    if (internal == "Mercenary_Range") return "용병(원거리)";
    if (internal == "Mercenary_Worker") return "용병(일꾼)";
    if (internal == "Mercenary_GuestWorker") return "용병(객원)";
    if (internal == "Mercenary_Shop") return "용병(상점)";
    if (internal == "Observer") return "관찰자";
    if (internal == "RecoveryItem") return "회복";
    if (internal.empty()) {
        static char buf[16];
        std::snprintf(buf, sizeof(buf), "행 %u", row);
        return buf;
    }
    return internal.c_str();
}

// --- 동반자 탭 ---------------------------------------------------------
//
// 캐릭터 표에서 용병 타입(_mercenaryInfo)이 탈것·마차·펫·가축인 것만
// 거른다. 야생(_Wild)이 포획 대상이다
// (specs/2026-09-05-catchable-companions.md).
void draw_companion_tab() {
    const auto& chars = game::character_catalog();
    const auto& types = game::mercenary_catalog();

    // 타입 콤보: 동반자 타입 행만
    const char* current = "동반자 전체";
    if (g_comp_type >= 0) {
        current = type_label(static_cast<std::uint16_t>(g_comp_type),
                             game::mercenary_type_name(
                                 static_cast<std::uint16_t>(g_comp_type)));
    }
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("##comp_type", current)) {
        if (ImGui::Selectable("동반자 전체", g_comp_type < 0)) g_comp_type = -1;
        for (const auto& t : types) {
            if (!game::is_companion_merc_type(t.merc_type)) continue;
            char label[96];
            std::snprintf(label, sizeof(label), "%s (%s)",
                          type_label(static_cast<std::uint16_t>(t.key), t.name),
                          t.name.c_str());
            if (ImGui::Selectable(label, g_comp_type == static_cast<int>(t.key))) {
                g_comp_type = static_cast<int>(t.key);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("야생만", &g_comp_wild_only);
    ImGui::SameLine();
    ImGui::Checkbox("고용 가능만", &g_comp_hirable_only);
    if (types.empty()) {
        ImGui::TextDisabled("용병 타입 표를 못 찾아 타입 이름 대신 행 번호를 씁니다.");
    }

    static std::vector<const game::RosterEntry*> view;
    view.clear();
    std::size_t companions = 0;
    for (const auto& e : chars) {
        if (!e.is_companion()) continue;
        // 용병 표가 있으면 타입으로 거르고, 없으면 행 번호 범위로 추정
        // (실측: 행 1~13 이 탈것·마차·펫·가축·어류·곤충).
        const std::uint8_t mt = game::mercenary_type_of_row(e.merc_row);
        const bool companion_type =
            types.empty() ? (e.merc_row >= 1 && e.merc_row <= 13)
                          : game::is_companion_merc_type(mt);
        if (!companion_type) continue;
        ++companions;
        if (g_comp_type >= 0 && e.merc_row != g_comp_type) continue;
        if (g_comp_wild_only && !game::roster_is_wild(e.name)) continue;
        if (g_comp_hirable_only && !e.hirable) continue;
        if (!matches_query(e)) continue;
        view.push_back(&e);
    }
    ImGui::Text("%zu / %zu", view.size(), companions);
    ImGui::SameLine();
    ImGui::TextDisabled("줄을 누르면 키가 복사됩니다");
    if (g_selected_key != 0) {
        ImGui::SameLine();
        ImGui::Text("| 선택: %u %s", g_selected_key, g_selected_name);
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("companions", 6, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed, 52);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("내부 이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("타입", ImGuiTableColumnFlags_WidthFixed, 84);
        ImGui::TableSetupColumn("야생", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("고용", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const game::RosterEntry* e = view[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                char label[32];
                std::snprintf(label, sizeof(label), "%u##c%d", e->key, i);
                const bool sel = (e->key == g_selected_key);
                if (ImGui::Selectable(label, sel,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    select(*e);
                }
                ImGui::TableSetColumnIndex(1);
                if (e->label.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextUnformatted(e->label.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(e->name.empty() ? "(이름 없음)"
                                                       : e->name.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    type_label(e->merc_row, game::mercenary_type_name(e->merc_row)));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(game::roster_is_wild(e->name) ? "야생"
                                                                     : "");
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(e->hirable ? "가능" : "");
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
}


// --- 근처 탭 -----------------------------------------------------------
//
// 살아 있는 액터를 걷어 캐릭터 이름을 붙인다(game/actors.h). 게임
// 메모리를 읽는 것은 여기서 2초에 한 번 또는 버튼을 눌렀을 때뿐이다.
// 동반자를 주는 아이템을 지급한다. 지급은 이미 검증된 경로다.
void draw_companion_item_tab() {
    ImGui::TextDisabled(
        "\"실측\"은 실제로 되는 것을 본 줄입니다. \"미확인\"은 이름만 보고 "
        "모은 것이라 동반자 아이템인지도 모릅니다 - 지급해서 확인해 "
        "보세요.");
    notice_draw(g_item_notice);
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable("companion_items", 6, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("종류", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed, 72);
    ImGui::TableSetupColumn("쓰는 법", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("확인", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableSetupColumn("지급", ImGuiTableColumnFlags_WidthFixed, 52);
    ImGui::TableHeadersRow();
    const int n = static_cast<int>(sizeof(kCompanionItems) /
                                   sizeof(kCompanionItems[0]));
    for (int i = 0; i < n; ++i) {
        const CompanionItem& it = kCompanionItems[i];
        if (g_query[0] != 0 && !contains_ci(it.name, g_query)) continue;
        ImGui::TableNextRow();
        ImGui::PushID(i);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(it.name);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(it.kind);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%u", it.key);
        ImGui::TableSetColumnIndex(3);
        ImGui::TextDisabled("%s", it.how);
        ImGui::TableSetColumnIndex(4);
        if (it.verified) {
            ImGui::TextUnformatted("실측");
        } else {
            ImGui::TextDisabled("미확인");
        }
        ImGui::TableSetColumnIndex(5);
        char btn[24];
        std::snprintf(btn, sizeof(btn), "지급##give%d", i);
        if (ImGui::SmallButton(btn)) {
            const std::uintptr_t sess = game::companion_pick_session();
            const bool queued =
                sess != 0 &&
                game::request_give(sess, it.key, 1, game::GiveExtras{});
            if (queued) {
                notice_set(&g_item_notice, NoticeLevel::Ok,
                           "지급 요청을 걸었습니다 - 인벤토리를 확인하세요");
            } else {
                notice_set(&g_item_notice, NoticeLevel::Warn, "{}",
                           sess == 0 ? "살아 있는 서버 세션이 없습니다"
                                     : "요청이 밀렸습니다 - 잠시 뒤 다시");
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// 동반자 레인의 구동 상태. 누른 자리에서 보여 줘야 오해가 없다.
void draw_companion_drive_gate() {
    if (game::drive_point_dead()) {
        ImGui::TextColored(col::kBad,
                           "구동 지점이 게임 안에서 멈췄습니다 - "
                           "게임을 다시 시작해야 지급·획득이 동작합니다");
    }
    const game::DriveGate g = game::drive_gate_state(game::DriveLane::Companion);
    const ImVec4 warn = col::kWarn;
    const ImVec4 bad = col::kBad;
    const bool stuck = g.pending && g.pending_age_ms > 30000;
    if (g.running) {
        ImGui::TextColored(warn, "구동 중: 게임 스레드가 %.1f초째 안 돌아왔습니다",
                           g.running_age_ms / 1000.0);
    }
    if (g.pending) {
        ImGui::TextColored(stuck ? bad : warn,
                           "동반자 구동 대기 중: %.1f초째 (월드가 돌고 있어야 실행됩니다)",
                           g.pending_age_ms / 1000.0);
    }
    if (g.cooldown_left_ms > 0) {
        ImGui::TextDisabled("쿨다운 %.1f초", g.cooldown_left_ms / 1000.0);
    }
}

// 종을 고르는 대화상자.
//
// 기본은 **같은 동반자 타입**만 보인다. 타입을 넘으면 용병단의
// 타입별 한도(컴포넌트 +0xF8)와 어긋날 수 있고 아직 시험해 보지
// 않았다. 체크를 풀면 전체가 나오되 경고를 붙인다.
void draw_species_popup() {
    if (!ImGui::BeginPopup("종 바꾸기")) return;
    log::Slow slow_p("종 바꾸기 팝업", 4.0);
    ImGui::Text("번호 %llu", static_cast<unsigned long long>(g_species_no));
    ImGui::SameLine();
    ImGui::TextDisabled("현재 타입 %s",
                       g_species_type == 0xFFFF
                           ? "-"
                           : type_label(g_species_type,
                                        game::mercenary_type_name(g_species_type)));
    ImGui::Checkbox("같은 타입만", &g_species_same_type);
    if (!g_species_same_type) {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "타입을 넘는 교체도 됩니다 - 펫→특수 탑승물 확인됨");
    }
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##species_q", "이름 또는 내부 이름(예 Wyvern)으로 거르기", g_species_query,
                             sizeof(g_species_query));
    ImGui::SameLine();
    ImGui::TextDisabled("후보 %zu개", g_species_hits);
    const ImGuiTableFlags f = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                              ImGuiTableFlags_BordersInnerV;
    if (ImGui::BeginTable("species_pick", 5, f, ImVec2(620, 320))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("행", ImGuiTableColumnFlags_WidthFixed, 48);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("내부 이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("타입", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("탑승", ImGuiTableColumnFlags_WidthFixed, 52);
        ImGui::TableHeadersRow();
        const auto& cat = game::character_catalog();
        static std::vector<const game::RosterEntry*> hits;
        hits.clear();
        for (const auto& c : cat) {
            // 걸러내는 것은 동반자 타입 하나다 - 낙타·성체 와이번이
            // 게임에 안 뜬 이유가 타입행 6(Vehicle) 이었다. 예전에
            // 함께 걸던 _equipInfo 조건은 근거가 없어 뺐다(roster.h).
            if (!c.listable()) continue;
            if (!game::is_listed_companion_row(c.merc_row)) continue;
            if (g_species_same_type && c.merc_row != g_species_type) continue;
            if (g_species_query[0] != 0 &&
                !(contains_ci(c.name, g_species_query) ||
                  contains_ci(c.label, g_species_query))) {
                continue;
            }
            hits.push_back(&c);
        }
        g_species_hits = hits.size();
        ImGuiListClipper cl;
        cl.Begin(static_cast<int>(hits.size()));
        while (cl.Step()) {
            for (int k = cl.DisplayStart; k < cl.DisplayEnd; ++k) {
                const game::RosterEntry* c = hits[static_cast<std::size_t>(k)];
                ImGui::TableNextRow();
                ImGui::PushID(k);
                ImGui::TableSetColumnIndex(0);
                char rl[32];
                std::snprintf(rl, sizeof(rl), "%u##pick%d", c->row, k);
                // 줄 클릭은 고르기만 한다. 수천 줄에서 오클릭 한 번이 곧
                // 게임 메모리 쓰기였다 - 적용은 아래 버튼이 한다.
                if (ImGui::Selectable(rl, static_cast<int>(c->row) == g_species_pick,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    g_species_pick = static_cast<int>(c->row);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(c->label.empty() ? "-" : c->label.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(c->name.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(
                    type_label(c->merc_row, game::mercenary_type_name(c->merc_row)));
                ImGui::TableSetColumnIndex(4);
                // _equipInfo 가 없으면 **소환은 되지만 탈 수 없다** -
                // 실측 2026-09-09: Animal_Tiger_Wild_2(7038)는 못 탔고,
                // 장비가 있는 Animal_Tiger_Wild_1(3438)은 탔다.
                // (안 움직인다는 것은 근거가 아니다 - 탈것은 타지 않으면
                //  원래 움직이지 않는다.) 숨기지 않고 알려 준다.
                if (c->equip_info == 0xFFFF) {
                    ImGui::TextDisabled("불가");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "장비 정보가 없는 종입니다.\n"
                            "소환은 되지만 탈 수 없습니다.\n"
                            "실측: Animal_Tiger_Wild_2(7038)는 못 탔고,\n"
                            "Animal_Tiger_Wild_1(3438)은 탔습니다.");
                    }
                } else {
                    ImGui::TextUnformatted("가능");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    const game::RosterEntry* pick =
        g_species_pick >= 0
            ? game::character_by_row(static_cast<std::uint16_t>(g_species_pick))
            : nullptr;
    if (pick != nullptr) {
        ImGui::Text("선택: %s (행 %d)",
                    pick->label.empty() ? pick->name.c_str() : pick->label.c_str(),
                    g_species_pick);
    } else {
        ImGui::TextDisabled("줄을 눌러 고르세요");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(pick == nullptr);
    if (ImGui::Button("바꾸기 적용")) {
        apply_species(g_species_no, static_cast<std::uint16_t>(g_species_pick));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("명부 레코드의 종을 그 자리에서 고쳐 씁니다.\n"
                          "저장·리로드에 남습니다.\n"
                          "되돌리려면 원래 종으로 다시 바꾸세요.");
    }
    // 결과는 누른 버튼 바로 아래에 - 팝업 머리에 두면 표에 가려 안 보였다.
    notice_draw(g_species_notice);
    ImGui::EndPopup();
}

// --- 내 동반자 탭 -----------------------------------------------------
//
// 용병단 컴포넌트의 명부를 그대로 보인다(game/clan.h). 지금까지는
// "뭐를 가졌는지" 를 게임 UI 에서만 볼 수 있었고, 획득이 실제로 들어
// 갔는지 확인할 수단이 없었다. 전부 읽기다.
void draw_my_companions_tab() {
    log::Slow slow_tab("내 동반자 탭", 8.0);
    if (!game::clan_ready()) {
        ImGui::TextDisabled("용병단 컴포넌트를 아직 못 찾았습니다. 월드 진입 후 잠시 기다리세요.");
        return;
    }
    const double now = ImGui::GetTime();
    bool refresh = g_clan_needs_refresh;
    g_clan_needs_refresh = false;
    if (ImGui::SmallButton("새로고침")) refresh = true;
    if (now - g_clan_last_refresh > 2.0) refresh = true;
    if (refresh) {
        log::Slow slow_r("명부 갱신", 4.0);
        game::refresh_clan_roster(g_near_reader);
        g_clan_last_refresh = now;
    }
    const auto& all = game::clan_roster();

    // 세 갈래로 접는다. 관찰자 같은 시스템 항목은 플레이어 동반자가
    // 아니라 기본으로 접어 둔다 - 대신 개수를 늘 보여 주므로 무엇이
    // 접혀 있는지는 숨겨지지 않는다.
    static bool show_people = true;
    static bool show_mount = true;
    static bool show_system = false;
    std::size_t n_people = 0, n_mount = 0, n_system = 0, n_other = 0;
    for (const auto& e : all) {
        switch (game::companion_group_of_row(e.merc_row)) {
            case game::CompanionGroup::People: ++n_people; break;
            case game::CompanionGroup::Mount:  ++n_mount; break;
            case game::CompanionGroup::System: ++n_system; break;
            default: ++n_other; break;
        }
    }
    char lb[48];
    ImGui::SameLine();
    std::snprintf(lb, sizeof(lb), "용병대원 (%zu)###clan_people", n_people);
    ImGui::Checkbox(lb, &show_people);
    ImGui::SameLine();
    std::snprintf(lb, sizeof(lb), "탈것·펫 (%zu)###clan_mount", n_mount);
    ImGui::Checkbox(lb, &show_mount);
    ImGui::SameLine();
    std::snprintf(lb, sizeof(lb), "시스템 (%zu)###clan_system", n_system);
    ImGui::Checkbox(lb, &show_system);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "관찰자·회복 같은 시스템 항목입니다.\n"
            "플레이어 동반자가 아니라 기본으로 접혀 있습니다.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("탈것·특수·반려동물만", &g_clan_listed_only);

    static std::vector<const game::ClanEntry*> view;
    view.clear();
    std::size_t spawned = 0;
    for (const auto& e : all) {
        if (e.spawned()) ++spawned;
        switch (game::companion_group_of_row(e.merc_row)) {
            case game::CompanionGroup::People: if (!show_people) continue; break;
            case game::CompanionGroup::Mount:  if (!show_mount) continue; break;
            case game::CompanionGroup::System: if (!show_system) continue; break;
            default: break;   // 미상은 숨기지 않는다 - 놓치면 안 된다
        }
        if (g_clan_listed_only && !game::is_listed_companion_row(e.merc_row)) {
            continue;
        }
        if (g_query[0] != 0) {
            char keybuf[16];
            std::snprintf(keybuf, sizeof(keybuf), "%u", e.key);
            if (!(contains_ci(e.name, g_query) || contains_ci(e.label, g_query) ||
                  std::strstr(keybuf, g_query))) {
                continue;
            }
        }
        view.push_back(&e);
    }
    ImGui::Text("%zu / %zu 명, 그중 월드에 %zu명", view.size(), all.size(), spawned);
    ImGui::SameLine();
    ImGui::TextDisabled("줄을 누르면 레코드 주소가 복사됩니다");

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("my_companions", 9, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("번호", ImGuiTableColumnFlags_WidthFixed, 74);
    ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("내부 이름", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("타입", ImGuiTableColumnFlags_WidthFixed, 84);
    ImGui::TableSetupColumn("행", ImGuiTableColumnFlags_WidthFixed, 48);
    ImGui::TableSetupColumn("키", ImGuiTableColumnFlags_WidthFixed, 56);
    ImGui::TableSetupColumn("소유자", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("월드", ImGuiTableColumnFlags_WidthFixed, 44);
    ImGui::TableSetupColumn("종", ImGuiTableColumnFlags_WidthFixed, 74);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(view.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const game::ClanEntry* e = view[static_cast<std::size_t>(i)];
            ImGui::TableNextRow();
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            char label[64];
            std::snprintf(label, sizeof(label), "%llu##clan%d",
                          static_cast<unsigned long long>(e->merc_no), i);
            // AllowOverlap 이 없으면 행 전체를 덤는 이 항목이 같은 줄의
            // [바꾸기] 클릭을 삼킨다 - 근처 탭에서 이미 고쳤던 문제인데
            // 새 탭에 빼먹어 버튼이 아예 안 들었다(사용자 지적 2026-09-09).
            if (ImGui::Selectable(label, false,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap)) {
                char addr[32];
                std::snprintf(addr, sizeof(addr), "0x%llX",
                              static_cast<unsigned long long>(e->record));
                ImGui::SetClipboardText(addr);
            }
            ImGui::TableSetColumnIndex(1);
            if (e->label.empty()) ImGui::TextDisabled("-");
            else ImGui::TextUnformatted(e->label.c_str());
            ImGui::TableSetColumnIndex(2);
            if (e->name.empty()) ImGui::TextDisabled("(행 %u)", e->row);
            else ImGui::TextUnformatted(e->name.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(
                e->merc_row == 0xFFFF
                    ? ""
                    : type_label(e->merc_row, game::mercenary_type_name(e->merc_row)));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%u", e->row);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%u", e->key);
            ImGui::TableSetColumnIndex(6);
            // 레코드 +0x148. 게임 자신의 소유자 판정(RVA 0x209FE40)이
            // 비교하는 그 자리다(clan.h 설명).
            if (e->owner_row == 0xFFFF) {
                // 개인 임자가 기록되지 않은 것. 게임에서는 플레이어블
                // 캐릭터들이 속한 **용병대**의 것으로 보면 된다(사용자
                // 확인). 실측으로 아는 것은 "+0x148 에 개인이 없다" 까지고,
                // '용병대' 라는 이름은 그 게임 안 의미를 붙인 것이다.
                ImGui::TextDisabled("용병대");
            } else {
                char on[64];
                if (e->owner_name.empty())
                    std::snprintf(on, sizeof(on), "행 %u", e->owner_row);
                else
                    std::snprintf(on, sizeof(on), "%s", e->owner_name.c_str());
                // 플레이어블(클리프·데미안·웅카…)은 그대로, NPC 소유는
                // 흐리게. 판정은 이름이 아니라 데이터다 - 주인공은 지금
                // 조종 중인 캐릭터 행, 나머지는 Mercenary_Main 타입이다.
                if (e->owner_playable) ImGui::TextUnformatted(on);
                else ImGui::TextDisabled("%s", on);
            }
            ImGui::TableSetColumnIndex(7);
            if (e->spawned()) ImGui::TextUnformatted("예");
            else ImGui::TextDisabled("-");
            ImGui::TableSetColumnIndex(8);
            // 종을 바꾸면 게임 목록·소환·저장까지 따라온다(실측
            // 2026-09-09: 혹멧돼지 -> 사자, 타고 다니고 리로드를 넘음).
            //
            // 한때 "야생 획득분은 바꾸면 죽는다" 며 이 버튼을 막았는데,
            // 그 판정 근거(레코드 +0x30~+0x4F)가 출신과 무관하다는 것이
            // 실측으로 드러나 걷어냈다. 막아야 할 것을 못 막고 멀쩡한
            // 것만 막고 있었다.
            if (ImGui::SmallButton("바꾸기")) {
                g_species_no = e->merc_no;
                g_species_type = e->merc_row;
                g_species_query[0] = 0;
                g_species_pick = -1;
                notice_clear(&g_species_notice);
                ImGui::OpenPopup("종 바꾸기");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("명부 레코드의 종을 그 자리에서 고쳐 씁니다.\n"
                                  "저장·리로드에 남습니다.\n"
                                  "되돌리려면 원래 종으로 다시 바꾸세요.");
            }
            draw_species_popup();
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void draw_nearby_tab() {
    if (!game::actor_manager_ready()) {
        ImGui::TextDisabled("액터 매니저를 아직 못 찾았습니다. 월드 진입 후 잠시 기다리세요.");
        return;
    }
    const double now = ImGui::GetTime();
    bool refresh = false;
    if (ImGui::SmallButton("새로고침")) refresh = true;
    ImGui::SameLine();
    ImGui::Checkbox("탈것·특수·반려동물만", &g_near_companion_only);
    if (now - g_near_last_refresh > 2.0) refresh = true;
    if (refresh) {
        game::refresh_live_actors(g_near_reader);
        g_near_last_refresh = now;
    }
    const auto& all = game::live_actors();
    static std::vector<const game::LiveActor*> view;
    view.clear();
    for (const auto& a : all) {
        // 게임이 목록으로 보여 주는 세 갈래만 보인다(roster.h 설명).
        if (g_near_companion_only && !game::is_listed_companion_row(a.merc_row)) {
            continue;
        }
        if (g_query[0] != 0) {
            char keybuf[16];
            std::snprintf(keybuf, sizeof(keybuf), "%u", a.key);
            if (!(contains_ci(a.name, g_query) || contains_ci(a.label, g_query) ||
                  std::strstr(keybuf, g_query))) {
                continue;
            }
        }
        view.push_back(&a);
    }
    ImGui::Text("%zu / %zu 액터", view.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("줄을 누르면 액터 주소가 복사됩니다");
    // 획득한 개체가 소환이 안 되는 것은 우리 결함이 아니라 게임의
    // 소환 쿨타임이다 - 실측 2026-09-06: 그 구간에는 방금 소환한
    // 개체도 전에 잘 되던 개체도 똑같이 0x533C0A53 으로 거부되고,
    // 몇 분 뒤에는 여섯 번 연속 전부 성공했다. 앞서 "재적재가
    // 필요하다"고 적었던 안내는 오진이라 지운다.
    const game::SpawnWorkResult sr = game::last_spawn_work();
    if (sr.valid && sr.code != 0) {
        if (sr.code == game::kSpawnCooldownCode) {
            ImGui::TextColored(col::kWarn,
                               "소환 거부 (번호 %llu) - 소환 쿨타임으로 보입니다. "
                               "잠시 뒤 다시 시도하세요",
                               static_cast<unsigned long long>(sr.merc_no));
        } else {
            ImGui::TextColored(col::kWarn,
                               "소환 거부 (번호 %llu) 코드 0x%08X",
                               static_cast<unsigned long long>(sr.merc_no),
                               sr.code);
        }
    }

    // 구동 상태를 여기서 보여 준다. 예전에는 아이템 지급 패널에만 떴어
    // 획득을 눌렀는데 ‘아이템 지급’ 이 대기 중으로 보였다(사용자 지적
    // 2026-09-09). 이젠 레인이 나뉘어 있고, 여기는 동반자 레인만 본다.
    draw_companion_drive_gate();

    // 눌렀는데 대기열이 차 있으면 요청은 버려진다. 그것을 화면에 알린다
    // (실측 2026-09-06: 빠르게 여러 번 누르면 조용히 사라졌다).
    notice_draw(g_near_notice);
    // 죽은 세션은 잠긴다. 왜 눌러도 안 되는지 화면에 그대로 쓴다.
    if (game::drive_fault_session() != 0) {
        ImGui::TextColored(col::kBad,
                           "구동이 게임 안에서 죽어 세션을 잠갔습니다. "
                           "월드를 다시 들어가면 풀립니다.");
    }
    // 마지막 획득 결과. 게임은 거부를 조용히 코드로만 알려 준다.
    const game::HireWorkResult hr = game::last_hire_work();
    if (hr.valid) {
        if (hr.code == 0) {
            ImGui::TextColored(col::kOk,
                               "최근 획득 0x%08X: 성공", hr.handle);
        } else {
            ImGui::TextColored(col::kWarn,
                               "최근 획득 0x%08X: 거부 (코드 0x%08X)", hr.handle,
                               hr.code);
        }
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("nearby", 10, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("액터", ImGuiTableColumnFlags_WidthFixed, 104);
        ImGui::TableSetupColumn("핸들", ImGuiTableColumnFlags_WidthFixed, 82);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("내부 이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("타입", ImGuiTableColumnFlags_WidthFixed, 84);
        ImGui::TableSetupColumn("야생", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("고용", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("소유", ImGuiTableColumnFlags_WidthFixed, 56);
        ImGui::TableSetupColumn("획득", ImGuiTableColumnFlags_WidthFixed, 52);
        ImGui::TableSetupColumn("거두기", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableHeadersRow();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const game::LiveActor* a = view[static_cast<std::size_t>(i)];
                ImGui::TableNextRow();
                // 줄마다 ID 를 분리한다. 라벨의 "##" 뒤가 ID 이므로 그것도
                // 줄마다 다르게 준다 - 둘 중 하나만으로는 ImGui 가 같은 ID 를
                // 두 개 봤다고 경고한다(실측 2026-09-06).
                ImGui::PushID(i);
                ImGui::TableSetColumnIndex(0);
                char label[64];
                std::snprintf(label, sizeof(label), "%llX##row%d",
                              static_cast<unsigned long long>(a->actor), i);
                // AllowOverlap 이 없으면 행 전체를 덮는 이 항목이 같은 줄의
                // 버튼 클릭을 삼킨다.
                if (ImGui::Selectable(label, false,
                                      ImGuiSelectableFlags_SpanAllColumns |
                                          ImGuiSelectableFlags_AllowOverlap)) {
                    char addr[32];
                    std::snprintf(addr, sizeof(addr), "0x%llX",
                                  static_cast<unsigned long long>(a->actor));
                    ImGui::SetClipboardText(addr);
                }
                ImGui::TableSetColumnIndex(1);
                if (a->handle != 0) {
                    ImGui::Text("%08X", a->handle);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(2);
                if (a->label.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::TextUnformatted(a->label.c_str());
                }
                ImGui::TableSetColumnIndex(3);
                if (a->name.empty()) {
                    ImGui::TextDisabled("(행 %u)", a->row);
                } else {
                    ImGui::TextUnformatted(a->name.c_str());
                }
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(
                    a->is_companion()
                        ? type_label(a->merc_row, game::mercenary_type_name(a->merc_row))
                        : "");
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(game::roster_is_wild(a->name) ? "야생" : "");
                ImGui::TableSetColumnIndex(6);
                ImGui::TextUnformatted(a->hirable ? "가능" : "");
                ImGui::TableSetColumnIndex(7);
                // 이미 임자가 있는 개체는 획득이 거부된다. 누르고 나서
                // 코드 0x97AE29C9 를 보기 전에 표에서 구분한다
                // (실측 2026-09-09, 근거는 game/actors.h 설명).
                if (a->owned()) {
                    ImGui::TextColored(col::kWarn, "소유");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "이미 임자가 있는 개체입니다 (고용주 핸들 0x%08X).\n"
                            "이미 길들인 전설마·스토리 동료가 여기 해당합니다.",
                            a->owner);
                    }
                } else if (a->is_companion()) {
                    ImGui::TextDisabled("-");
                }
                ImGui::TableSetColumnIndex(8);
                // 획득: 그 자리에서 동반자로 등록한다(2338). 실제 게임플레이
                // 거래라 되돌리려면 게임의 반려동물 풀어주기를 쓴다.
                const bool can = a->is_companion() && a->handle != 0 &&
                                 !a->owned() && game::hire_target_ready();
                char btn[32];
                std::snprintf(btn, sizeof(btn), "획득##hire%d", i);
                ImGui::BeginDisabled(!can);
                if (ImGui::SmallButton(btn)) {
                    const std::uintptr_t sess = game::companion_pick_session();
                    const bool queued =
                        sess != 0 && game::request_hire_target(sess, a->handle, 0);
                    if (!queued) {
                        notice_set(&g_near_notice, NoticeLevel::Warn, "{}",
                                   sess == 0
                                       ? "살아 있는 서버 세션이 없습니다 - 월드에 "
                                         "들어가서 잠시 기다리세요"
                                   : sess == game::drive_fault_session()
                                       ? "세션이 잠겨 있습니다"
                                       : "요청이 밀렸습니다 - 앞의 작업이 끝나면 "
                                         "다시 누르세요");
                    }
                }
                ImGui::EndDisabled();
                // 비활성 버튼은 AllowWhenDisabled 가 없으면 hover 가 false 라
                // 툴팁이 절대 안 떴다. 막힌 이유별로 가른다.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (can) {
                        ImGui::SetTooltip(
                            "이 개체를 동반자로 등록합니다.\n"
                            "소환이 안 되면 게임의 소환 쿨타임입니다.\n"
                            "되돌리려면 게임의 반려동물 풀어주기를 쓰세요.");
                    } else if (a->owned()) {
                        ImGui::SetTooltip(
                            "이미 임자가 있어 등록되지 않습니다.\n"
                            "게임이 코드 0x97AE29C9 로 거부하는 자리입니다.");
                    } else if (!a->is_companion()) {
                        ImGui::SetTooltip("동반자로 등록할 수 있는 종이 아닙니다.");
                    } else if (a->handle == 0) {
                        ImGui::SetTooltip("액터 핸들이 아직 없습니다 - 잠시 뒤 다시 보세요.");
                    } else {
                        ImGui::SetTooltip("획득 경로가 아직 준비되지 않았습니다.");
                    }
                }
                ImGui::TableSetColumnIndex(9);
                // 거두기(2386): 알에서 깬 개체를 거두는 경로다. 야생 개체를
                // 잡는 길이 아니다(실측 2026-09-07: 임의의 야생 동물에게
                // 쏘면 아무 일도 일어나지 않는다). 표본이 있어 남겨 두지만
                // 일반 획득은 왼쪽 칸을 쓴다.
                const bool can_catch = a->handle != 0 && game::catch_ready();
                char cbtn[32];
                std::snprintf(cbtn, sizeof(cbtn), "거두기##catch%d", i);
                ImGui::BeginDisabled(!can_catch);
                if (ImGui::SmallButton(cbtn)) {
                    const std::uintptr_t sess = game::companion_pick_session();
                    const bool queued =
                        sess != 0 && game::request_catch(sess, a->handle, 0);
                    if (!queued) {
                        notice_set(&g_near_notice, NoticeLevel::Warn, "{}",
                                   sess == 0 ? "살아 있는 서버 세션이 없습니다"
                                             : "요청이 밀렸습니다 - 잠시 뒤 다시");
                    }
                }
                ImGui::EndDisabled();
                // 비활성일 때도 이유를 낸다 - 회색 버튼만 보면 왜 못
                // 누르는지 알 수 없다.
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if (can_catch) {
                        ImGui::SetTooltip(
                            "알에서 깬 개체를 거두는 경로입니다(2386).\n"
                            "야생 개체에게는 아무 일도 일어나지 않습니다 - "
                            "획득은 왼쪽 칸을 쓰세요.");
                    } else {
                        ImGui::SetTooltip("알에서 깬 개체만 거둘 수 있습니다.");
                    }
                }
                ImGui::PopID();
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
}

// --- 단순 목록 탭 (탈것·용병 타입·캐릭터) ------------------------------
void draw_list_tab(const std::vector<game::RosterEntry>& all,
                   bool show_merc_type) {
    static std::vector<const game::RosterEntry*> view;
    view.clear();
    view.reserve(all.size());
    for (const auto& e : all) {
        if (matches_query(e)) view.push_back(&e);
    }
    ImGui::Text("%zu / %zu", view.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("줄을 누르면 키가 복사됩니다");
    if (g_selected_key != 0) {
        ImGui::SameLine();
        ImGui::Text("| 선택: %u %s", g_selected_key, g_selected_name);
    }
    ImGui::Separator();

    if (ImGui::BeginChild("roster_list")) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const game::RosterEntry* e = view[static_cast<std::size_t>(i)];
                char line[256];
                if (show_merc_type) {
                    std::snprintf(line, sizeof(line), "%-4u  type %u  %s##r%d",
                                  e->key, e->merc_type,
                                  e->display().empty() ? "(이름 없음)"
                                                       : e->display().c_str(),
                                  i);
                } else if (e->label.empty()) {
                    std::snprintf(line, sizeof(line), "%-10u  %s##r%d", e->key,
                                  e->name.empty() ? "(이름 없음)" : e->name.c_str(),
                                  i);
                } else {
                    // 표시명을 앞에 두되 내부 이름을 지우지는 않는다.
                    // 키를 찾는 작업에는 내부 이름이 있어야 한다.
                    std::snprintf(line, sizeof(line), "%-10u  %s  (%s)##r%d",
                                  e->key, e->label.c_str(),
                                  e->name.empty() ? "?" : e->name.c_str(), i);
                }
                const bool sel = (e->key == g_selected_key);
                if (ImGui::Selectable(line, sel)) select(*e);
            }
        }
        clipper.End();
    }
    ImGui::EndChild();
}

}  // namespace

void draw_roster_panel(bool* open) {
    if (!begin_window(Win::Roster, open)) {
        ImGui::End();
        return;
    }

    if (!game::roster_ready()) {
        ImGui::TextDisabled("표를 아직 못 찾았습니다. 월드 진입 후 잠시 기다리세요.");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled(
        "이름은 인게임 표시명입니다. 표에 없는 행은 내부 이름만 나옵니다.");

    if (ImGui::BeginTabBar("roster_tabs")) {
        if (ImGui::BeginTabItem("동반자")) { g_tab = RosterTab::Companion; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("근처")) { g_tab = RosterTab::Nearby; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("탈것")) { g_tab = RosterTab::Vehicle; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("용병 타입")) { g_tab = RosterTab::MercType; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("캐릭터")) { g_tab = RosterTab::Character; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("내 동반자")) { g_tab = RosterTab::Mine; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("동반자 아이템")) { g_tab = RosterTab::Items; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##roster_query", "이름 또는 키 검색", g_query,
                             sizeof(g_query));

    // 소환 버튼은 없다. SpawnCharacterCheatReq 처리기가 아이템 라우터라
    // 파괴적이다(specs/2026-09-04-game-update-break.md). 동반자 소환·획득의
    // 설계는 specs/2026-09-05-companion-summon-acquire-design.md.
    switch (g_tab) {
        case RosterTab::Companion: draw_companion_tab(); break;
        case RosterTab::Nearby: draw_nearby_tab(); break;
        case RosterTab::Items: draw_companion_item_tab(); break;
        case RosterTab::Mine: draw_my_companions_tab(); break;
        case RosterTab::Vehicle:
            draw_list_tab(game::vehicle_catalog(), false);
            break;
        case RosterTab::MercType:
            if (game::mercenary_catalog().empty()) {
                ImGui::TextDisabled("용병 타입 표를 못 찾았습니다.");
            } else {
                draw_list_tab(game::mercenary_catalog(), true);
            }
            break;
        default: draw_list_tab(game::character_catalog(), false); break;
    }

    ImGui::End();
}

}  // namespace cdtb::render
