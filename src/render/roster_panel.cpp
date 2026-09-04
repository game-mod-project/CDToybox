#include "render/roster_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "game/roster.h"

namespace cdtb::render {
namespace {

char g_query[128] = "";
int g_tab = 0;  // 0=탈것, 1=용병, 2=캐릭터

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
    if (!game::roster_named()) {
        ImGui::TextDisabled("이름을 푸는 중입니다 (현지화 대기).");
    }

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
    ImGui::TextDisabled("줄을 누르면 키가 복사됩니다");
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
                if (ImGui::Selectable(line)) {
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
