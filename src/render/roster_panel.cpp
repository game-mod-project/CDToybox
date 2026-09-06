#include "render/roster_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/actors.h"
#include "game/camera.h"
#include "game/companion.h"
#include "game/grant.h"
#include "game/roster.h"

namespace cdtb::render {
namespace {

char g_query[128] = "";
int g_tab = 0;  // 0=동반자, 1=근처, 2=탈것, 3=용병 타입, 4=캐릭터
bool g_near_companion_only = true;
double g_near_last_refresh = 0.0;
double g_near_busy_until = 0.0;   // 요청을 못 받았다고 알리는 시각
const char* g_near_busy_why = "";  // 왜 못 받았는지
mem::LocalReader g_near_reader;
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
void draw_nearby_tab() {
    if (!game::actor_manager_ready()) {
        ImGui::TextDisabled("액터 매니저를 아직 못 찾았습니다. 월드 진입 후 잠시 기다리세요.");
        return;
    }
    const double now = ImGui::GetTime();
    bool refresh = false;
    if (ImGui::SmallButton("새로고침")) refresh = true;
    ImGui::SameLine();
    ImGui::Checkbox("동반자만", &g_near_companion_only);
    if (now - g_near_last_refresh > 2.0) refresh = true;
    if (refresh) {
        game::refresh_live_actors(g_near_reader);
        g_near_last_refresh = now;
    }
    const auto& all = game::live_actors();
    static std::vector<const game::LiveActor*> view;
    view.clear();
    for (const auto& a : all) {
        if (g_near_companion_only && !a.is_companion()) continue;
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
    // 실측 2026-09-06: 획득한 개체는 그 자리에서는 목록에만 들어가고
    // 소환에 아무 반응이 없다. 세이브/로드를 한 번 하면 정상이 된다.
    // 등록 기록은 바로 생기지만 실행 시점 상태는 월드를 다시 불러올
    // 때 완성되는 것으로 보인다.
    ImGui::TextDisabled("획득한 개체는 세이브/로드 후에 소환됩니다");

    // 눌렀는데 대기열이 차 있으면 요청은 버려진다. 그것을 화면에 알린다
    // (실측 2026-09-06: 빠르게 여러 번 누르면 조용히 사라졌다).
    if (g_near_busy_until > now) {
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s", g_near_busy_why);
    }
    // 죽은 세션은 잠긴다. 왜 눌러도 안 되는지 화면에 그대로 쓴다.
    if (game::drive_fault_session() != 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f),
                           "구동이 게임 안에서 죽어 세션을 잠갔습니다. "
                           "월드를 다시 들어가면 풀립니다.");
    }
    // 마지막 획득 결과. 게임은 거부를 조용히 코드로만 알려 준다.
    const game::HireWorkResult hr = game::last_hire_work();
    if (hr.valid) {
        if (hr.code == 0) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f),
                               "최근 획득 0x%08X: 성공", hr.handle);
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                               "최근 획득 0x%08X: 거부 (코드 0x%08X)", hr.handle,
                               hr.code);
        }
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("nearby", 8, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("액터", ImGuiTableColumnFlags_WidthFixed, 104);
        ImGui::TableSetupColumn("핸들", ImGuiTableColumnFlags_WidthFixed, 82);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("내부 이름", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("타입", ImGuiTableColumnFlags_WidthFixed, 84);
        ImGui::TableSetupColumn("야생", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("고용", ImGuiTableColumnFlags_WidthFixed, 34);
        ImGui::TableSetupColumn("획득", ImGuiTableColumnFlags_WidthFixed, 52);
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
                // 획득: 그 자리에서 동반자로 등록한다(2338). 실제 게임플레이
                // 거래라 되돌리려면 게임의 반려동물 풀어주기를 쓴다.
                const bool can = a->is_companion() && a->handle != 0 &&
                                 game::hire_target_ready();
                char btn[32];
                std::snprintf(btn, sizeof(btn), "획득##hire%d", i);
                ImGui::BeginDisabled(!can);
                if (ImGui::SmallButton(btn)) {
                    const std::uintptr_t sess = game::companion_pick_session();
                    const bool queued =
                        sess != 0 && game::request_hire_target(sess, a->handle, 0);
                    if (!queued) {
                        g_near_busy_until = now + 3.0;
                        if (sess == 0) {
                            g_near_busy_why =
                                "살아 있는 서버 세션이 없습니다 - 월드에 "
                                "들어가서 잠시 기다리세요";
                        } else if (sess == game::drive_fault_session()) {
                            g_near_busy_why = "세션이 잠겨 있습니다";
                        } else {
                            g_near_busy_why =
                                "요청이 밀렸습니다 - 앞의 작업이 끝나면 "
                                "다시 누르세요";
                        }
                    }
                }
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered() && can) {
                    ImGui::SetTooltip(
                        "이 개체를 동반자로 등록합니다.\n"
                        "세이브/로드를 한 번 해야 소환할 수 있습니다.\n"
                        "되돌리려면 게임의 반려동물 풀어주기를 쓰세요.");
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
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("탈것 · 용병 · 캐릭터", open)) {
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
        if (ImGui::BeginTabItem("동반자")) { g_tab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("근처")) { g_tab = 1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("탈것")) { g_tab = 2; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("용병 타입")) { g_tab = 3; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("캐릭터")) { g_tab = 4; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##roster_query", "이름 또는 키 검색", g_query,
                             sizeof(g_query));

    // 소환 버튼은 없다. SpawnCharacterCheatReq 처리기가 아이템 라우터라
    // 파괴적이다(specs/2026-09-04-game-update-break.md). 동반자 소환·획득의
    // 설계는 specs/2026-09-05-companion-summon-acquire-design.md.
    switch (g_tab) {
        case 0: draw_companion_tab(); break;
        case 1: draw_nearby_tab(); break;
        case 2: draw_list_tab(game::vehicle_catalog(), false); break;
        case 3:
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
