#include "render/player_panel.h"

#include <imgui.h>

#include <cstdint>

#include "game/nofall.h"
#include "game/player.h"
#include "mem/reader.h"
#include "render/layout.h"

namespace cdtb::render {

void draw_player_panel(bool* open) {
    if (!begin_window(Win::Player, open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    if (!game::player_ready()) {
        ImGui::TextDisabled("월드에 들어가면 스탯 게이지를 잡습니다 (자동).");
        ImGui::TextDisabled("장비가 읽힌 뒤에 활성화됩니다.");
        ImGui::End();
        return;
    }

    const auto v = game::player_vitals(reader);
    if (v.ok) {
        ImGui::Text("생명 %d / %d", v.hp_cur, v.hp_max);
        ImGui::Text("스태미나 %d / %d", v.sta_cur, v.sta_max);
        ImGui::Text("정신력 %d / %d", v.spi_cur, v.spi_max);
        // 창 폭 320 에서 한 줄로는 잘린다. 회색은 그대로 두고 줄바꿈만 켠다.
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("(내부 수치입니다. 화면 표기와 배율이 다를 수 있습니다)");
        ImGui::PopStyleColor();
    } else {
        // 못 읽는 건 고장이 아니라 전환 중이라는 뜻이다. 빈 화면 대신 이유를 말한다.
        ImGui::TextDisabled(
            "게이지를 읽지 못했습니다 (지역 이동·캐릭터 전환 중일 수 있습니다)");
    }
    ImGui::Separator();

    bool god = game::player_godmode();
    if (ImGui::Checkbox("무적", &god)) game::player_set_godmode(god);
    bool sta = game::player_inf_stamina();
    if (ImGui::Checkbox("무한 스태미나", &sta)) game::player_set_inf_stamina(sta);
    bool spi = game::player_inf_spirit();
    if (ImGui::Checkbox("무한 정신력", &spi)) game::player_set_inf_spirit(spi);

    // 낙사 방지는 코드 훅이라 별도(전투 무적과 분리). 훅 미설치면 비활성.
    ImGui::Separator();
    if (game::nofall_installed()) {
        bool nf = game::nofall_enabled();
        if (ImGui::Checkbox("낙사 방지", &nf)) game::nofall_set(nf);
        // 같은 줄(SameLine)에 붙이면 창 폭에서 잘린다 - 실측 2026-09-12 화면에서
        // "...환경 피해를 막습니" 로 끝났다. 아래 줄로 내리고 폭에 맞춰 감싼다.
        ImGui::TextWrapped(
            "낙하와 출처 없는 환경 피해를 막습니다. 판별식을 이 빌드에 맞추는"
            " 중이라 아래 규칙을 바꿔 가며 시험합니다.");
        // 규칙을 화면에서 바꾼다. 게임을 껐다 켜지 않고 후보를 갈아 보려는 것이다
        // (2026-09-12: CT 의 가해자 슬롯 판별이 이 빌드에서 낙하를 못 걸러 냈다).
        const char* kRuleNames[] = {"가해자 없을 때만 취소",
                                    "출처 없을 때만 취소",
                                    "내 생명 피해면 무조건 취소(시험용)"};
        int rule = static_cast<int>(game::nofall_rule());
        if (rule < 0 || rule > 2) rule = 0;
        ImGui::SetNextItemWidth(300.0f);
        if (ImGui::Combo("판별 규칙", &rule, kRuleNames, 3)) {
            game::nofall_set_rule(static_cast<std::uint64_t>(rule));
        }
        if (rule == 2) {
            ImGui::TextDisabled(
                "주의: 이 규칙은 낙하가 아니어도 내 생명 피해를 전부 지웁니다."
                " r9 를 0 으로 만드는 것이 정말 피해를 막는지 가리는 용도입니다.");
        }
        const auto d = game::nofall_diag();
        ImGui::Text("취소함 %llu / 통과시킴 %llu",
                    static_cast<unsigned long long>(d.zeroed),
                    static_cast<unsigned long long>(d.let_through));
        // 마지막 생명 피해의 **출처 정체**. 낙하일 때와 맞았을 때가 어떻게 다른지
        // 이 줄로 가른다 - vtable 을 probe 로 풀면 클래스 이름이 나온다.
        ImGui::Text("마지막 출처 0x%llX  vtable 0x%llX",
                    static_cast<unsigned long long>(d.last_src),
                    static_cast<unsigned long long>(d.last_vt));
        ImGui::Text("가해자 0x%llX  델타 %lld  (출처없음 %llu / 이상한값 %llu)",
                    static_cast<unsigned long long>(d.last_atk),
                    static_cast<long long>(d.last_delta),
                    static_cast<unsigned long long>(d.src_low),
                    static_cast<unsigned long long>(d.src_bad));
        if (const auto f = game::nofall_faults(); f > 0) {
            ImGui::TextDisabled("판정 불가 %llu회 (그대로 통과시켰습니다)",
                                static_cast<unsigned long long>(f));
        }
    } else if (game::nofall_unsupported()) {
        ImGui::TextDisabled(
            "낙사 방지: 이 게임 빌드에서 훅 지점을 못 찾았습니다"
            " (모드 업데이트가 필요합니다)");
    } else {
        ImGui::TextDisabled("낙사 방지: 훅을 준비하는 중입니다...");
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "매 주기 현재=최대로 채웁니다. 정신력 풀로 플레이어를 식별하므로 주변"
        " NPC 에는 영향이 없습니다. 발열·탈것 화염 게이지는 건드리지 않습니다.");
    ImGui::End();
}

}  // namespace cdtb::render
