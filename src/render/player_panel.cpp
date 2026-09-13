#include "render/player_panel.h"

#include <imgui.h>

#include <cstdint>

#include "game/nofall.h"
#include "game/player.h"
#include "game/skillgate.h"
#include "game/skillpoint.h"
#include "render/colors.h"
#include "render/notice.h"
#include "mem/reader.h"
#include "render/layout.h"

namespace cdtb::render {

namespace {

// 스킬 포인트(어비스 결속). 기술 창의 우상단 넷째 카운터다.
void draw_skill_bond(const mem::Reader& reader) {
    if (!ImGui::CollapsingHeader("스킬 포인트 (어비스 결속)")) return;

    if (!game::knowledge_ready()) {
        ImGui::TextDisabled("월드에 들어가면 지식 컴포넌트를 잡습니다 (자동).");
        return;
    }

    // realm 둘을 다 보여 준다. 실측에서는 두 벌이 바이트까지 같았지만, 어긋나면
    // 그것이 곧 진단이다(한쪽만 써진 상태를 모르고 지나치지 않게).
    const game::BondState sv = game::bond_read(reader, 0);
    const game::BondState cl = game::bond_read(reader, 1);
    if (sv.address == 0 && cl.address == 0) {
        ImGui::TextDisabled("지금은 결속 값을 읽을 수 없습니다.");
        return;
    }
    const game::BondState& s = sv.address != 0 ? sv : cl;
    ImGui::Text("보유 %d · 총합 %d", s.have, s.total);
    ImGui::SameLine();
    // 좌하단 "사용" 은 저장된 값이 아니라 화면이 계산해 그리는 값이다.
    ImGui::TextDisabled("(화면의 '사용' = %d)", s.total - s.have);
    if (sv.address != 0 && cl.address != 0 &&
        (sv.have != cl.have || sv.total != cl.total)) {
        ImGui::TextColored(col::kWarn, "두 realm 이 다릅니다: 서버 %d/%d · 클라 %d/%d",
                           sv.have, sv.total, cl.have, cl.total);
    }

    static int s_add = 10;
    static bool s_also_total = true;
    static std::string s_msg;

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("더할 양", &s_add, 1, game::kBondAddMax);
    if (ImGui::Checkbox("총합도 함께 올리기", &s_also_total)) {}
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "기술 창 좌하단의 '사용 어비스 결속' 은 저장된 값이 아니라\n"
            "화면이 (총합 - 보유)로 계산해 그리는 값입니다(실측 확인).\n"
            "총합을 같이 올리지 않으면 그 숫자가 그만큼 줄어들어,\n"
            "쓰지도 않은 것을 되돌려받은 것처럼 보입니다.\n"
            "끄는 것은 어느 사본을 화면이 읽는지 가릴 때만 쓰십시오.");
    }

    if (ImGui::Button("결속 더하기")) {
        const auto r = game::bond_add(reader, s_add, s_also_total);
        s_msg = "바꾼 것 " + std::to_string(r.changed) + " realm, 건너뜀 " +
                std::to_string(r.skip) + ", 실패 " + std::to_string(r.fail);
        if (r.changed == 0 && r.skip > 0) s_msg += std::string(" - ") + r.last_skip;
    }
    ImGui::SameLine();
    const bool has_backup = game::bond_has_backup();
    if (!has_backup) ImGui::BeginDisabled();
    if (ImGui::Button("되돌리기")) {
        const auto r = game::bond_restore(reader);
        s_msg = "되돌린 것 " + std::to_string(r.changed) + " realm, 건너뜀 " +
                std::to_string(r.skip) + ", 실패 " + std::to_string(r.fail);
        if (r.skip > 0) s_msg += std::string(" - ") + r.last_skip;
    }
    if (!has_backup) ImGui::EndDisabled();
    if (!has_backup &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("이번 실행에서 더한 적이 없습니다.");
    }
    if (!s_msg.empty()) ImGui::TextWrapped("%s", s_msg.c_str());

    ImGui::TextWrapped(
        "되돌리기는 이번 실행 동안에만 됩니다. 저장에 남는지는 확인되지 "
        "않았습니다 - 처음이라면 적게(예: 10) 더해 보고 저장 후 게임을 다시 켜서 "
        "유지되는지 보십시오.");
    ImGui::TextDisabled("상한 %d - 참고 모드에 스킬 포인트 기능이 없어 근거로 삼을"
                        " 숫자가 없습니다. 보수적으로 잡은 값입니다.",
                        game::kBondCeiling);
}

}  // namespace

