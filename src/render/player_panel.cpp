#include "render/ui_persist.h"
#include "render/player_panel.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>

#include "game/nofall.h"
#include "game/player.h"
#include "game/clan.h"
#include "game/companion.h"
#include "game/grant.h"
#include "game/knowledge.h"
#include "game/reserveslot.h"
#include "game/skillgate.h"
#include "game/wheelfill.h"
#include "game/skillpoint.h"
#include "render/colors.h"
#include "render/confirm.h"
#include "render/table_sort_imgui.h"
#include "render/notice.h"
#include "mem/reader.h"
#include "render/layout.h"
#include "render/overlay.h"

namespace cdtb::render {

namespace {

// 스킬 포인트(어비스 결속). 기술 창의 우상단 넷째 카운터다.
void draw_skill_bond(const mem::Reader& reader) {
    if (!collapsing_header("player.skillpoint",
                           "스킬 포인트 (어비스 결속)")) return;

    if (!game::knowledge_ready()) {
        ImGui::TextDisabled("월드에 들어가면 지식 컴포넌트를 잡습니다 (자동).");
        return;
    }

    // **결속은 캐릭터별이다**(실측 2026-09-14). 셋을 다 보여 주고, 쓸 칸을 고르게
    // 한다. 예전에는 첫 칸만 읽어 보유 1 만 보였고, 다른 두 캐릭터의 70·150 은
    // 화면에 아예 안 나왔다.
    static int s_slot = 0;
    static int s_add = 10;
    static bool s_also_total = true;
    static std::string s_msg;

    int readable = 0;
    for (int i = 0; i < game::kBondSlots; ++i) {
        const game::BondState sv = game::bond_read(reader, 0, i);
        const game::BondState cl = game::bond_read(reader, 1, i);
        if (sv.address == 0 && cl.address == 0) continue;
        ++readable;
        const game::BondState& s = sv.address != 0 ? sv : cl;
        ImGui::RadioButton("", &s_slot, i);
        ImGui::SameLine();
        ImGui::Text("%s: 보유 %d · 총합 %d", game::bond_name(i), s.have,
                    s.total);
        ImGui::SameLine();
        // 좌하단 "사용" 은 저장된 값이 아니라 화면이 계산해 그리는 값이다.
        ImGui::TextDisabled("(사용 %d)", s.total - s.have);
        if (sv.address != 0 && cl.address != 0 &&
            (sv.have != cl.have || sv.total != cl.total)) {
            ImGui::TextColored(col::kWarn,
                               "   두 realm 이 다릅니다: 서버 %d/%d · 클라 %d/%d",
                               sv.have, sv.total, cl.have, cl.total);
        }
    }
    if (readable == 0) {
        ImGui::TextDisabled("지금은 결속 값을 읽을 수 없습니다.");
        return;
    }
    ImGui::TextDisabled("SkillPointOwnerType 0·1·2 = 클리프·웅카·데미안"
                        " (화면과 대조해 확인).");

    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("더할 양", &s_add, 1, game::kBondAddMax);
    ImGui::SameLine();
    ImGui::TextDisabled("-> %s", game::bond_name(s_slot));
    if (ImGui::Checkbox("총합도 함께 올리기", &s_also_total)) {}
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "기술 창 좌하단의 '사용 어비스 결속' 은 저장된 값이 아니라\n"
            "화면이 (총합 - 보유)로 계산해 그리는 값입니다(실측 확인).\n"
            "총합을 같이 올리지 않으면 그 숫자가 그만큼 줄어들어,\n"
            "쓰지도 않은 것을 되돌려받은 것처럼 보입니다.\n"
            "고른 칸의 총합만 올립니다 - 다른 캐릭터는 안 건드립니다.");
    }

