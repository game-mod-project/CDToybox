#include "render/player_panel.h"

#include <imgui.h>

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
        ImGui::SameLine();
        ImGui::TextDisabled("(낙하·출처 없는 환경 피해를 막습니다)");
        // 이 두 숫자가 판별식이 맞는지 보는 **유일한** 수단이다. 「무적」을 끈
        // 채로 낮은 데서 떨어지면 취소함이 오르고, 적에게 맞으면 통과시킴만
        // 올라야 한다. 맞는 동안 취소함이 오르면 조용한 갓모드이니 바로 끈다.
        ImGui::Text("취소함 %llu / 통과시킴 %llu",
                    static_cast<unsigned long long>(game::nofall_zeroed()),
                    static_cast<unsigned long long>(
                        game::nofall_let_through()));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "취소함 = 가해자 없는 피해(낙하·환경·지속 피해)를 0 으로 만든 "
                "횟수.\n"
                "통과시킴 = 가해자가 있어 그대로 둔 횟수.\n"
                "\n"
                "검증은 「무적」을 끈 채로 하십시오.\n"
                "  적에게 맞았는데 통과시킴이 오르면 정상입니다.\n"
                "  통과시킴이 안 오르고 취소함만 오르면 모든 피해가 지워지는 "
                "것이니 바로 끄고 알려 주십시오.\n"
                "  둘 다 안 오르면 훅이 안 물린 것입니다(판정 불가).");
        }
        // 폴트 가드가 끼어들었다면 그 사실을 숨기지 않는다 - 가해자 판정이
        // 이 빌드에서 불안정하다는 뜻이다(피해는 통과시키므로 위험하지는 않다).
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