namespace {

// 스킬 강화 조건 관문. **게임 코드에 바이트를 쓴다** - 다른 치트들과 성격이 다르므로
// 그 사실을 화면이 먼저 말한다.
void draw_skill_gates() {
    if (!ImGui::CollapsingHeader("스킬 강화 조건 무시")) return;

    // **누르기 전에** 그 자리를 한 번 본다. 안 그러면 원본이 다른 빌드(게임 갱신)에서
    // 첫 클릭이 아무 반응 없이 먹히고, 빨간 줄은 다음 프레임에야 뜬다. 이미 확인한
    // 관문은 다시 안 본다.
    game::skillgate_probe();

    ImGui::TextWrapped(
        "게임 코드에 직접 바이트를 씁니다. 끄면 원래대로 되돌리고, 모드를 내릴 때도"
        " 되돌립니다. 그 자리의 원본 여덟 바이트가 우리가 아는 것과 다르면(게임"
        " 갱신) 설치를 거부합니다.");

    static Notice s_note;
    for (int g = 0; g < game::kGateCount; ++g) {
        const game::SkillGateInfo i = game::skillgate_info(g);
        // 못 쓰는 자리라도 **이미 걸려 있으면 체크박스를 남긴다** - 켜 둔 채 끌
        // 방법이 사라지면 안 된다.
        if (i.unsupported && !i.on) {
            ImGui::TextColored(col::kBad, "%s: 이 게임 빌드에서는 못 씁니다", i.name);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "그 자리의 원본 바이트가 우리가 아는 것과 다릅니다.\n"
                    "게임이 갱신되면 고정 주소가 영역마다 다르게 밀립니다 -\n"
                    "모드 쪽에서 주소를 다시 찾아야 합니다.");
            }
            continue;
        }
        bool on = i.on;
        if (ImGui::Checkbox(i.name, &on)) {
            const char* why = "";
            if (!game::skillgate_set(g, on, &why)) {
                // 체크 상태는 다음 프레임에 skillgate_info 로 다시 읽으므로 저절로
                // 맞는다. 여기서 할 일은 **왜 안 됐는지 말하는 것**이다.
                notice_set(&s_note, NoticeLevel::Bad, "{}: {}", i.name, why);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", i.what);
        if (i.on && i.site != 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("0x%llX",
                                static_cast<unsigned long long>(i.site));
        }
    }
    notice_draw(s_note);

    ImGui::TextWrapped(
        "\"결속 비용 무시\" 는 판정만 통과시킵니다. 이 패치가 결속 값을 건드리지는"
        " 않지만, 실제 차감이 보유보다 큰 비용을 어떻게 쓰는지는 아직 확인되지"
        " 않았습니다 - 보유보다 비싼 노드를 찍으면 결속 수가 음수로 돌 수 있습니다."
        " 위쪽 \"스킬 포인트\" 로 결속을 먼저 채우는 쪽이 안전합니다.");
    ImGui::TextWrapped(
        "선행 지식(화면의 \"[깨달음] 필요\")은 여기서 안 풉니다 - 그 판정 루프가"
        " 화면에 뿌릴 목록도 만들어서, 잘못 건드리면 툴팁이 깨집니다. 따로"
        " 조사한 뒤에 넣습니다.");
}

}  // namespace

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
            "낙하와 환경 피해를 막습니다. 이 게임은 낙하 피해의 출처를 내 캐릭터"
            " 자신으로 적으므로(실측), 적의 타격과 그것으로 가릅니다.");
        // 규칙을 화면에서 바꾼다. 게임을 껐다 켜지 않고 후보를 갈아 보려는 것이다
        // (2026-09-12: CT 의 가해자 슬롯 판별이 이 빌드에서 낙하를 못 걸러 냈다).
        const char* kRuleNames[] = {"출처가 나 자신일 때 취소 (권장)",
                                    "가해자 없을 때만 (이 빌드에선 안 먹음)",
                                    "출처 없을 때만 취소",
                                    "내 생명 피해면 무조건 취소(시험용)"};
        int rule = static_cast<int>(game::nofall_rule());
        if (rule < 0 || rule > 3) rule = 0;
        ImGui::SetNextItemWidth(340.0f);
        if (ImGui::Combo("판별 규칙", &rule, kRuleNames, 4)) {
            game::nofall_set_rule(static_cast<std::uint64_t>(rule));
        }
        if (rule == 3) {
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

    draw_skill_bond(reader);
    draw_skill_gates();
    ImGui::End();
}

}  // namespace cdtb::render
