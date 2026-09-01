#include "render/grant_panel.h"

#include <imgui.h>

#include <cstdio>

#include "game/camera.h"
#include "game/grant.h"

namespace cdtb::render {
namespace {

// 조회 함수가 돌려준 것 중 사용자가 고른 자리.
int g_pick = -1;
int g_item_key = 50001;      // 화살
int g_count = 1;

bool g_called = false;
bool g_call_ok = false;
std::uint32_t g_result = 0;

// 게임이 위치를 페이로드로 받는다 - 자동으로 발밑이 되지 않는다.
// 카메라 분석이 찾아 둔 플레이어 컴포넌트에서 좌표를 가져온다.
bool player_position(float out[3]) {
    const auto& set = game::cameras();
    if (set.player_component == 0) return false;
    return game::read_world_position(set.player_component, out);
}

}  // namespace

void draw_grant_panel() {
    if (!ImGui::CollapsingHeader("지급 (시험)")) return;

    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = game::seen_actors(seen, hits, 16);
    if (n == 0) {
        ImGui::TextDisabled("액터를 아직 못 봤습니다. 월드에 들어가세요.");
        return;
    }

    ImGui::TextUnformatted("대상 (호출이 많은 쪽이 플레이어입니다)");
    for (int i = 0; i < n; ++i) {
        char label[96];
        std::snprintf(label, sizeof(label), "0x%llX  호출 %u회",
                      static_cast<unsigned long long>(seen[i]), hits[i]);
        if (ImGui::RadioButton(label, g_pick == i)) g_pick = i;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("아이템 키");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("##itemkey", &g_item_key);
    ImGui::SameLine();
    ImGui::TextUnformatted("개수");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputInt("##count", &g_count);
    if (g_count < 1) g_count = 1;

    float pos[3]{};
    const bool have_pos = player_position(pos);
    if (have_pos) {
        ImGui::Text("위치 %.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);
    } else {
        ImGui::TextDisabled("플레이어 좌표를 아직 못 읽었습니다");
    }

    const bool ready =
        g_pick >= 0 && g_pick < n && have_pos &&
        game::spawn_args_ok(static_cast<std::uint32_t>(g_item_key), g_count);

    ImGui::BeginDisabled(!ready);
    if (ImGui::Button("발밑에 떨구기")) {
        // 이 함수는 렌더 스레드에서 돈다. 게임 함수를 부르기에 맞는
        // 스레드다 - 다른 스레드에서 부르면 죽는다.
        g_call_ok = game::spawn_item_to_ground(
            seen[g_pick], static_cast<std::uint32_t>(g_item_key), g_count, pos,
            &g_result);
        g_called = true;
    }
    ImGui::EndDisabled();

    if (g_called) {
        ImGui::SameLine();
        if (!g_call_ok) {
            ImGui::TextDisabled("부르지 못했습니다");
        } else if (g_result == 0) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "성공 (0)");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1.0f),
                               "게임이 거절: 0x%X", g_result);
        }
    }
}

}  // namespace cdtb::render
