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
int g_temper = 0;
int g_sharpness = 0;

// 열어서 줄 소켓 칸 수. 게임이 이 값만큼 칸을 열어 준다 - 보석을
// 안 고르면 **빈 칸이 열린 채로** 나온다(= 어비스 슬롯 락 우회).
// 상한은 `items::socket_room_for` 와 배열 다섯 칸이다.
int g_socket_open = 0;
// 각 칸에 박을 보석의 아이템 키. 0 이면 그 칸은 빈 채로 연다.
std::uint32_t g_socket_keys[game::kGiveMaxSockets]{};
char g_gem_search[64]{};   // 보석 고르기 안의 이름 찾기

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

// 키로 아이템을 찾는다. 없으면 nullptr.
const game::ItemCatalogEntry* entry_of(std::uint32_t key) {
    if (!game::items_ready() || key == 0) return nullptr;
    for (const auto& e : game::item_catalog()) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

// 지금 고른 키의 아이템. 없으면 nullptr.
const game::ItemCatalogEntry* selected_item() {
    if (g_item_key <= 0) return nullptr;
    return entry_of(static_cast<std::uint32_t>(g_item_key));
}

// 이 아이템에 지급으로 열 수 있는 칸 수. 배열 다섯 칸까지다.
int socket_cap_of(const game::ItemCatalogEntry* item) {
    if (item == nullptr) return 0;
    const auto room = game::socket_room(item->max_sockets, item->max_stack,
                                        item->equip_type);
    const int cap = static_cast<int>(room);
    return (cap > game::kGiveMaxSockets) ? game::kGiveMaxSockets : cap;
}

// 소켓을 **열어서** 준다.
//
// 게임의 필드 복사 함수(0x234F930)가 `TrItemValue +0x5E` 만큼 칸을 열고
// (`칸[k][4] = k`) 그 칸에 우리가 준 6바이트를 넣는다. 보석 키를 안
// 넣으면 **빈 칸이 열린 채로** 나온다 - 그것이 어비스 슬롯 락 우회다.
//
// 상한은 아이템 표의 `max_sockets` 이고, 넘겨 보내면 게임이 거절한다.
// `request_give` 가 한 번 더 자르지만 화면에서도 같은 값으로 자른다.
void draw_sockets(const game::ItemCatalogEntry* item) {
    const int cap = socket_cap_of(item);
    if (cap <= 0) {
        g_socket_open = 0;
        return;
    }
    if (g_socket_open > cap) g_socket_open = cap;
    for (int i = cap; i < game::kGiveMaxSockets; ++i) g_socket_keys[i] = 0;

    if (!ImGui::CollapsingHeader("소켓 (잠금 없이 열어서 준다)")) return;
    ImGui::Indent();

    ImGui::TextUnformatted("열 칸 수");
    ImGui::SameLine(80.0f);
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputInt("##sockets", &g_socket_open, 1, 1);
    if (g_socket_open < 0) g_socket_open = 0;
    if (g_socket_open > cap) g_socket_open = cap;
    ImGui::SameLine();
    if (ImGui::SmallButton("없음##k")) g_socket_open = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("전부##k")) g_socket_open = cap;
    ImGui::SameLine();
    ImGui::TextDisabled("(0 ~ %d)", cap);

    for (int k = 0; k < g_socket_open; ++k) {
        ImGui::PushID(k);
        ImGui::Text("칸 %d", k);
        ImGui::SameLine(60.0f);

        // 키를 손으로 넣게 두면 190종 중에서 숫자를 찾아야 한다. 목록에서
        // 고르게 한다(장비 소켓 창의 보석 고르기와 같은 방식).
        const auto* gem = entry_of(g_socket_keys[k]);
        const char* label =
            (g_socket_keys[k] == 0)
                ? "(빈 칸으로 열기)"
                : ((gem != nullptr && !gem->name.empty()) ? gem->name.c_str()
                                                          : "(표에 없는 키)");
        ImGui::SetNextItemWidth(230.0f);
        if (ImGui::BeginCombo("##gem", label)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##find", "이름으로 찾기", g_gem_search,
                                     sizeof(g_gem_search));
            if (ImGui::Selectable("(빈 칸으로 열기)", g_socket_keys[k] == 0)) {
                g_socket_keys[k] = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            int shown = 0;
            if (!game::items_ready()) {
                ImGui::TextDisabled("아이템 표를 아직 못 읽었습니다");
            } else {
                for (const auto& e : game::item_catalog()) {
                    if (e.category != game::kSocketGemCategory) continue;
                    if (e.name.empty()) continue;
                    if (g_gem_search[0] != 0 &&
                        e.name.find(g_gem_search) == std::string::npos) {
                        continue;
                    }
                    ImGui::PushID(static_cast<int>(e.key));
                    if (ImGui::Selectable(e.name.c_str(),
                                          e.key == g_socket_keys[k])) {
                        g_socket_keys[k] = e.key;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                    // 190종이라 다 그려도 되지만, 표가 커지면 무거워진다.
                    if (++shown >= 400) break;
                }
                if (shown == 0) ImGui::TextDisabled("맞는 보석이 없습니다");
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(g_socket_keys[k] == 0);
        if (ImGui::SmallButton("비우기")) g_socket_keys[k] = 0;
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::TextDisabled("보석을 안 고르면 빈 칸이 열린 채로 나옵니다.");
    ImGui::Unindent();
}

// `drive_gate_reset` 과 같은 기준. 이보다 오래 물린 것만 풀 수 있다.
constexpr unsigned long long kGateStuckMs = 30000;

// 구동 게이트가 무엇을 물고 있는지 숫자로 낸다. 물린 것이 있으면 true.
//
// 실측 2026-09-08: 게임의 지급 처리기가 안 돌아와 `running` 이 10분 넘게
// 물렸는데 화면에는 "연달아 누르면 잠시 막힙니다 (2초)" 만 떠서, 2초
// 쿨다운으로 오인하고 한참을 헤맸다. 로그와 `drive` 명령을 봐야만 알 수
// 있었다. 그 숫자를 여기 그대로 낸다.
bool draw_drive_gate() {
    const game::DriveGate g = game::drive_gate_state(game::DriveLane::Item);
    const bool held = g.pending || g.running || g.cooldown_left_ms > 0 ||
                      g.fault_session != 0;
    if (!held) return false;

    const bool stuck = (g.running && g.running_age_ms > kGateStuckMs) ||
                       (g.pending && g.pending_age_ms > kGateStuckMs);
    const ImVec4 warn(0.9f, 0.6f, 0.3f, 1.0f);
    const ImVec4 bad(0.95f, 0.35f, 0.35f, 1.0f);

    if (game::drive_point_dead()) {
        ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f),
                           "구동 지점이 게임 안에서 멈췄습니다 - "
                           "게임을 다시 시작해야 지급·획득이 동작합니다");
    }
    if (g.running) {
        // 실행 중은 원래 몇 초다. 분 단위면 게임 안에서 안 돌아온 것이다.
        ImGui::TextColored(stuck ? bad : warn,
                           "구동 중: 게임 스레드가 %.1f초째 안 돌아왔습니다",
                           g.running_age_ms / 1000.0);
    }
    if (g.pending) {
        ImGui::TextColored(stuck ? bad : warn,
                           "대기 중: %.1f초째 실행되지 않았습니다"
                           " (월드가 돌고 있어야 실행됩니다)",
                           g.pending_age_ms / 1000.0);
    }
    if (g.cooldown_left_ms > 0) {
        ImGui::TextDisabled("쿨다운 %.1f초", g.cooldown_left_ms / 1000.0);
    }
    if (g.fault_session != 0) {
        ImGui::TextColored(bad, "세션 0x%llX 는 죽어서 잠겼습니다",
                           static_cast<unsigned long long>(g.fault_session));
    }

    if (stuck) {
        ImGui::TextDisabled(
            "이 상태에서는 지급·소환이 전부 조용히 거부됩니다.");
        if (ImGui::Button("구동 게이트 풀기", ImVec2(150.0f, 0.0f))) {
            game::drive_gate_reset();
        }
        // 게임 스레드가 처리기 안에서 안 돌아온 것이면 게이트를 풀어도
        // 그 스레드는 그대로다. 실행 지점이 같이 죽으므로 재시작해야 한다.
        ImGui::SameLine();
        ImGui::TextDisabled("(풀어도 안 되면 게임을 다시 켜야 합니다)");
    }
    return true;
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

// 아이템이 바뀌면 소켓 선택은 뜻을 잃는다. 상한도 보석 자리도 다르다.
void reset_sockets() {
    g_socket_open = 0;
    for (int i = 0; i < game::kGiveMaxSockets; ++i) g_socket_keys[i] = 0;
}

void set_grant_item_key(unsigned int key) {
    g_item_key = static_cast<int>(key);
    g_called = false;      // 새 아이템을 고르면 이전 결과는 지운다
    reset_sockets();
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
    reset_sockets();
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
    // 게이트가 실제로 풀리는(지급이 통하는) 서버 세션 중 호출 최다를 고른다.
    // freshness(last_seen) 로 거르지 않는다 - 실측 2026-09-07: 로드 후에도
    // 실제로 지급이 되는 세션(세션 1)이 last_seen 이 오래됐다는 이유로 걸러져
    // "세션 못 찾음"이 됐다. gate_object 는 안전 읽기라 풀린 세션은 자연히
    // 실패하므로, 게이트 통과 자체가 곧 "지급 가능" 판정이다.
    (void)last;
    if (!g_picked_by_hand) {
        const mem::LocalReader rd;
        bool gate_open[16]{};
        std::uintptr_t gate = 0;
        for (int i = 0; i < n; ++i) {
            gate_open[i] = game::gate_object(rd, seen[i], &gate);
        }
        // 고르는 규칙은 grant.cpp 에 있다. 진단(sessions 명령)이 같은
        // 함수를 보므로, 화면을 안 봐도 패널이 무엇을 고를지 알 수 있다.
        g_pick = game::best_gate_session_index(gate_open, hits, server, n);
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
    draw_sockets(item);

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

            // 소켓. 칸 수가 곧 "열림" 이고, 보석 키가 0 인 칸은
            // 빈 칸(0xFFFF)으로 연다. 순번을 못 풀면 그 칸도 빈 칸이다 -
            // 틀린 순번을 박느니 안 박는 것이 낫다.
            int open = g_socket_open;
            const int cap_k = socket_cap_of(item);
            if (open > cap_k) open = cap_k;
            if (open < 0) open = 0;
            for (int k = 0; k < open; ++k) {
                auto& dst = extras.sockets[k];
                if (g_socket_keys[k] == 0 ||
                    !game::socket_bytes_for_key(g_socket_keys[k], dst.raw)) {
                    game::make_socket_bytes(0xFFFF, dst.raw);
                }
            }
            extras.socket_count = static_cast<std::uint8_t>(open);
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

    // 게이트는 눌렀든 안 눌렀든 낸다. 물려 있으면 다음에 눌러도 거부된다.
    const bool gate_held = draw_drive_gate();

    if (g_called) {
        g_outcome = game::last_outcome();
        if (game::spawn_pending(game::DriveLane::Item)) {
            ImGui::TextDisabled("게임 스레드를 기다리는 중...");
        } else if (!g_call_ok) {
            // 2초 쿨다운이 아니라 게이트가 물린 것일 수 있다. 게이트를
            // 이미 위에 냈으므로 여기서는 무엇 때문인지만 가른다.
            ImGui::TextDisabled(gate_held
                                    ? "구동 게이트가 물려 요청이 거부됐습니다"
                                    : "연달아 누르면 잠시 막힙니다 (2초)");
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
