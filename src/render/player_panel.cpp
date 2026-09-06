#include "render/player_panel.h"

#include <imgui.h>

#include "game/nofall.h"
#include "game/player.h"
#include "mem/reader.h"

namespace cdtb::render {

void draw_player_panel(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(320.0f, 220.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("플레이어 치트", open)) {
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
        ImGui::TextDisabled("(내부 수치. 화면 표기와 배율이 다를 수 있음)");
    }
    ImGui::Separator();

    bool god = game::player_godmode();
    if (ImGui::Checkbox("무적 (Godmode)", &god)) game::player_set_godmode(god);
    bool sta = game::player_inf_stamina();
    if (ImGui::Checkbox("무한 스태미나", &sta)) game::player_set_inf_stamina(sta);
    bool spi = game::player_inf_spirit();
    if (ImGui::Checkbox("무한 정신력", &spi)) game::player_set_inf_spirit(spi);

    // 낙사 방지는 코드 훅이라 별도(전투 무적과 분리). 훅 미설치면 비활성.
    ImGui::Separator();
    if (game::nofall_installed()) {
        bool nf = game::nofall_enabled();
        if (ImGui::Checkbox("낙사 방지 (No Fall Damage)", &nf))
            game::nofall_set(nf);
        ImGui::SameLine();
        ImGui::TextDisabled("(한 번 낙하해야 학습)");
    } else if (game::nofall_unsupported()) {
        ImGui::TextDisabled("낙사 방지: 이 게임 빌드 미지원 (사이트 재추출 필요)");
    } else {
        ImGui::TextDisabled("낙사 방지: 훅 준비 중...");
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "매 주기 현재=최대로 채웁니다. 정신력 풀로 플레이어를 식별하므로 주변"
        " NPC 에는 영향이 없습니다. 발열·탈것 화염 게이지는 건드리지 않습니다.");
    ImGui::End();
}

}  // namespace cdtb::render
