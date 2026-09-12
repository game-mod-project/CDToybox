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
        ImGui::TextDisabled("(첫 낙하 한 번은 피해를 받습니다 - 그때 대상을 학습합니다)");
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