    if (ImGui::Button("결속 더하기")) {
        const auto r = game::bond_add(reader, s_add, s_also_total, s_slot);
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

// 지식(스킬 트리 노드) 표. 화면의 `[깨달음] 필요` / `[원소: …] 중 하나` 는 **둘 다
// 선행 지식**이고, 판정은 `배운지식표[번호].레벨 >= 필요레벨` 한 줄이다(실측).
// 여기서는 먼저 **읽기만** 해서 "무엇이 모자란가" 를 보여 주고, 배우기는 확인을 받는다.
void draw_knowledge(const mem::Reader& reader) {
    if (!collapsing_header("player.knowledge", "지식 (선행 조건)")) return;

    static game::KnowScan s_scan;
    static Notice s_note;
    static bool s_scanned = false;
    // **게임 함수를 부르는 스위치.** 기본 꺼짐 - 이 저장소에서 게임 코드를 부르는
    // 것은 이것이 처음이고, 데이터 쓰기와 위험이 다르다.
    static bool s_allow_call = false;

    game::KnowTable t;
    if (!game::know_table(reader, 0, &t)) {
        ImGui::TextDisabled("월드에 들어가면 지식 컴포넌트를 잡습니다 (자동).");
        return;
    }
    ImGui::Text("지식 %d개", t.count);
    ImGui::SameLine();
    ImGui::TextDisabled("표 0x%llX", static_cast<unsigned long long>(t.data));

    if (ImGui::Button("선행 조건 훑기")) {
        // **쓰기가 없다.** 정적 표를 걸어 "어딘가가 요구하는데 내 레벨이 모자란
        // 지식" 을 모으기만 한다.
        s_scan = game::know_scan(game::clan_rtti(), reader, 0);
        s_scanned = true;
        if (!s_scan.ok) {
            notice_set(&s_note, NoticeLevel::Bad, "{}", s_scan.skip);
        } else {
            notice_set(&s_note, NoticeLevel::Ok,
                       "지식 {}개 중 배운 것 {}개 · 모자란 선행 조건 {}개",
                       s_scan.knowledge, s_scan.learned,
                       static_cast<int>(s_scan.needs.size()));
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "게임의 지식 표를 전부 읽어, 스킬 트리 경로(태그 3)의 선행 조건\n"
            "목록을 모읍니다. 읽기만 하며 게임 상태를 바꾸지 않습니다.");
    }
    ImGui::SameLine();
    if (ImGui::Button("진단")) {
        // **읽기만 한다.** 등록 호출이 예외로 끝났을 때 무엇이 어긋났는지 보려고
        // 컴포넌트의 자리들을 로그로 남긴다.
        game::know_diagnose(reader);
        notice_set(&s_note, NoticeLevel::Ok, "진단을 로그에 남겼습니다");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("컴포넌트의 vtable·소유 액터·맵 자리를 로그에 적습니다.\n"
                          "읽기만 합니다.");
    }
    ImGui::SameLine();
    if (ImGui::Button("휠 진단")) {
        // 예약 슬롯(원형 휠) 쪽을 **한 번에** 다 찍는다. 읽기만 한다.
        game::reserveslot_diagnose(reader, game::player_char());
        // 원소 조건 넷의 정체까지 같은 버튼에서 찍는다 - 배포마다 게임을 꺼야
        // 하므로 진단은 몰아서 넣는다.
        game::element_diagnose(reader, game::player_char());
        notice_set(&s_note, NoticeLevel::Ok, "휠 진단을 로그에 남겼습니다");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("예약 슬롯 매니저·원소 칸의 조건식·런타임 슬롯을 한 번에 로그로 찍습니다. 읽기만 합니다.");
    }
    ImGui::SameLine();
    if (ImGui::Button("이름 진단")) {
        // 이름이 하나도 안 풀릴 때 - 지역화 상태와 레벨 데이터 날바이트를 찍는다.
        int n = 0;
        if (s_scan.ok && !s_scan.fresh.empty()) {
            n = s_scan.fresh[0].number;
        } else if (s_scan.ok && !s_scan.needs.empty()) {
            n = s_scan.needs[0].number;
        }
        game::know_diagnose_names(game::clan_rtti(), reader, n);
        notice_set(&s_note, NoticeLevel::Ok, "{}번 이름 진단을 로그에 남겼습니다",
                   n);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("이름이 안 뜰 때 누르십시오. 먼저 훑기를 한 번 하십시오.");
    }
    ImGui::SameLine();
    if (ImGui::Button("호출 지식")) {
        // **읽기만 한다.** 드래곤 호출 모션이 스킬이고 그 스킬을 지식이 준다는
        // 가설을, 세이브에 남는 쓰기를 하기 전에 확인한다.
        game::know_diagnose_call(reader);
        notice_set(&s_note, NoticeLevel::Ok, "호출 지식 진단을 로그에 남겼습니다");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Knowledge_CallVehicle(말, 대조군)와 Knowledge_CallDragon 이\n"
            "서버 스킬 맵에 등록돼 있는지 로그에 적습니다. 읽기만 합니다.\n"
            "대조군은 있는데 드래곤만 없다면 그것이 호출 모션이 없던 이유입니다.");
    }
    // ---- 호출 지식 (드래곤 소환 시험). **세이브에 남는 쓰기가 여기 있다.**
    //
    // 호출 모션은 스킬이고 그 스킬은 지식이 준다(게임 데이터 실측). 대조군인
    // 말 호출 지식은 레벨 1 이고 잘 불리는데, 드래곤 쪽은 레벨 0 이다 -
    // 확인된 차이가 그 한 칸뿐이라 여기서 그것만 눌러 본다.
    {
        game::CallKnow ck[game::kCallKnowCount];
        if (game::call_knowledge(reader, ck)) {
            ImGui::TextWrapped(
                "호출 모션은 스킬이고 그 스킬은 지식이 줍니다. 아래 셋 중 앞의"
                " 둘은 대조군이라 손대지 않습니다.");
            for (int i = 0; i < game::kCallKnowCount; ++i) {
                const game::CallKnow& e = ck[i];
                ImGui::PushID(2000 + i);
                if (e.number < 0) {
                    ImGui::TextColored(col::kBad, "%s: 지식을 못 찾았습니다",
                                       e.label);
                    ImGui::PopID();
                    continue;
                }
                ImGui::TextColored(e.level > 0 ? col::kOk : col::kWarn, "%s",
                                   e.label);
                ImGui::SameLine();
                ImGui::TextDisabled("행 %d · 키 %d · 레벨 %d%s", e.number,
                                    e.data_key, e.level,
                                    e.in_skill_map ? " · 스킬맵 있음" : "");
                if (i == game::kCallKnowDragon) {
                    ImGui::SameLine();
                    if (e.level == 0) {
                        if (confirm_small_button("얻기")) {
                            const game::KnowWrite w =
                                game::know_learn(reader, e.number, 1);
                            if (w.changed > 0) {
                                notice_set(&s_note, NoticeLevel::Ok,
                                           "드래곤 호출 지식 습득 - realm {}개",
                                           w.changed);
                            } else {
                                notice_set(&s_note, NoticeLevel::Bad, "실패: {}",
                                           w.last_skip[0] != 0 ? w.last_skip
                                                               : "쓰기 실패");
                            }
                        }
                    } else if (confirm_small_button("되돌리기")) {
                        const game::KnowWrite w =
                            game::know_forget(reader, e.number);
                        if (w.changed > 0) {
                            notice_set(&s_note, NoticeLevel::Ok,
                                       "드래곤 호출 지식 되돌림 - realm {}개",
                                       w.changed);
                        } else {
                            notice_set(&s_note, NoticeLevel::Bad, "실패: {}",
                                       w.last_skip[0] != 0 ? w.last_skip
                                                           : "쓰기 실패");
                        }
                    }
                }
                ImGui::PopID();
            }
            ImGui::TextDisabled(
                "쓰면 게임 저장 때 세이브에 남습니다. 자동 재적용 목록에는"
                " 안 넣습니다 - 시험이라 [되돌리기] 가 확실히 먹어야 합니다.");
        }
    }
    notice_draw(s_note);

    if (!s_scanned || !s_scan.ok) return;

    // 게임 스레드가 집어 간 결과를 받아 알림으로 바꾼다.
    {
        const game::KnowQueue q = game::knowledge_queue_state();
        if (q.has_result) {
            if (q.result.ok) {
                notice_set(&s_note, NoticeLevel::Ok,
                           "{}번 스킬 등록: 맵 {} -> {} (스킬키 {})", q.number,
                           q.result.before, q.result.after, q.result.skill_key);
            } else {
                notice_set(&s_note, NoticeLevel::Bad, "{}번 등록 실패: {}",
                           q.number, q.result.skip);
            }
            game::knowledge_queue_clear_result();
        } else if (q.pending) {
            ImGui::TextColored(col::kWarn, "%d번 등록 요청 대기 중 (게임 스레드)",
                               q.number);
        }
    }

    ImGui::Checkbox("게임 함수 호출 허용", &s_allow_call);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "켜면 각 줄에 [등록] 이 생깁니다.\n"
            "지금까지의 치트는 전부 데이터만 읽고 썼습니다 - 이것은 게임 코드를\n"
            "직접 부르는 첫 기능이라 위험이 다릅니다. 되돌릴 수 있는 세이브에서,\n"
            "전투 중이 아닐 때 시험하십시오.");
    }
    if (game::know_register_locked()) {
        ImGui::TextColored(col::kBad,
                           "등록 호출이 예외로 끝나 잠겼습니다. 저장하지 마시고"
                           " 이전 세이브를 부르거나 게임을 다시 켜십시오.");
    } else if (s_allow_call) {
        ImGui::TextColored(col::kWarn,
                           "게임 코드를 직접 부릅니다 - 되돌릴 수 있는 세이브에서"
                           " 시험하십시오.");
    }


    ImGui::TextWrapped(
        "아래는 다른 노드가 선행 조건으로 요구하는데 아직 레벨이 모자란 지식입니다."
        " \"중 하나\" 표시는 같은 목록의 다른 것으로도 조건이 풀릴 수 있다는 뜻입니다.");
    ImGui::TextWrapped(
        "\"스킬\" 칸이 \"없음\" 이면 그 지식에는 붙는 스킬이 없어 등록이 헛일입니다"
        " - 게임 자신도 그런 지식은 건너뜁니다. 그래서 [등록] 을 안 그립니다.");
    if (s_scan.needs.empty()) {
        ImGui::TextColored(col::kOk, "모자란 선행 조건이 없습니다.");
    }

    // 폭 조절(Resizable)과 머리글 정렬(Sortable). 다른 표들과 같은 배관을 쓴다.
    constexpr ImGuiTableFlags kKnowF =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate;
    if (!s_scan.needs.empty() &&
        ImGui::BeginTable("know_needs", 7, kKnowF, ImVec2(0.0f, 220.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("번호", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("지금", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("필요", ImGuiTableColumnFlags_WidthFixed, 45.0f);
        ImGui::TableSetupColumn("요구", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("스킬", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed |
                                        ImGuiTableColumnFlags_NoSort,
                                140.0f);
        ImGui::TableHeadersRow();

        static SortSpec s_need_sort;
        table_sort_pull(&s_need_sort);
        std::vector<const game::KnowNeed*> view;
        view.reserve(s_scan.needs.size());
        for (const auto& e : s_scan.needs) view.push_back(&e);
        sort_view(view, s_need_sort,
                  [](const game::KnowNeed* a, const game::KnowNeed* b, int col) {
                      switch (col) {
                          case 0: return cmp3(a->number, b->number);
                          case 1: return cmp3(a->name, b->name);
                          case 2: return cmp3(a->have_level, b->have_level);
                          case 3: return cmp3(a->need_level, b->need_level);
                          case 4: return cmp3(a->wanted_by, b->wanted_by);
                          case 5: return cmp3(a->skill_key, b->skill_key);
                          default: return 0;
                      }
                  });
        for (const game::KnowNeed* ep : view) {
            const game::KnowNeed& e = *ep;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.number);
            ImGui::TableNextColumn();
            if (e.name.empty()) {
                ImGui::TextDisabled("(이름 못 읽음)");
            } else {
                ImGui::TextUnformatted(e.name.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(e.have_level > 0 ? col::kOk : col::kWarn, "%d",
                               e.have_level);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.need_level);
            ImGui::TableNextColumn();
            ImGui::Text("%d곳%s", e.wanted_by, e.any_of ? " · 중 하나" : "");
            ImGui::TableNextColumn();
            // **붙는 스킬이 없으면 등록은 헛일이다** - 게임 자신도 건너뛴다.
            const bool has_skill =
                e.skill_key != 0 && e.skill_key != game::kNoApplySkill;
            if (has_skill) {
                ImGui::TextColored(col::kOk, "%d", e.skill_key);
            } else {
                ImGui::TextDisabled("없음");
            }
            ImGui::TableNextColumn();
            ImGui::PushID(e.number);
            if (confirm_small_button("배우기")) {
                const game::KnowWrite w =
                    game::know_learn(reader, e.number, e.need_level);
                if (w.changed > 0) {
                    // **기억해 둔다.** 저장을 잊고 끄면 사라지고 리로드는
                    // 컴포넌트를 새로 만드므로, 모자랄 때마다 다시 건다.
                    game::know_auto_remember(e.number, e.need_level);
                    notice_set(&s_note, NoticeLevel::Ok,
                               "{}번을 레벨 {} 로 - realm {}개",
                               e.number, e.need_level, w.changed);
                } else {
                    notice_set(&s_note, NoticeLevel::Bad, "{}번: {}", e.number,
                               w.last_skip[0] != 0 ? w.last_skip : "쓰기 실패");
                }
            }
            if (s_allow_call && has_skill && !game::know_register_locked()) {
                ImGui::SameLine();
                if (confirm_small_button("등록")) {
                    // **여기서 직접 부르지 않는다.** 렌더 스레드에는 게임 함수가
                    // 따라가는 TLS 가 없다 - 그래서 죽었다(1.8/1.13). 요청만 걸고
                    // 게임 스레드가 집어 간다.
                    if (game::knowledge_queue_register(e.number, e.need_level)) {
                        notice_set(&s_note, NoticeLevel::Info,
                                   "{}번 등록 요청 - 게임 스레드를 기다립니다",
                                   e.number);
                    } else {
                        notice_set(&s_note, NoticeLevel::Warn,
                                   "이미 걸린 요청이 있습니다");
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "게임의 스킬 등록 함수를 부릅니다(서버 컴포넌트에만).\n"
                        "레벨 쓰기만으로는 화면에 배운 것처럼 보이기만 하고\n"
                        "실제로는 쓸 수 없습니다 - 이 맵이 채워져야 합니다.");
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // ---------------- 스킬이 붙었는데 아직 안 배운 지식 (전수)
    //
    // 선행 조건 목록으로는 목적지에 못 간다는 것이 실측으로 드러났다 - 레벨 0 인
    // 줄에는 붙는 스킬이 없고, 스킬이 붙은 줄은 이미 배운 것뿐이라 등록해도 맵이
    // 안 늘었다. 그래서 "요구받는가" 를 안 따지고 전수로 모은다.
    ImGui::Separator();
    ImGui::Text("스킬이 붙었는데 아직 안 배운 지식: %d개", s_scan.fresh_total);
    if (s_scan.fresh_total > static_cast<int>(s_scan.fresh.size())) {
        ImGui::SameLine();
        ImGui::TextDisabled("(앞 %d개만 보여 줍니다)",
                            static_cast<int>(s_scan.fresh.size()));
    }
    if (!s_scan.fresh.empty() &&
        ImGui::BeginTable("know_fresh", 4, kKnowF, ImVec2(0.0f, 220.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("번호", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("스킬", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed |
                                        ImGuiTableColumnFlags_NoSort,
                                140.0f);
        ImGui::TableHeadersRow();

        static SortSpec s_fresh_sort;
        table_sort_pull(&s_fresh_sort);
        std::vector<const game::KnowNeed*> fview;
        fview.reserve(s_scan.fresh.size());
        for (const auto& e : s_scan.fresh) fview.push_back(&e);
        sort_view(fview, s_fresh_sort,
                  [](const game::KnowNeed* a, const game::KnowNeed* b, int col) {
                      switch (col) {
                          case 0: return cmp3(a->number, b->number);
                          case 1: return cmp3(a->name, b->name);
                          case 2: return cmp3(a->skill_key, b->skill_key);
                          default: return 0;
                      }
                  });
        for (const game::KnowNeed* ep : fview) {
            const game::KnowNeed& e = *ep;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.number);
            ImGui::TableNextColumn();
            if (e.name.empty()) {
                ImGui::TextDisabled("(이름 못 읽음)");
            } else {
                ImGui::TextUnformatted(e.name.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(col::kOk, "%d", e.skill_key);
            ImGui::TableNextColumn();
            ImGui::PushID(100000 + e.number);
            if (confirm_small_button("배우기")) {
                const game::KnowWrite w = game::know_learn(reader, e.number, 1);
                if (w.changed > 0) {
                    game::know_auto_remember(e.number, 1);
                    notice_set(&s_note, NoticeLevel::Ok,
                               "{}번을 레벨 1 로 - realm {}개", e.number,
                               w.changed);
                } else {
                    notice_set(&s_note, NoticeLevel::Bad, "{}번: {}", e.number,
                               w.last_skip[0] != 0 ? w.last_skip : "쓰기 실패");
                }
            }
            if (s_allow_call && !game::know_register_locked()) {
                ImGui::SameLine();
                if (confirm_small_button("등록")) {
                    if (game::knowledge_queue_register(e.number, 1)) {
                        notice_set(&s_note, NoticeLevel::Info,
                                   "{}번 등록 요청 - 게임 스레드를 기다립니다",
                                   e.number);
                    } else {
                        notice_set(&s_note, NoticeLevel::Warn,
                                   "이미 걸린 요청이 있습니다");
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    const int kept = static_cast<int>(game::know_auto_list().size());
    if (kept > 0) {
        ImGui::TextColored(col::kOk, "자동 재적용 %d개", kept);
        ImGui::SameLine();
        if (ImGui::SmallButton("잊기")) game::know_auto_forget();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "기억을 지웁니다. 이미 쓴 레벨을 되돌리지는 않습니다 -\n"
                "다음 리로드부터 다시 안 걸 뿐입니다.");
        }
    }
    ImGui::TextWrapped(
        "건 뒤에 게임에서 저장하면 남습니다(실측). 저장을 안 하고 끄면 사라지므로,"
        " 건 것을 기억해 두었다가 모자라면 다시 겁니다. 이 기억은 설정 파일에 남아"
        " 다음 실행에도 이어집니다.");
}

// 원소 습득. 휠 칸의 조건이 평문으로 나와 확정됐다(2026-09-15):
//   CheckEquipSlotName(Bracelet) && CheckKnowledge(Knowledge_MpFire|Ice|Lightning|Wind)
// **팔찌를 낀 채** 그 지식을 가지면 칸이 켜진다. 조건이 보는 것은 **접두사 없는**
// 이름이라, 캐릭터별 노드(Knowledge_Kliff_MpFire 등)를 써도 소용이 없다 -
// 그것으로 여러 번 헛돌았다.
void draw_elements(const mem::Reader& reader) {
    if (!collapsing_header("player.element",
                           "원소 (어비스 관문 없이 습득)")) {
        return;
    }

    static Notice s_note;
    game::ElementKnow el[game::kElementCount];
    if (!game::element_knowledge(reader, el)) {
        ImGui::TextDisabled("월드에 들어가면 지식 표를 잡습니다 (자동).");
        return;
    }
    ImGui::TextWrapped(
        "휠 칸이 켜지려면 둘이 다 필요합니다: 팔찌(Bracelet) 장착 + 그 원소 지식."
        " 여기서는 지식만 넣습니다 - 팔찌는 직접 끼셔야 합니다.");
    ImGui::TextColored(col::kWarn,
                       "아래 선행 조건 표의 \"원소 : ...\" 줄들은 여기와 다른"
                       " 것입니다 - 캐릭터별 스킬트리 노드라 눌러도 휠은 안 켜집니다.");
    for (int i = 0; i < game::kElementCount; ++i) {
        const game::ElementKnow& e = el[i];
        ImGui::PushID(i);
        if (e.number < 0) {
            ImGui::TextColored(col::kBad, "%s: 지식을 못 찾았습니다", e.label);
            ImGui::PopID();
            continue;
        }
        ImGui::TextColored(e.level > 0 ? col::kOk : col::kWarn, "%s", e.label);
        ImGui::SameLine();
        ImGui::TextDisabled("%d번 · %s", e.number,
                            e.level > 0 ? "가지고 있음" : "없음");
        if (e.level == 0) {
            ImGui::SameLine();
            if (confirm_small_button("얻기")) {
                const game::KnowWrite w = game::know_learn(reader, e.number, 1);
                if (w.changed > 0) {
                    // 저장을 잊고 껐을 때를 받친다 - 모자라면 자동으로 다시 건다.
                    game::know_auto_remember(e.number, 1);
                    notice_set(&s_note, NoticeLevel::Ok,
                               "{} 습득 - realm {}개", e.label, w.changed);
                } else {
                    notice_set(&s_note, NoticeLevel::Bad, "{}: {}", e.label,
                               w.last_skip[0] != 0 ? w.last_skip : "쓰기 실패");
                }
            }
        }
        ImGui::PopID();
    }
    notice_draw(s_note);
    ImGui::TextDisabled(
        "얻은 뒤 게임에서 저장하면 남습니다. 저장을 잊어도 다음 실행에 자동으로"
        " 다시 겁니다.");
}

// 스킬 강화 조건 관문. **게임 코드에 바이트를 쓴다** - 다른 치트들과 성격이 다르므로
// 그 사실을 화면이 먼저 말한다.
void draw_skill_gates() {
    if (!collapsing_header("player.skillgate", "스킬 강화 조건 무시")) return;

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

// 탈것 휠. 게임 데이터(`gamedata/reserveslot`)를 꺼내 읽어 확정한 것을 그린다 -
// 탈것 휠은 슬롯이 셋이고, 드래곤·ATAG 는 **메인 휠의 허용 목록에서 빠진 채**
// 자기 전용 슬롯에만 들어간다. 그 전용 슬롯이 스토리로 채워지는 자리다.
// 여기서는 메인 휠의 목록에 그 카테고리를 얹는다. 자세한 근거는 reserveslot.h.
void draw_vehicle_wheel(const mem::Reader& reader) {
    if (!collapsing_header("player.wheel",
                           "탈것 휠 (드래곤·ATAG 슬롯 우회)")) return;

    static Notice s_note;
    const game::WheelState w = game::wheel_state(reader);
    if (!w.ready) {
        ImGui::TextDisabled("%s", w.note[0] != 0
                                      ? w.note
                                      : "월드에 들어가면 예약 슬롯 표를 잡습니다.");
        return;
    }

    // 무엇을 보고 결정했는지 화면이 먼저 보인다 - 누르기 전에 확인할 수 있어야 한다.
    auto line = [](const char* name, const game::WheelSlot& s) {
        std::string v;
        for (int i = 0; i < s.count; ++i) {
            if (i != 0) v += ", ";
            v += std::to_string(s.cats[i]);
        }
        ImGui::TextDisabled("%s(키 %d) 허용: [%s]", name, s.key, v.c_str());
    };
    line("메인 휠", w.main_slot);
    line("드래곤", w.dragon);
    line("ATAG/기계", w.mech);

    // **즉시 걸지 않는다.** 게임이 휠을 다 만든 뒤에 목록을 늘리면 같은 휠의
    // 특수 탑승물 호출이 먹통이 된다(실측 2026-09-15). 그래서 설정에 적어
    // **다음 실행의 시작 지점**에서 건다 - 그때는 게임이 아직 휠을 안 만들었다.
    // 소켓 상한이 매 실행 다시 걸리는 것과 같은 방식이다.
    bool want = overlay::vehicle_wheel_setting();
    if (ImGui::Checkbox("메인 휠에 드래곤·ATAG 얹기 (다음 실행부터)", &want)) {
        if (overlay::set_vehicle_wheel_setting(want)) {
            if (want) {
                notice_set(&s_note, NoticeLevel::Ok,
                           "저장했습니다 - 게임을 다시 켜면 걸립니다");
            } else {
                notice_set(&s_note, NoticeLevel::Ok,
                           "껐습니다 - 게임을 다시 켜면 원래대로입니다");
            }
        } else {
            notice_set(&s_note, NoticeLevel::Bad, "설정 파일을 못 썼습니다");
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "메인 탈것 휠이 허용하는 카테고리 목록에\n"
            "드래곤 슬롯·메카닉 슬롯이 쓰는 값을 그대로 더합니다.\n"
            "번호를 박지 않고 그 슬롯들에서 베껴 옵니다.\n\n"
            "7시 전용 드래곤 슬롯은 무반응입니다 - 이것을 켜야\n"
            "드래곤이 6시 메인 휠에서 소환 경로까지 갑니다.\n\n"
            "게임이 휠을 만들기 전에 걸어야 하므로 지금 바로가 아니라\n"
            "다음 실행의 시작 지점에서 겁니다. 그래야 같은 휠의\n"
            "특수 탑승물이 멀쩡합니다.");
    }
    if (w.on) {
        ImGui::TextColored(col::kOk, "이번 실행에 걸려 있습니다 (허용 %d개)",
                           w.main_slot.count);
    } else if (want) {
        ImGui::TextColored(col::kWarn,
                           "다음 실행부터 걸립니다 - 지금은 안 걸려 있습니다.");
    }
    notice_draw(s_note);

    // 쿨다운. 휠에 얹고 나니 드래곤이 "쿨타임 중" 으로 막혔다(2026-09-15).
    ImGui::Separator();
    const game::MountTimerState t = game::mount_timer_state(reader);
    if (!t.ready) {
        ImGui::TextDisabled("%s", t.note);
    } else {
        ImGui::TextDisabled("탈것 %d종 · 쿨다운/시간제한이 걸린 것 %d종", t.mounts,
                            t.timed);
        bool free_on = t.on;
        if (ImGui::Checkbox("소환 쿨다운·탑승 시간제한 풀기", &free_on)) {
            if (game::mount_timer_free(reader, free_on)) {
                if (free_on) {
                    notice_set(&s_note, NoticeLevel::Ok, "{}종을 풀었습니다",
                               t.timed);
                } else {
                    notice_set(&s_note, NoticeLevel::Ok, "타이머를 되돌렸습니다");
                }
            } else {
                notice_set(&s_note, NoticeLevel::Bad,
                           "실패 - 표를 안 건드렸습니다");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "CharacterInfo 의 재소환 쿨다운(+0x70)을 1초로,\n"
                "강제 하차까지의 시간(+0x78)을 68시간으로 바꿉니다.\n"
                "원래 시간제한이 없던 탈것에는 새로 걸지 않습니다.\n\n"
                "**이미 돌기 시작한 대기 시간에는 안 듣습니다.** 게임이 부를 때\n"
                "마감 시각을 따로 박아 두기 때문입니다(실측 2026-09-15). 이건\n"
                "다음 소환부터 듣습니다 - 지금 도는 것은 아래 아이템으로 줄이십시오.");
        }
    }

    // 이미 돌고 있는 대기 시간은 위 스위치로 안 풀린다. 그 자리는 게임이 부를
    // 때 박아 두는 마감 시각인데, 명부 레코드(0x1C0)·예약 슬롯 런타임 어디에도
    // 없었다(2026-09-15: 90초·15분 간격 덤프와 아이템 사용 전후 모두 무변화).
    // 그래서 **게임이 정해 둔 길**을 쓴다 - 게임 자신의 쿨다운 감소 아이템이다.
    //
    // 지급만 한다. 사용은 인벤토리에서 하십시오 - 우리가 2976(아이템 사용)을
    // 구동해 봤지만 대기 시간이 자연 감소분 이상으로 안 줄었다(실측 19:37).
    ImGui::Separator();
    ImGui::TextDisabled("돌고 있는 재소환 대기 시간 줄이기 (아이템 지급)");
    {
        static int s_cd_count = 12;
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputInt("개수##cdreduce", &s_cd_count)) {
            if (s_cd_count < 1) s_cd_count = 1;
            if (s_cd_count > 99) s_cd_count = 99;
        }
        auto give_cd = [&](const char* label, std::uint32_t key,
                           const char* what) {
            if (!ImGui::SmallButton(label)) return;
            const std::uintptr_t session = game::companion_pick_session();
            if (session == 0) {
                notice_set(&s_note, NoticeLevel::Bad,
                           "서버 세션을 못 찾았습니다 (월드 밖?)");
                return;
            }
            if (!game::give_ready()) {
                notice_set(&s_note, NoticeLevel::Bad, "지급 준비가 안 됐습니다");
                return;
            }
            if (game::request_give(session, key, s_cd_count)) {
                notice_set(&s_note, NoticeLevel::Ok,
                           "{} 쿨다운 감소 {}개를 넣었습니다 - 가방에서 쓰십시오",
                           what, s_cd_count);
            } else {
                notice_set(&s_note, NoticeLevel::Bad,
                           "지급 대기열이 차 있습니다 - 잠시 뒤 다시");
            }
        };
        give_cd("A.T.A.G. I", 1002632, "A.T.A.G.");
        ImGui::SameLine();
        give_cd("A.T.A.G. II", 1003773, "A.T.A.G.");
        ImGui::SameLine();
        give_cd("드래곤 I", 1002631, "드래곤");
        ImGui::SameLine();
        give_cd("드래곤 II", 1003772, "드래곤");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "게임 자신의 쿨다운 감소 아이템을 가방에 넣습니다.\n"
                "  1002632 / 1003773  A.T.A.G. I · II\n"
                "  1002631 / 1003772  드래곤 I · II\n\n"
                "하나당 5~10분씩 줄어 60분을 지우려면 여러 개가 듭니다.\n"
                "**사용은 가방에서 하십시오** - 우리가 사용 메시지를 구동해 봤지만\n"
                "대기 시간이 자연 감소분 이상으로 안 줄었습니다(실측 2026-09-15).");
        }
    }

    // 호출 장소. "호출할 수 없는 장소입니다" 로 막히는 자리다(2026-09-15 확정).
    const game::CallPlaceState cp = game::call_place_state(reader);
    if (cp.ready) {
        ImGui::TextDisabled("탈것 표 %d행 · 지면 거리 검사가 걸린 것 %d행", cp.rows,
                            cp.gated);
        bool place_on = cp.on;
        if (ImGui::Checkbox("호출 장소 제한 풀기", &place_on)) {
            if (game::call_place_free(reader, place_on)) {
                if (place_on) {
                    notice_set(&s_note, NoticeLevel::Ok, "{}행을 풀었습니다",
                               cp.gated);
                } else {
                    notice_set(&s_note, NoticeLevel::Ok, "장소 제한을 되돌렸습니다");
                }
            } else {
                notice_set(&s_note, NoticeLevel::Bad, "실패 - 표를 안 건드렸습니다");
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "VehicleInfo 의 _checkDistanceToGround(+0x8C)를 0 으로 둡니다.\n"
                "드래곤은 30, 정상 동작하는 와이번은 0 입니다 - 드래곤이\n"
                "\"호출할 수 없는 장소입니다\" 로 막히던 자리입니다.");
        }
    }

    // 드래곤을 "되는 탈것" 규칙에 태운다. 검사를 하나씩 찾아 푸는 대신, 같은
    // 휠에서 정상 동작하는 특수 탑승물과 **같은 두 칸**을 주는 쪽이 빠르다.
    ImGui::Separator();
    const game::DisguiseState dg = game::disguise_state(reader);
    if (!dg.ready) {
        ImGui::TextDisabled("%s", dg.note);
    } else {
        ImGui::TextDisabled("바꿀 종 %d개 · 기증자 종행 %d (타입행 %d ·"
                            " 탈것규칙 %d 는 안 베낍니다)",
                            dg.targets, dg.donor_row, dg.donor_merc,
                            dg.donor_veh);
        if (dg.targets > 0) {
            // 누르기 전에 **무엇을 건드리는지** 보여 준다. 2026-09-15 에 대상이
            // 반려 동물로 번져도 아무 데도 안 보였다.
            char rows[128] = {};
            int at = 0;
            for (int i = 0; i < dg.targets && i < game::kDisguiseMax; ++i) {
                const int put = std::snprintf(rows + at, sizeof rows - at,
                                              i == 0 ? "%u" : ", %u",
                                              dg.rows[i]);
                if (put <= 0 || at + put >= static_cast<int>(sizeof rows)) break;
                at += put;
            }
            ImGui::TextDisabled("  대상 종행: %s", rows);
        }
        bool dis = dg.on;
        if (ImGui::Checkbox("드래곤·ATAG 를 특수 탑승물 칸으로 옮기기", &dis)) {
            if (game::disguise_apply(reader, dis)) {
                if (dis) {
                    notice_set(&s_note, NoticeLevel::Ok, "{}개 종을 바꿨습니다",
                               dg.targets);
                } else {
                    notice_set(&s_note, NoticeLevel::Ok, "원래대로 되돌렸습니다");
                }
            } else {
                notice_set(&s_note, NoticeLevel::Bad,
                           "실패 - 표를 안 건드렸습니다");
            }
        }
        // **등록 채우기.** 동반자마다 "올려 둔 휠 칸" 이 있고, 드래곤은 그 자리가
        // 비어 있어 소환 판정의 마지막 관문에서 거부됐다(2026-09-16 실측).
        // 위에서 고른 종에 한해 그 자리를 채운다.
        static bool s_fill = game::wheel_fill_enabled();
        if (ImGui::Checkbox("휠 칸에 등록까지 채우기", &s_fill)) {
            game::wheel_fill_set_enabled(s_fill);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "동반자 항목의 '올려 둔 휠 칸' 이 비어 있으면 소환이 거부됩니다.\n"
                "드래곤이 그 상태였습니다 - 잠긴 게 아니라 칸에 안 올라가\n"
                "있었습니다. 위에서 고른 종만 채웁니다.");
        }
        // 등록을 채우면 제 칸에서 그대로 불리므로 옮길 이유가 없다. 옮기면
        // 카테고리가 달라져 채운 등록이 안 닿는다.
        static bool s_swap = game::disguise_swap_slot();
        if (ImGui::Checkbox("동반자 칸까지 옮기기", &s_swap)) {
            game::disguise_set_swap_slot(s_swap);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "끄면 장소 제한만 풀고 칸은 제자리에 둡니다.\n"
                "등록을 채웠다면 끄는 쪽이 맞습니다 - 옮기면 카테고리가\n"
                "달라져 채운 등록이 안 닿습니다.");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "CharacterInfo 의 **한 칸만** 바꿉니다:\n"
                "  +0xBE _mercenaryInfo  동반자 타입 행 (= 휠의 어느 칸인가)\n\n"
                "+0x6E _vehicleInfo 는 **건드리지 않습니다.** 그 칸은 규칙이 아니라\n"
                "무엇이 나오는가를 정합니다 - 말 값을 씌웠더니 드래곤 자리에서\n"
                "말이 나왔습니다(2026-09-15 실측).\n\n"
                "대상은 드래곤 슬롯·정비 슬롯이 가진 타입행 + 진짜 탈것 뿐입니다.\n"
                "반려 동물·용병은 탈것 규칙이 0xFFFF 라 절대 안 걸립니다.\n"
                "기증자는 **나는 것**에서 고릅니다(높이 상한이 있는 쪽).\n"
                "대상이 자기 탈것 규칙에 가진 지면 거리 검사도 같이 풉니다.\n"
                "바꾼 뒤 명부의 종류별 색인도 같이 맞춥니다.\n"
                "이걸 켜면 드래곤이 특수 탑승물 칸으로 가므로 **얹기는 필요 없습니다.**\n"
                "정적 표라 세이브에 안 남고, 끄면 원래 값으로 되돌립니다.");
        }
    }

    ImGui::TextColored(col::kWarn,
                       "실험입니다. 휠 목록에 뜨는 것과 실제로 소환·탑승이"
                       " 되는 것은 다를 수 있습니다.");
    ImGui::TextDisabled(
        "\"등록 안 됨\" 으로 막히는 종은 그 타입의 동반자를 하나 가지고 있어야"
        " 합니다 - 탈것·용병·캐릭터 창에서 종을 바꿔 만드십시오.");
    ImGui::TextDisabled(
        "정적 표에 쓰므로 세이브에 남지 않습니다 - 게임을 끄면 원래대로 돌아가고,"
        " 모드를 내릴 때도 되돌립니다.");
}

// 게이지 없이도 그리는 절들. **스탯 게이지와 아무 상관이 없다** - 결속·지식은 지식
// 컴포넌트만 있으면 되고, 관문은 코드 패치라, 휠은 정적 표라 아무것도 필요 없다.
// 그래서 게이지를 못 잡은 상태에서도 그린다.
void draw_skill_sections(const mem::Reader& reader) {
    draw_skill_bond(reader);
    draw_skill_gates();
    draw_elements(reader);
    draw_vehicle_wheel(reader);
    draw_knowledge(reader);
}

}  // namespace

void draw_player_panel(bool* open) {
    if (!begin_window(Win::Player, open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    if (!game::player_ready()) {
        // **여기서 통째로 돌아가면 안 된다.** 예전에는 그랬고, 그래서 세이브를
        // 불러 게이지를 다시 잡는 동안 지식 표를 열 방법이 아예 없었다
        // (사용자 화면 확인 2026-09-13). 게이지에 안 매달리는 절들은 그려 준다.
        ImGui::TextDisabled("월드에 들어가면 스탯 게이지를 잡습니다 (자동).");
        ImGui::TextDisabled("장비가 읽힌 뒤에 활성화됩니다.");
        ImGui::Separator();
        draw_skill_sections(reader);
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

    draw_skill_sections(reader);
    ImGui::End();
}

}  // namespace cdtb::render
