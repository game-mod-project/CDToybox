#include "render/ui_persist.h"
#include "render/vehicle_panel.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "game/actors.h"
#include "game/companion.h"
#include "game/callgate.h"
#include "game/grant.h"
#include "game/knowledge.h"
#include "game/mountslot.h"
#include "game/mountvital.h"
#include "game/player.h"
#include "game/reserveslot.h"
#include "game/towngate.h"
#include "game/wheelfill.h"
#include "mem/reader.h"
#include "render/colors.h"
#include "render/layout.h"
#include "render/notice.h"
#include "render/overlay.h"

namespace cdtb::render {

void draw_vehicle_panel(bool* open) {
    if (!begin_window(Win::Vehicle, open)) {
        ImGui::End();
        return;
    }
    const mem::LocalReader reader;

    static Notice s_note;
    const game::WheelState w = game::wheel_state(reader);
    if (!w.ready) {
        ImGui::TextDisabled("%s", w.note[0] != 0
                                      ? w.note
                                      : "월드에 들어가면 예약 슬롯 표를 잡습니다.");
        // **End() 를 빼먹으면 ImGui 가 "Missing End()" 를 화면에 띄운다.**
        // 이 갈래는 예약 슬롯 표를 못 잡았을 때만 도는데, 2944 에서 매니저
        // 전역이 낡으면서 처음으로 돌았다 - 갱신이 잠자던 버그를 깨웠다
        // (사용자 보고 2026-09-18).
        ImGui::End();
        return;
    }

    // ------------------------------------------------------------ 왜 막히는가
    //
    // 조건은 `gamedata/failmessageinfo` 에 있고 **조건마다 화면 문구가 다르다.**
    // 그래서 원인을 메모리로 캘 것 없이 문구만 읽으면 갈린다(2026-09-16 실측,
    // `specs/2026-09-16-vehicle-place-and-dismount.md` §5-0). 사용자 화면으로
    // 확인된 것:
    //
    //   "마을에서는 해당 탑승물을 호출할 수 없습니다"        -> IsInTown()
    //   "마을에서는 … 탑승할 수 없어 자동으로 하차합니다"     -> IsInTown()
    //   "A.T.A.G. 진입 불가 지역입니다"(지도의 붉은 구역)
    //        -> IsVehicleAllowedInEnteredRegion(Vehicle_WarMachine)
    //
    // 고도 조건(`CheckVehicleAllowableHeight`)은 **안 걸린다** - 걸렸다면 문구가
    // 달랐을 것이다. 아래 "호출 장소 제한" 은 그 고도·지면 쪽이라 마을과 무관하다.
    if (collapsing_header("veh.why", "왜 막히는가")) {
        ImGui::TextWrapped(
            "마을에서 못 부르는 것도, 타고 들어가면 내려지는 것도 같은 조건"
            " IsInTown() 입니다. 지도의 붉은 구역은 그것과 별개인 '진입 불가"
            " 지역' 이고 조건이 다릅니다.");
        ImGui::TextDisabled("드래곤은 마을이라도 '길 위 20 이상' 이면 예외입니다");
        ImGui::TextDisabled("A.T.A.G. 는 그 예외가 없어 더 빡빡합니다");
        ImGui::TextDisabled(
            "IsInTown() 은 지금 겹쳐 들어와 있는 구역 중 하나라도"
            " RegionInfo._isTown 이 켜져 있으면 참입니다 - 아래에서 그것을 끕니다");
        ImGui::TextColored(col::kWarn,
                           "붉은 구역(진입 불가)은 아직 푸는 기능이 없습니다.");
    }

    // ---------------------------------------------------------------- 마을
    //
    // `IsInTown()` 이 무엇을 읽는지 실행 파일에서 전부 떴다(2026-09-17,
    // specs/2026-09-16-vehicle-place-and-dismount.md §5-1-1). 재료는 정적
    // 표의 `RegionInfo._isTown` 이고, 그것을 0 으로 두면 조건이 그 자리에서
    // 거짓이 된다 - 사용자 실측으로 마을에서 소환·탑승이 됐다.
    if (collapsing_header("veh.town", "마을 (소환 · 강제 하차)")) {
        const game::TownGateState tg =
            game::town_gate_state(reader, game::player_char());
        if (!tg.ready) {
            ImGui::TextDisabled("%s", tg.note[0] != 0
                                          ? tg.note
                                          : "구역 표를 아직 못 잡았습니다.");
        } else {
            ImGui::TextDisabled("구역 표 %d행 · 마을 %d행 · 탈것 달리기 제한 %d행",
                                tg.rows, tg.towns, tg.runlimits);
            if (tg.here_rows > 0) {
                if (tg.here_town) {
                    ImGui::TextColored(col::kWarn, "지금 여기: 마을입니다%s%s",
                                       tg.here_name[0] != 0 ? " - " : "",
                                       tg.here_name);
                } else {
                    ImGui::TextDisabled("지금 여기: 마을 아닙니다 (겹친 구역 %d개)",
                                        tg.here_rows);
                }
            }
            bool town_on = tg.on;
            if (ImGui::Checkbox("마을 제한 풀기", &town_on)) {
                if (game::town_gate_free(reader, town_on)) {
                    if (town_on) {
                        notice_set(&s_note, NoticeLevel::Ok,
                                   "마을 {}행을 풀었습니다", tg.towns);
                    } else {
                        notice_set(&s_note, NoticeLevel::Ok, "마을 판정을 되돌렸습니다");
                    }
                } else {
                    notice_set(&s_note, NoticeLevel::Bad,
                               "실패 - 표를 안 건드렸습니다");
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "RegionInfo 의 _isTown(+0x75)과 _limitVehicleRun(+0x74)을\n"
                    "0 으로 둡니다. IsInTown() 이 읽는 바로 그 칸입니다.\n"
                    "세이브에는 안 남고 실행할 때마다 다시 걸어야 합니다.\n\n"
                    "**범위가 넓습니다.** 현상금·상점·NPC 일과도 같은 판정을\n"
                    "씁니다. 부를 때만 켜고 곧바로 끄시는 편이 안전합니다.");
            }
            if (tg.town_counter > 0) {
                ImGui::TextColored(col::kWarn,
                                   "살아있는 마을 칸이 %d 입니다 - 표만으로는"
                                   " 안 풀리는 자리입니다",
                                   tg.town_counter);
            }
        }
    }

    // ---------------------------------------------------------------- 소환
    if (collapsing_header("veh.call", "소환 (호출 장소)")) {
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

        // 서 있는 자리를 보고 거부하는 층. 위 항목(표 값)과 달리 **게임 코드**를
        // 건드린다 - 판정이 월드 질의라 데이터로는 못 끈다(2026-09-18,
        // specs/2026-09-16-vehicle-place-and-dismount.md §5-1-2).
        ImGui::Separator();
        ImGui::TextDisabled("서 있는 자리로 막히는 것 (게임 코드에 씁니다)");
        game::callgate_probe();
        for (int g = 0; g < game::kCallGateCount; ++g) {
            const game::CallGateInfo ci = game::callgate_info(g);
            if (ci.unsupported) {
                ImGui::TextColored(col::kBad, "%s - 이 게임 빌드에서는 못 씁니다",
                                   ci.name);
                continue;
            }
            bool on = ci.on;
            ImGui::PushID(g);
            if (ImGui::Checkbox(ci.name, &on)) {
                const char* why = "";
                if (game::callgate_set(g, on, &why)) {
                    notice_set(&s_note, NoticeLevel::Ok, "{} {}", ci.name,
                               on ? "켰습니다" : "껐습니다");
                } else {
                    notice_set(&s_note, NoticeLevel::Bad, "{} 실패 - {}", ci.name,
                               why);
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n\n거부로 가는 분기 한 바이트를 건너뛰게"
                                  " 합니다(je -> jmp).\n모드를 내리면 되돌립니다.",
                                  ci.what);
            }
            ImGui::PopID();
        }
        ImGui::TextColored(col::kWarn,
                           "정말 못 서는 자리에서 부르면 탈것이 지형에 박히거나"
                           " 곧 사라질 수 있습니다");
    }

    // ---------------------------------------------------------------- 시간
    if (collapsing_header("veh.time", "시간 (쿨다운 · 강제 하차)")) {
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
    }

    // ------------------------------------------------- 휠 슬롯 등록 · 호출 지식
    //
    // 등록 표의 주인을 **직접** 잡는다(`[[플레이어+0x68]+0x110]`). 예전에는 조회
    // 함수를 후킹해 인자로 받았는데, 홀더에 그대로 달려 있다 - 그래서 게임이
    // 갱신돼 `dragondiag` 가 통째로 꺼져 있어도(2944 가 그 상태다) 여기는 산다.
    if (collapsing_header("veh.slot", "휠 슬롯 등록 · 호출 지식")) {
        // 드래곤 호출은 **지식 한 칸**이 준다(Knowledge_CallDragon 행 5025 ->
        // Skill_CallDragon). 소환이 막히던 진짜 관문 중 하나다.
        // **번호를 박지 않는다** - `call_knowledge` 가 이름으로 찾아 준다.
        // 원소 휠에서 번호를 잘못 골라 여러 번 헛돈 전례가 있다(STATUS §1.20).
        game::CallKnow ck[game::kCallKnowCount];
        if (game::call_knowledge(reader, ck)) {
            const game::CallKnow& kd = ck[game::kCallKnowDragon];
            if (kd.number >= 0) {
                ImGui::Text("%s (행 %d)", kd.label, kd.number);
                ImGui::SameLine();
                if (kd.level >= 1) {
                    ImGui::TextColored(col::kOk, "배움 (레벨 %d)", kd.level);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("되돌리기##know")) {
                        const game::KnowWrite w =
                            game::know_forget(reader, kd.number);
                        notice_set(&s_note, w.changed > 0 ? NoticeLevel::Ok
                                                          : NoticeLevel::Bad,
                                   "되돌림 realm {}개", w.changed);
                    }
                } else {
                    ImGui::TextColored(col::kWarn,
                                       "안 배움 - 호출 모션이 안 나갑니다");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("배우기##know")) {
                        const game::KnowWrite w =
                            game::know_learn(reader, kd.number, 1);
                        notice_set(&s_note, w.changed > 0 ? NoticeLevel::Ok
                                                          : NoticeLevel::Bad,
                                   "배움 realm {}개", w.changed);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s -> 붙을 스킬 %d.\n"
                                      "이 지식이 없으면 휠에서 눌러도 호출 모션\n"
                                      "자체가 안 나갑니다. 세이브에 남습니다.",
                                      kd.internal, kd.apply_skill);
                }
                ImGui::Separator();
            }
        }

