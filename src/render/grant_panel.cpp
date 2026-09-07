#include "render/grant_panel.h"

#include <windows.h>  // GetTickCount64

#include <imgui.h>

#include <cstdint>
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
const char* g_last_what = "";

// 담금질과 장비 연마. 아이템을 바꾸면 상한에 맞춰 잘린다.
//
// 소켓은 여기서 다루지 않는다. 2026-09-04 게임 업데이트로 생성/지급
// 경로에 소켓을 실으면(+0x5E>0) 새 처리기가 오류 분기로 빠져 게임이
// 죽는다(grant.cpp request_give 가 강제로 0 으로 민다). 그래서 지급
// 으로는 소켓을 못 넣는다 - 이미 박힌 소켓 표시는 인벤토리에서, 착용
// 장비의 열린 소켓 편집은 "장비 소켓" 패널(both-realms)에서 한다.
int g_temper = 0;
int g_sharpness = 0;
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

// 담금질과 장비 연마를 정해 준다. 아이템이 그 값을 가질 때만 낸다.
//
// 상한은 아이템 표에서 온다 - 담금질은 `max_temper`, 연마는
// `max_sharpness` 다. 넘겨 보내면 게임이 조용히 거절한다.
void draw_extras(const game::ItemCatalogEntry* item) {
    if (item == nullptr) return;
    const int cap_t = static_cast<int>(item->max_temper);
    const int cap_s = static_cast<int>(item->max_sharpness);
    if (cap_t == 0 && cap_s == 0) return;

    // 아이템이 바뀌면 상한 밖의 값이 남아 있을 수 있다.
    if (g_temper > cap_t) g_temper = cap_t;
    if (g_sharpness > cap_s) g_sharpness = cap_s;

    if (!ImGui::CollapsingHeader("담금질 · 장비 연마")) return;
    ImGui::Indent();

    if (cap_t > 0) {
        ImGui::TextUnformatted("담금질");
        ImGui::SameLine(80.0f);
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("##temper", &g_temper, 1, 1);
        if (g_temper < 0) g_temper = 0;
        if (g_temper > cap_t) g_temper = cap_t;
        ImGui::SameLine();
        if (ImGui::SmallButton("최소##t")) g_temper = 0;
        ImGui::SameLine();
        if (ImGui::SmallButton("최대##t")) g_temper = cap_t;
        ImGui::SameLine();
        ImGui::TextDisabled("(0 ~ %d)", cap_t);
    }

    if (cap_s > 0) {
        // 인벤토리 507개가 전부 0 이다. 화면에 무엇이 달라지는지는
        // 아직 못 봤다 - 넣어 보고 툴팁을 확인하려고 낸 칸이다.
        ImGui::TextUnformatted("연마");
        ImGui::SameLine(80.0f);
        ImGui::SetNextItemWidth(110.0f);
        ImGui::InputInt("##sharp", &g_sharpness, 1, 10);
        if (g_sharpness < 0) g_sharpness = 0;
        if (g_sharpness > cap_s) g_sharpness = cap_s;
        ImGui::SameLine();
        if (ImGui::SmallButton("최소##s")) g_sharpness = 0;
        ImGui::SameLine();
        if (ImGui::SmallButton("최대##s")) g_sharpness = cap_s;
        ImGui::SameLine();
        ImGui::TextDisabled("(0 ~ %d)", cap_s);
    }

    ImGui::Unindent();
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

unsigned int grant_item_key() {
    return (g_item_key > 0) ? static_cast<unsigned int>(g_item_key) : 0u;
}

long long grant_item_count() { return g_count; }

unsigned int grant_temper() {
    return static_cast<unsigned int>(g_temper < 0 ? 0 : g_temper);
}

void set_grant_item(unsigned int key, long long count, unsigned int temper,
                    unsigned int sharpness) {
    g_item_key = static_cast<int>(key);
    g_count = (count < 1) ? 1 : (count > 0x7FFFFFFF)
                                    ? 0x7FFFFFFF
                                    : static_cast<int>(count);
    g_temper = static_cast<int>(temper);
    g_sharpness = static_cast<int>(sharpness);
}

unsigned int grant_sharpness() {
    return static_cast<unsigned int>(g_sharpness < 0 ? 0 : g_sharpness);
}

void draw_grant_panel(bool* open) {
    ImGui::SetNextWindowPos(ImVec2(1180, 60), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 260.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("아이템 지급", open)) {
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
    std::uint64_t last[16]{};
    for (int i = 0; i < n; ++i) {
        server[i] = game::session_is_server(i);
        last[i] = game::session_last_seen(i);
    }
    // 살아 있고 게이트가 실제로 풀리는(지급이 통하는) 서버 세션을 고른다.
    // best_live 만으로는 부족했다 - 다른 세이브를 로드하면 옛 세션이 표에
    // 남아 게이트가 끊긴 채 뽑혀 "액터가 안 나왔습니다" 로 먹통이 됐다(실측
    // 2026-09-07). 게이트 통과가 곧 지급 성공 조건이다. 못 찾으면 -1(막힘
    // 표시)로 두고 stale 세션으로 폴백하지 않는다.
    if (!g_picked_by_hand) {
        const mem::LocalReader rd;
        std::uint64_t newest = 0;
        for (int i = 0; i < n; ++i) if (last[i] > newest) newest = last[i];
        int best = -1;
        std::uint32_t best_hits = 0;
        std::uintptr_t gate = 0;
        for (int i = 0; i < n; ++i) {
            if (!server[i]) continue;
            if (last[i] == 0 || (newest > last[i] &&
                                 newest - last[i] > game::kSessionFreshMs)) {
                continue;
            }
            if (!game::gate_object(rd, seen[i], &gate)) continue;
            if (best < 0 || hits[i] >= best_hits) {
                best = i;
                best_hits = hits[i];
            }
        }
        g_pick = best;
    }

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
    // 최대 스택을 넘기면 게임이 조용히 거절한다. 미리 자른다.
    // 형변환은 game::clamp_count_to_stack 안에서 다룬다 - 여기서
    // int 로 좁혔다가 개수가 음수로 못박힌 적이 있다.
    g_count = game::clamp_count_to_stack(
        g_count, item != nullptr ? item->max_stack : 0);
    if (item != nullptr && item->max_stack > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(최대 %u)", item->max_stack);
    }
    ImGui::TextDisabled("아이템 목록에서 줄을 누르면 여기로 들어옵니다");

    draw_extras(item);

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
        const auto key = static_cast<std::uint32_t>(g_item_key);
        game::GiveExtras extras;
        // 내구도가 있는 아이템은 가득 채워 준다. 안 그러면 툴팁에
        // 0/30 이 빨갛게 뜨고 공격력에 벌점이 붙는다.
        extras.endurance = game::full_endurance_for(key);
        if (item != nullptr) {
            // 상한은 여기서도 다시 자른다. 화면에서 자른 값과
            // 보내는 값이 갈리면 게임이 조용히 거절한다.
            const int cap_t = static_cast<int>(item->max_temper);
            const int t = (g_temper > cap_t) ? cap_t : g_temper;
            extras.temper = static_cast<std::uint16_t>(t < 0 ? 0 : t);

            const int cap_s = static_cast<int>(item->max_sharpness);
            const int sh = (g_sharpness > cap_s) ? cap_s : g_sharpness;
            extras.sharpness = static_cast<std::uint16_t>(sh < 0 ? 0 : sh);
        }
        g_call_ok = game::request_give(seen[g_pick], key, g_count, extras);
        g_called = true;
        g_last_to_inventory = true;
        g_last_what = "";
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
        g_last_what = "";
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
        } else if (g_last_what[0] != 0) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s",
                               g_last_what);
        } else if (g_last_to_inventory) {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                               "인벤토리에 넣었습니다");
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                               "조준한 곳에 떨궜습니다");
        }
    }

    ImGui::Separator();

    // --- 고급: 세션 고르기 ------------------------------------------
    // 자동 선택이 맞는 것을 실측으로 확인했으므로 접어 둔다. 틀릴
    // 때만 열면 된다.
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
