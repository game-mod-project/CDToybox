#include "render/grant_panel.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

#include "game/camera.h"
#include "game/grant.h"
#include "game/items.h"
#include "render/icon_atlas.h"
#include "render/item_style.h"

namespace cdtb::render {
namespace {

int g_pick = -1;
bool g_picked_by_hand = false;
int g_item_key = 50001;      // 화살
int g_count = 1;
// 카메라 좌표를 넘기면 시선 쪽에 생겨 발밑이 아니다. 게임이 위치를
// 스스로 정하는지 시험할 수 있게 둔다 - 역직렬화가 위치 필드를
// 기본값으로 초기화하는 코드가 있었다.
bool g_let_game_pick_pos = false;

bool g_called = false;
bool g_call_ok = false;
bool g_last_to_inventory = true;
game::SpawnOutcome g_outcome;

constexpr float kIconSize = 24.0f;

// 게임이 위치를 페이로드로 받는다. PlayerCameraComponent 의 +0x360
// 을 읽는데, 실측에서 아이템이 화면 정중앙에 생겼다 - 카메라의
// 초점 좌표다(그 점이 크로스헤어에 투영된다). 캐릭터 발밑 좌표는
// 아직 못 찾았다. 인벤토리 지급은 위치가 필요 없으므로 그쪽이 낫다.
bool camera_position(float out[3]) {
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

// 지금 고른 키의 아이템. 없으면 nullptr.
const game::ItemCatalogEntry* selected_item() {
    if (!game::items_ready() || g_item_key <= 0) return nullptr;
    const auto key = static_cast<std::uint32_t>(g_item_key);
    for (const auto& e : game::item_catalog()) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

// 고른 아이템을 아이콘·이름·등급·분류로 보여 준다. 키 숫자만
// 보여 주면 무엇을 주는지 알 수 없다.
void draw_selected(const game::ItemCatalogEntry* item) {
    if (item == nullptr) {
        ImGui::TextDisabled("그 키의 아이템이 목록에 없습니다");
        return;
    }
    const IconRef ico = icon_for(item->key);
    if (ico.valid) {
        ImGui::Image(ico.tex, ImVec2(kIconSize, kIconSize), ico.uv0, ico.uv1);
    } else {
        ImGui::Dummy(ImVec2(kIconSize, kIconSize));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (item->name.empty()) {
        ImGui::TextDisabled("(이름 없음)");
    } else {
        ImGui::TextColored(grade_color(item->grade), "%s", item->name.c_str());
    }
    ImGui::SameLine();
    const char* cat = category_name(item->category);
    ImGui::TextDisabled("· %s%s%s", game::grade_label(item->grade),
                        (cat != nullptr && cat[0] != 0) ? " · " : "",
                        (cat != nullptr) ? cat : "");
}

}  // namespace

void set_grant_item_key(unsigned int key) {
    g_item_key = static_cast<int>(key);
    g_called = false;      // 새 아이템을 고르면 이전 결과는 지운다
}

void draw_grant_panel() {
    ImGui::SetNextWindowSize(ImVec2(440.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("아이템 지급")) {
        ImGui::End();
        return;
    }

    // 액터가 아니라 세션을 넘긴다. 그러면 게임이 자기 경로로 액터를
    // 찾는다 - 우리가 인벤토리 컴포넌트를 고를 일이 없다.
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = game::seen_sessions(seen, hits, 16);
    if (n == 0) {
        ImGui::TextDisabled("월드에 들어가면 준비됩니다.");
        ImGui::End();
        return;
    }

    bool server[16]{};
    for (int i = 0; i < n; ++i) server[i] = game::session_is_server(i);
    if (!g_picked_by_hand) g_pick = game::best_actor_index(hits, server, n);

    // --- 무엇을 줄 것인가 -------------------------------------------
    const game::ItemCatalogEntry* item = selected_item();
    draw_selected(item);

    ImGui::TextUnformatted("키");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("##itemkey", &g_item_key, 0, 0);
    ImGui::SameLine();
    ImGui::TextUnformatted("개수");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("##count", &g_count, 1, 10);
    if (g_count < 1) g_count = 1;

    // 최대 스택을 넘기면 게임이 조용히 거절한다. 미리 자른다.
    if (item != nullptr && item->max_stack > 0 &&
        g_count > static_cast<int>(item->max_stack)) {
        g_count = static_cast<int>(item->max_stack);
    }
    if (item != nullptr && item->max_stack > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(최대 %u)", item->max_stack);
    }
    ImGui::TextDisabled("아이템 목록에서 줄을 누르면 여기로 들어옵니다");

    // --- 막힌 이유는 항상 적는다 ------------------------------------
    const char* blocked = nullptr;
    if (g_pick < 0 || g_pick >= n) {
        blocked = "플레이어 세션을 아직 못 찾았습니다";
    } else if (game::session_class(g_pick)[0] == 0) {
        blocked = "세션을 확인하는 중입니다";
    } else if (!server[g_pick]) {
        blocked = "클라이언트 쪽 세션입니다 - 고급에서 초록색을 고르세요";
    } else if (!game::spawn_args_ok(static_cast<std::uint32_t>(g_item_key),
                                    g_count)) {
        blocked = "키는 0이 아니어야 하고 개수는 1 이상이어야 합니다";
    }
    if (blocked != nullptr) {
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f), "%s", blocked);
    }

    // --- 버튼 -------------------------------------------------------
    float pos[3]{};
    const bool have_pos = camera_position(pos);

    ImGui::BeginDisabled(blocked != nullptr || !game::give_ready());
    if (ImGui::Button("인벤토리에 넣기", ImVec2(150.0f, 0.0f))) {
        g_call_ok = game::request_give(
            seen[g_pick], static_cast<std::uint32_t>(g_item_key), g_count);
        g_called = true;
        g_last_to_inventory = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();

    ImGui::BeginDisabled(blocked != nullptr ||
                         (!have_pos && !g_let_game_pick_pos));
    if (ImGui::Button("조준한 곳에 떨구기", ImVec2(150.0f, 0.0f))) {
        if (g_let_game_pick_pos) {
            pos[0] = 0.0f;
            pos[1] = 0.0f;
            pos[2] = 0.0f;
        }
        // 렌더 스레드에서 직접 부르면 죽는다. 요청만 걸고 TLS 가 선
        // 게임 스레드가 집어 간다.
        g_call_ok = game::request_spawn(
            seen[g_pick], static_cast<std::uint32_t>(g_item_key), g_count, pos);
        g_called = true;
        g_last_to_inventory = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("위치를 게임에 맡기기", &g_let_game_pick_pos);
    if (g_let_game_pick_pos) {
        ImGui::TextDisabled("(0,0,0) 을 넘깁니다 - 게임이 발밑을 잡아 주는지 시험");
    } else if (!have_pos) {
        ImGui::TextDisabled("좌표 대기 중");
    } else {
        // 읽는 값은 카메라의 초점 좌표다 - 그래서 화면 정중앙,
        // 크로스헤어 자리에 생긴다. 캐릭터 발밑 좌표는 아직 못 찾았다.
        ImGui::TextDisabled("화면 중앙(크로스헤어) 자리에 생깁니다");
    }

    if (g_called) {
        g_outcome = game::last_outcome();
        if (game::spawn_pending()) {
            ImGui::TextDisabled("게임 스레드를 기다리는 중...");
        } else if (!g_call_ok) {
            ImGui::TextDisabled("연달아 누르면 잠시 막힙니다 (2초)");
        } else if (g_outcome.no_actor) {
            ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1.0f),
                               "그 세션에서 액터가 안 나왔습니다");
        } else if (g_outcome.crashed) {
            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                               "게임 안에서 죽었습니다 0x%X", g_outcome.seh);
        } else if (g_last_to_inventory) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                               "인벤토리에 넣었습니다");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                               "조준한 곳에 떨궜습니다");
        }
    }

    // --- 고급: 세션 고르기 ------------------------------------------
    // 자동 선택이 맞는 것을 실측으로 확인했으므로 접어 둔다. 틀릴
    // 때만 열면 된다.
    ImGui::Separator();
    if (ImGui::CollapsingHeader("고급")) {
        const auto& msg = game::spawn_message();
        if (msg.handler != 0) {
            ImGui::TextDisabled("바닥 스폰 ID %u · 처리기 0x%llX", msg.id,
                                static_cast<unsigned long long>(msg.handler));
        }
        if (g_picked_by_hand && ImGui::SmallButton("자동으로 다시 고르기")) {
            g_picked_by_hand = false;
        }
        if (ImGui::BeginTable("sessions", 3,
                              ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingFixedFit)) {
            for (int i = 0; i < n; ++i) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char id[32];
                std::snprintf(id, sizeof(id), "##s%d", i);
                if (ImGui::Selectable(id, g_pick == i,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    g_pick = i;
                    g_picked_by_hand = true;
                }
                ImGui::SameLine();
                ImGui::Text("0x%llX",
                            static_cast<unsigned long long>(seen[i]));
                ImGui::TableNextColumn();
                ImGui::Text("%u회", hits[i]);
                ImGui::TableNextColumn();
                if (server[i]) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s",
                                       short_class(game::session_class(i)));
                } else {
                    ImGui::TextDisabled("%s",
                                        short_class(game::session_class(i)));
                }
            }
            ImGui::EndTable();
        }
        if (have_pos) {
            ImGui::TextDisabled("카메라 좌표 %.1f, %.1f, %.1f", pos[0], pos[1],
                                pos[2]);
        }
    }
    ImGui::End();
}

}  // namespace cdtb::render