        const std::uintptr_t pch = game::player_char();
        const game::SlotTable st = game::mount_slots(reader, pch);
        if (!st.ready) {
            ImGui::TextDisabled("%s", st.note);
        } else {
            ImGui::TextDisabled("칸 65535 는 '휠에 안 올라감' 입니다."
                                " 생명 -1 은 '게임이 정함' 이고 껍데기(1)보다 낫습니다.");
            for (const game::SlotCategory& c : st.cats) {
                const std::string title = game::slot_category_name(c.cat);
                if (!ImGui::TreeNode(title.c_str())) continue;
                for (const game::SlotEntry& e : c.entries) {
                    ImGui::PushID(static_cast<int>(c.cat) * 1000 + e.nth);
                    ImGui::Text("[%d] 종행 %-5u %-28s 칸 %-6u 생명 %-8d 성장 %d",
                                e.nth, e.species,
                                e.name.empty() ? "-" : e.name.c_str(),
                                e.slot, e.hp, e.grow);
                    ImGui::SameLine();
                    if (game::slot_registered(e.slot)) {
                        if (ImGui::SmallButton("해제")) {
                            if (game::mount_slot_set(reader, pch, c.cat, e.nth,
                                                     game::kSlotNone)) {
                                notice_set(&s_note, NoticeLevel::Ok, "해제했습니다");
                            } else {
                                notice_set(&s_note, NoticeLevel::Bad, "실패");
                            }
                        }
                    } else {
                        static int s_slot = 0;
                        ImGui::SetNextItemWidth(70.0f);
                        ImGui::InputInt("칸##set", &s_slot);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("등록")) {
                            if (s_slot < 0) s_slot = 0;
                            if (game::mount_slot_set(
                                    reader, pch, c.cat, e.nth,
                                    static_cast<std::uint16_t>(s_slot))) {
                                notice_set(&s_note, NoticeLevel::Ok,
                                           "칸 {} 에 올렸습니다", s_slot);
                            } else {
                                notice_set(&s_note, NoticeLevel::Bad, "실패");
                            }
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::PushID(static_cast<int>(c.cat));
                if (ImGui::SmallButton("추천대로 정리")) {
                    const int n = game::mount_slot_autofix(reader, pch, c.cat);
                    notice_set(&s_note, NoticeLevel::Ok, "{}개를 바꿨습니다", n);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "종마다 **하나만** 올리고 같은 종의 나머지는 내립니다.\n"
                        "올릴 칸은 그 종이 지금 쓰는 칸을 그대로 씁니다 -\n"
                        "아무도 안 올라가 있으면 건드리지 않습니다.\n\n"
                        "\"어느 쪽이 나은가\" 판정은 믿을 것이 못 됩니다.\n"
                        "값을 보고 직접 고르시는 편이 확실합니다.");
                }
                ImGui::PopID();
                ImGui::TreePop();
            }
        }
    }

