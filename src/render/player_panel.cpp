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
#include "game/wanted.h"
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
        // 라벨이 비면 세 라디오가 **같은 ID** 를 갖는다 - ImGui 가 화면에
        // "3 visible items with conflicting ID" 를 띄우고, 하나를 누르면
        // 엉뚱한 것이 눌린 것처럼 보인다(사용자 보고 2026-09-18). 칸 번호로
        // ID 를 갈라 준다. 라벨은 그대로 비워 두고 옆의 Text 가 이름을 낸다.
        ImGui::PushID(i);
        ImGui::RadioButton("", &s_slot, i);
        ImGui::PopID();
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


// 게이지 없이도 그리는 절들. **스탯 게이지와 아무 상관이 없다** - 결속·지식은 지식
// 컴포넌트만 있으면 되고, 관문은 코드 패치라, 휠은 정적 표라 아무것도 필요 없다.
// 그래서 게이지를 못 잡은 상태에서도 그린다.
// 수배(범죄수치). 게임 자신의 개발용 요청 메시지를 그대로 부른다 -
// 새 경로를 만들지 않는다(game/wanted.h).
//
// 아직 확인 안 된 것이 둘이라 화면이 그것을 숨기지 않는다. 대상 핸들이
// 정말 플레이어인지, 플래그가 무슨 뜻인지 모른다. 그래서 둘 다 고칠 수
// 있게 두고, 보낸 값을 로그에 남긴다.
void draw_wanted(const mem::Reader& reader) {
    if (!collapsing_header("player.wanted", "범죄수치 (수배)")) return;

    static Notice s_note;
    static int s_handle = static_cast<int>(game::kAssumedPlayerHandle);
    static int s_flag = 0;

    // 메시지를 못 잡았어도 벌금 칸은 그려야 한다. 요청하신 기능은
    // 벌금 쪽이고, 그것은 메시지가 아니라 제자리 쓰기라 메시지 해석에
    // 매달릴 이유가 없다.
    const bool msg_ok = game::wanted_ready();

    // --- 벌금 (요청하신 기능) ---
    //
    // 화면의 "데메니스 왕국 / 벌금 N.NN" 을 그대로 읽고 쓴다. 제자리
    // 쓰기이고, 쓰면 화면이 바로 따라온다(실측 2026-09-18).
    std::uint64_t raw = 0;
    if (game::bounty_read(reader, &raw)) {
        static double s_want = -1.0;
        const double now = game::bounty_from_raw(raw);
        if (s_want < 0.0) s_want = now;   // 처음엔 현재 값으로 맞춰 둔다

        ImGui::Text("현재 벌금  %.2f", now);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputDouble("설정값", &s_want, 1.0, 10.0, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("적용")) {
            // notice_set 의 형식 문자열은 컴파일 타임 상수여야 한다 -
            // 삼항으로 고르면 std::format_string 이 안 받는다.
            if (game::bounty_write(reader, game::bounty_to_raw(s_want))) {
                notice_set(&s_note, NoticeLevel::Ok,
                           "벌금을 {:.2f} 로 썼습니다", s_want);
            } else {
                notice_set(&s_note, NoticeLevel::Bad, "쓰지 못했습니다");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("0 으로 지우기")) {
            if (game::bounty_write(reader, 0)) {
                s_want = 0.0;
                notice_set(&s_note, NoticeLevel::Ok, "벌금을 지웠습니다");
            } else {
                notice_set(&s_note, NoticeLevel::Bad, "쓰지 못했습니다");
            }
        }
        ImGui::TextDisabled("상한 %.2f (게임이 그 위로 안 올라갑니다)",
                            game::bounty_from_raw(game::kBountyMaxRaw));
    } else if (game::wanted_component_ready()) {
        // 컴포넌트는 잡았는데 지역 데이터가 없다 = 지금 범죄 기록이 없다.
        // 여기서 "월드에 들어가면" 을 띄우면 영원히 안 바뀌는 것처럼 보인다.
        ImGui::TextDisabled("지금 범죄 기록이 없습니다 - 죄를 지으면 지역이"
                            " 생기고 그때 벌금이 읽힙니다.");
    } else {
        ImGui::TextDisabled("벌금을 아직 못 읽었습니다 - 월드에 들어가면"
                            " 저절로 잡습니다.");
    }

    ImGui::Separator();
    if (!msg_ok) {
        // 메시지 쪽은 부수 기능이다. 못 잡아도 벌금은 위에서 이미 된다.
        ImGui::TextDisabled("추격 끊기 메시지는 아직 못 잡았습니다.");
        notice_draw(s_note);
        return;
    }
    // 이름을 사실에 맞춘다. 처음엔 "수배 해제" 로 불렀는데, 눌러 보니
    // 지우는 것은 목격자·범죄 기록뿐이고 **벌금은 그대로였다**(사용자
    // 화면 확인 2026-09-18). 쫓기는 중에 추격을 끊는 용도다.
    ImGui::TextDisabled("아래는 벌금과 다른 것입니다 - 목격자와 범죄"
                        " 기록만 비웁니다.");
    ImGui::TextDisabled("메시지 ID %u", game::wanted_clear_message_id());

    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("대상 핸들", &s_handle, 0, 0,
                    ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::TextDisabled("(기본 0x%08X = 플레이어로 추정)",
                        game::kAssumedPlayerHandle);

    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputInt("플래그", &s_flag);
    ImGui::SameLine();
    ImGui::TextDisabled("(뜻 미확인 - 0 과 1 을 견줘 보십시오)");

    // 벌금을 0 으로 만들어도 지도에 지역 항목이 남는다(실측 2026-09-18).
    // 액수와 상태는 다른 것이고, 상태 쪽 뜻은 아직 모른다 - 그래서
    // 값을 쓸어 볼 수 있게 그대로 열어 둔다.
    static int s_state = 0;
    static int s_extra = 0;
    ImGui::SeparatorText("상태 바꾸기 (뜻 미확인 - 쓸어 보는 중)");
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("상태", &s_state);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("extra", &s_extra);
    ImGui::SameLine();
    if (ImGui::Button("상태 보내기")) {
        const bool ok = game::request_change_wanted_state(
            reader, static_cast<std::uint32_t>(s_handle),
            static_cast<std::uint8_t>(s_state),
            static_cast<std::uint8_t>(s_extra));
        if (ok) {
            notice_set(&s_note, NoticeLevel::Ok,
                       "상태 {}·{} 를 걸었습니다 - 지도를 다시 여십시오",
                       s_state, s_extra);
        } else {
            notice_set(&s_note, NoticeLevel::Bad,
                       "상태 요청을 걸지 못했습니다 (로그 참조)");
        }
    }
    ImGui::Separator();

    if (ImGui::Button("목격 상태 지우기")) {
        const bool ok = game::request_clear_wanted(
            reader, static_cast<std::uint32_t>(s_handle),
            static_cast<std::uint8_t>(s_flag));
        // "걸었다" 와 "먹었다" 는 다르다. 여기서 아는 것은 대기열에
        // 걸렸는지까지다 - 실제 결과는 화면과 로그로 본다.
        if (ok) {
            notice_set(&s_note, NoticeLevel::Ok,
                       "요청을 걸었습니다 - 화면과 로그를 보십시오");
        } else {
            notice_set(&s_note, NoticeLevel::Bad,
                       "요청을 걸지 못했습니다 (로그에 이유가 있습니다)");
        }
    }
    notice_draw(s_note);
}

void draw_skill_sections(const mem::Reader& reader) {
    draw_skill_bond(reader);
    draw_skill_gates();
    draw_elements(reader);
    draw_knowledge(reader);
    draw_wanted(reader);
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
