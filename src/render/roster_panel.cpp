#include "render/roster_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/camera.h"
#include "game/grant.h"
#include "game/roster.h"

namespace cdtb::render {
namespace {

char g_query[128] = "";
int g_tab = 0;  // 0=탈것, 1=용병, 2=캐릭터
std::uint32_t g_selected_key = 0;   // 마지막으로 누른 줄의 키
char g_selected_name[128] = "";

// 카메라 초점의 월드 좌표. 소환 위치로 쓴다. 그란트 패널과 같은 경로.
bool camera_position(float out[3]) {
    const auto& set = game::cameras();
    if (set.player_component == 0) return false;
    return game::read_world_position(set.player_component, out);
}

// 가장 유력한 서버 세션을 고른다. 그란트 패널과 같은 방식.
std::uintptr_t best_server_session() {
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = game::seen_sessions(seen, hits, 16);
    if (n == 0) return 0;
    bool server[16]{};
    for (int i = 0; i < n; ++i) server[i] = game::session_is_server(i);
    const int pick = game::best_actor_index(hits, server, n);
    if (pick < 0 || pick >= n || !server[pick]) return 0;
    return seen[pick];
}

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

const std::vector<game::RosterEntry>& active_catalog() {
    switch (g_tab) {
        case 1: return game::mercenary_catalog();
        case 2: return game::character_catalog();
        default: return game::vehicle_catalog();
    }
}

}  // namespace

void draw_roster_panel(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("탈것 · 용병 · 캐릭터", open)) {
        ImGui::End();
        return;
    }

    if (!game::roster_ready()) {
        ImGui::TextDisabled("표를 아직 못 찾았습니다. 월드 진입 후 잠시 기다리세요.");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("내부 이름입니다. 한글 표시명은 후속 조사 대상입니다.");

    if (ImGui::BeginTabBar("roster_tabs")) {
        if (ImGui::BeginTabItem("탈것")) { g_tab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("용병")) { g_tab = 1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("캐릭터")) { g_tab = 2; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##roster_query", "이름 또는 키 검색", g_query,
                             sizeof(g_query));

    const auto& all = active_catalog();

    if (g_tab == 1 && all.empty()) {
        ImGui::TextDisabled(
            "용병은 키 구조가 달라 아직 못 읽습니다 (후속 조사 대상).");
        ImGui::End();
        return;
    }

    // 거른 목록을 프레임마다 다시 만든다. 최대 캐릭터 표가 수천 개라
    // 값싸다(문자열 부분일치뿐). 걸러 낸 포인터만 그린다.
    static std::vector<const game::RosterEntry*> view;
    view.clear();
    view.reserve(all.size());
    char keybuf[16];
    for (const auto& e : all) {
        if (g_query[0] != '\0') {
            std::snprintf(keybuf, sizeof(keybuf), "%u", e.key);
            const bool by_name = !e.name.empty() && contains_ci(e.name, g_query);
            const bool by_key = std::strstr(keybuf, g_query) != nullptr;
            if (!by_name && !by_key) continue;
        }
        view.push_back(&e);
    }

    ImGui::Text("%zu / %zu", view.size(), all.size());
    ImGui::SameLine();
    ImGui::TextDisabled("줄을 누르면 선택됩니다");

    // --- 소환 (보류) --------------------------------------------------
    // 2026-09-04 업데이트로 SpawnCharacterCheatReq 의 처리기 탐지가
    // 깨졌다(잘못된 함수가 잡힌다). 실측: 소환을 누르니 필드의 NPC 가
    // 모두 사라지고 빠른 이동이 무한 로딩에 걸렸다. 새 exe 로 처리기
    // 규약을 재도출하기 전까지 버튼을 막는다.
    // 자세한 것은 specs/2026-09-04-game-update-break.md.
    if (g_selected_key != 0) {
        ImGui::Text("선택: %u  %s", g_selected_key, g_selected_name);
    }
    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1),
                       "소환은 게임 업데이트로 비활성화되었습니다.");
    ImGui::TextDisabled(
        "이 빌드에서 소환을 누르면 NPC 가 사라지고 무한 로딩에 걸립니다.");
    ImGui::TextDisabled("처리기 규약 재도출 후 다시 켭니다.");
    ImGui::Separator();

    if (ImGui::BeginChild("roster_list")) {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(view.size()));
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const game::RosterEntry* e = view[static_cast<std::size_t>(i)];
                char line[256];
                std::snprintf(line, sizeof(line), "%-10u  %s##r%d", e->key,
                              e->name.empty() ? "(이름 없음)" : e->name.c_str(),
                              i);
                const bool sel = (e->key == g_selected_key);
                if (ImGui::Selectable(line, sel)) {
                    g_selected_key = e->key;
                    std::snprintf(g_selected_name, sizeof(g_selected_name), "%s",
                                  e->name.c_str());
                    char just_key[16];
                    std::snprintf(just_key, sizeof(just_key), "%u", e->key);
                    ImGui::SetClipboardText(just_key);
                }
            }
        }
        clipper.End();
    }
    ImGui::EndChild();

    ImGui::End();
}

}  // namespace cdtb::render