    // ------------------------------------------------------- 체력 · 스태미나
    //
    // 탈것도 플레이어와 **같은 게이지 사슬**이다(2026-09-18 실측). 다만 액터에서
    // 곧장 닿는 배열은 **거울**이고, 진짜 값은 서버 realm 사본에 있다 - 거기까지
    // 써야 먹는다. 자세한 근거는 `game/mountvital.h`.
    if (collapsing_header("veh.vital", "체력 · 스태미나")) {
        // 권위 사본 찾기는 힙 전수라 분석 스레드가 **부탁받을 때만** 돈다.
        // 절을 펴면 한 번 부탁한다 - 안 부탁하면 거울에만 써서 곧 되돌아간다.
        static bool s_asked_auth = false;
        if (!s_asked_auth) {
            s_asked_auth = true;
            game::mount_authority_request_refresh();
        }
        // **살아있는 액터 목록은 부탁해야 걷는다**(`live_actors_tick` 은 요청
        // 플래그가 섰을 때만 돈다). 부탁하지 않으면 탈것이 눈앞에 서 있어도
        // 목록이 비어 "없습니다" 가 뜬다 - 실제로 그렇게 나왔다(2026-09-18).
        //
        // 걷기는 힙 전수라 비싸므로 **매 프레임 부탁하지 않는다.** 목록이 비어
        // 있고 세대가 우리가 마지막으로 부탁한 뒤로 바뀌었을 때만 다시 부탁한다.
        const std::vector<game::MountVital> mv = game::mount_vitals(reader);
        if (mv.empty()) {
            static std::uint64_t s_asked_gen = ~0ULL;
            const std::uint64_t gen = game::live_actors_generation();
            if (s_asked_gen != gen) {
                s_asked_gen = gen;
                game::live_actors_request_refresh();
            }
            ImGui::TextDisabled("살아있는 목록을 받아오는 중입니다…"
                                " (탈것을 부른 직후면 잠시 걸립니다)");
        } else {
            const game::MountPin pin = game::mount_pin_get();
            ImGui::TextDisabled("값은 1000배 척도입니다 (2,500,000 = 생명 2500)");
            for (const game::MountVital& m : mv) {
                ImGui::PushID(static_cast<int>(m.handle));
                if (!m.ok) {
                    ImGui::TextDisabled("%s - 게이지를 못 잡았습니다",
                                        m.name.c_str());
                    ImGui::PopID();
                    continue;
                }
                const bool pinned = (pin.handle == m.handle) &&
                                    game::mount_pin_active(pin);
                ImGui::Text("%s%s", m.name.c_str(), pinned ? "  [고정 중]" : "");
                ImGui::SameLine();
                ImGui::TextDisabled("체력 %lld / %lld · 스태미나 %lld / %lld",
                                    static_cast<long long>(m.hp_cur),
                                    static_cast<long long>(m.hp_max),
                                    static_cast<long long>(m.sta_cur),
                                    static_cast<long long>(m.sta_max));
                // **권위 사본을 못 찾았으면 미리 말한다.** 예전에는 "썼습니다"
                // 가 뜨고 1초 뒤 숫자가 되돌아가, 사용자가 왜인지 알 길이 없었다.
                if (m.authority == 0) {
                    // **흐린 글씨로 두면 안 된다.** 실제로 이 상태에서 쓰고는
                    // "썼습니다" 만 보고 탔다가 값이 되돌아간 일이 있었다
                    // (사용자 보고 2026-09-19). 눈에 띄는 색으로 낸다.
                    ImGui::TextColored(col::kWarn,
                                       "  ⚠ 진짜 값을 든 사본을 찾는 중입니다"
                                       " - 지금 쓰면 **타는 순간 되돌아갑니다**");
                }

                // 입력값은 대상마다 따로 기억한다 - 창을 오가도 안 섞이게.
                static std::map<std::uint32_t, std::array<int, 4>> s_edit;
                auto& box = s_edit[m.handle];
                if (box[0] == 0 && box[1] == 0 && box[2] == 0 && box[3] == 0) {
                    box = {static_cast<int>(m.hp_cur), static_cast<int>(m.hp_max),
                           static_cast<int>(m.sta_cur),
                           static_cast<int>(m.sta_max)};
                }
                ImGui::SetNextItemWidth(320.0f);
                ImGui::InputInt2("체력 현재/최대", box.data());
                ImGui::SetNextItemWidth(320.0f);
                ImGui::InputInt2("스태미나 현재/최대", box.data() + 2);

                game::MountPin want;
                want.handle = m.handle;
                want.hp_cur = box[0];
                want.hp_max = box[1];
                want.sta_cur = box[2];
                want.sta_max = box[3];

                if (ImGui::Button("적용 (한 번)")) {
                    if (!game::mount_vital_write(reader, m.handle, want)) {
                        notice_set(&s_note, NoticeLevel::Bad,
                                   "실패 - 아무것도 안 썼습니다");
                    } else if (m.authority == 0) {
                        // **성공이라고 말하면 안 된다.** 거울에만 쓴 것은
                        // 타는 순간 되돌아간다 - 그것을 "썼습니다" 로 알렸다가
                        // 사용자가 왜 되돌아가는지 몰라 헤맸다(2026-09-19).
                        notice_set(&s_note, NoticeLevel::Warn,
                                   "{} - 보이는 값만 바꿨습니다. 진짜 사본을"
                                   " 아직 못 찾아 **타면 되돌아갑니다**",
                                   m.name);
                    } else {
                        notice_set(&s_note, NoticeLevel::Ok, "{} 에 썼습니다",
                                   m.name);
                    }
                }
                ImGui::SameLine();
                bool on = pinned;
                if (ImGui::Checkbox("고정", &on)) {
                    if (on) {
                        game::mount_pin_set(want);
                    } else {
                        game::mount_pin_clear();
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "게임은 이 값을 두 벌 들고 있고, 눈에 보이는 쪽은"
                        " 거울입니다.\n"
                        "거울에만 쓰면 회복 틱(1~2초)에 곧바로 되돌아갑니다 -"
                        " 그래서\n"
                        "진짜 값을 든 사본까지 함께 씁니다.\n\n"
                        "고정은 그 위에 얹는 보험입니다(피해를 받는 동안 유지).\n"
                        "한 마리만 걸리고, 다른 것을 켜면 옮겨갑니다. 대상은"
                        " 주소가\n"
                        "아니라 핸들로 들고 있어서 탈것이 사라지면 조용히"
                        " 건너뜁니다.");
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::TextDisabled("체력·스태미나만 건드립니다. 나머지 게이지는"
                                " 뜻을 확인하지 않아 그대로 둡니다.");
            // 탈것을 새로 부르거나 돌려보낸 뒤 목록을 맞추는 길.
            if (ImGui::SmallButton("목록 새로 고침")) {
                game::live_actors_request_refresh();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("진짜 값 사본 다시 찾기")) {
                game::mount_authority_request_refresh();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "탈것을 새로 부르면 사본도 새로 생깁니다. 위에 ⚠ 가 뜨면"
                    " 눌러 주십시오.\n"
                    "힙을 통째로 훑어 **십수 초** 걸리고, 분석 스레드에서 도므로"
                    " 게임은 안 멈춥니다.");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(짝지은 것 %zu)",
                                game::mount_authority_count());
        }
    }

    // ---------------------------------------------------------------- 조사용
    //
    // **평소에는 전부 끈다.** 드래곤이 풀리기 전에 하나씩 눌러 보려고 넣은
    // 것들이고, 지금은 켤 이유가 없다. 지우지 않는 이유는 A.T.A.G. 를 메인 휠로
    // 올리는 길이 아직 이것뿐이고, 되짚을 때 자리를 다시 찾기 어렵기 때문이다.
    if (collapsing_header("veh.exp", "조사용 (평소에는 끕니다)")) {
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

    ImGui::End();
}

}  // namespace cdtb::render
