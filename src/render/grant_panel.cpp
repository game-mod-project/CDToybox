#include "render/grant_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "game/camera.h"
#include "game/grant.h"

namespace cdtb::render {
namespace {

int g_pick = -1;
bool g_picked_by_hand = false;
int g_item_key = 50001;      // 화살
int g_count = 1;

bool g_called = false;
bool g_call_ok = false;
game::SpawnOutcome g_outcome;

// 게임이 위치를 페이로드로 받는다 - 자동으로 발밑이 되지 않는다.
// 카메라 분석이 찾아 둔 플레이어 컴포넌트에서 좌표를 가져온다.
bool player_position(float out[3]) {
    const auto& set = game::cameras();
    if (set.player_component == 0) return false;
    return game::read_world_position(set.player_component, out);
}

// ".?AVServerInventoryActorComponent@pa@@" 에서 쓸 만한 부분만.
const char* short_class(const char* mangled) {
    if (mangled == nullptr || mangled[0] == 0) return "(확인 중)";
    const char* p = std::strstr(mangled, ".?AV");
    return (p != nullptr) ? p + 4 : mangled;
}

}  // namespace

void draw_grant_panel() {
    // 아이템 목록 안에 접어 넣었더니 스크롤 밖으로 밀려 버튼이 보이지
    // 않았다. 따로 띄운다.
    ImGui::SetNextWindowSize(ImVec2(560.0f, 340.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("지급 (시험)")) {
        ImGui::End();
        return;
    }

    // 액터가 아니라 세션을 넘긴다. 그러면 게임이 자기 경로로 액터를
    // 찾는다 - 우리가 인벤토리 컴포넌트를 고를 일이 없다. 예전에는
    // 직접 골랐다가 틀린 것을 찍어 게임 안에서 죽었다.
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = game::seen_sessions(seen, hits, 16);
    if (n == 0) {
        ImGui::TextDisabled("세션을 아직 못 봤습니다. 월드에 들어가세요.");
        ImGui::End();
        return;
    }

    // 가장 많이 쓰인 세션이 플레이어 것이다.
    if (!g_picked_by_hand) {
        int best = 0;
        for (int i = 1; i < n; ++i) {
            if (hits[i] > hits[best]) best = i;
        }
        g_pick = best;
    }

    const auto& msg = game::spawn_message();
    if (msg.handler != 0) {
        ImGui::Text("메시지 ID %u  처리기 0x%llX", msg.id,
                    static_cast<unsigned long long>(msg.handler));
    }
    ImGui::TextUnformatted("세션 - 호출이 가장 많은 것이 플레이어입니다");
    if (ImGui::BeginTable("actors", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        for (int i = 0; i < n; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char id[32];
            std::snprintf(id, sizeof(id), "##a%d", i);
            if (ImGui::Selectable(id, g_pick == i,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                g_pick = i;
                g_picked_by_hand = true;
            }
            ImGui::SameLine();
            ImGui::Text("0x%llX", static_cast<unsigned long long>(seen[i]));
            ImGui::TableNextColumn();
            ImGui::Text("호출 %u회", hits[i]);
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("아이템 키");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("##itemkey", &g_item_key);
    ImGui::SameLine();
    ImGui::TextUnformatted("개수");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("##count", &g_count);
    if (g_count < 1) g_count = 1;

    float pos[3]{};
    const bool have_pos = player_position(pos);

    // 못 누르는 이유를 항상 적는다. 눌러도 아무 일이 없으면 무엇이
    // 잘못됐는지 알 수 없다 - 실제로 그렇게 막혔다.
    const char* blocked = nullptr;
    if (g_pick < 0 || g_pick >= n) {
        blocked = "세션을 고르세요";
    } else if (!have_pos) {
        blocked = "플레이어 좌표를 아직 못 읽었습니다 (월드 진입 필요)";
    } else if (!game::spawn_args_ok(static_cast<std::uint32_t>(g_item_key),
                                    g_count)) {
        blocked = "아이템 키는 0이 아니어야 하고 개수는 1 이상이어야 합니다";
    } else if (!game::spawn_ready()) {
        blocked = "치트 메시지를 해석하지 못했습니다";
    }

    if (have_pos) {
        ImGui::Text("위치 %.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);
    }

    if (blocked != nullptr) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "%s", blocked);
    }

    ImGui::BeginDisabled(blocked != nullptr);
    if (ImGui::Button("발밑에 떨구기", ImVec2(160.0f, 0.0f))) {
        // 이 함수는 렌더 스레드에서 돈다. 게임 함수를 부르기에 맞는
        // 스레드다 - 다른 스레드에서 부르면 죽는다.
        g_call_ok = game::spawn_item_to_ground(
            seen[g_pick], static_cast<std::uint32_t>(g_item_key), g_count, pos,
            &g_outcome);
        g_called = true;
    }
    ImGui::EndDisabled();

    if (g_called) {
        ImGui::SameLine();
        if (!g_call_ok) {
            ImGui::TextDisabled("부르지 못했습니다");
        } else if (g_outcome.crashed) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                               "게임 안에서 죽었다 0x%X - 대상이 틀렸습니다",
                               g_outcome.seh);
        } else if (g_outcome.result == 0) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "성공 (0)");
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1.0f),
                               "게임이 거절: 0x%X", g_outcome.result);
        }
    }
    ImGui::End();
}

}  // namespace cdtb::render
