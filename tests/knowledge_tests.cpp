// 지식 표의 **순수 부분**. 선행 조건을 모으고 합치는 계산이 여기서 틀리면
// "무엇을 배워야 하는가" 를 틀리게 말하고, 그 답으로 게임 메모리를 쓴다.
#include <cstdint>
#include <vector>

#include "game/knowledge.h"
#include "harness.h"

using cdtb::game::know_mgr_sane;
using cdtb::game::know_need_add;
using cdtb::game::know_need_drop_satisfied;
using cdtb::game::know_need_sort;
using cdtb::game::know_record;
using cdtb::game::KnowNeed;

TEST(know_record_uses_the_measured_stride) {
    // 레코드 24바이트. 한 칸만 어긋나도 남의 지식 레벨을 쓴다.
    CHECK(cdtb::game::kKnowRecStride == 24);
    CHECK(know_record(0x1000, 0) == 0x1000);
    CHECK(know_record(0x1000, 1) == 0x1018);
    CHECK(know_record(0x1000, 10) == 0x1000 + 240);
    // 말이 안 되는 입력은 0 - 부르는 쪽이 0 을 쓰지 않게 한다.
    CHECK(know_record(0, 3) == 0);
    CHECK(know_record(0x1000, -1) == 0);
}

TEST(know_mgr_sane_refuses_what_a_drifted_rva_would_give) {
    // 게임이 갱신되면 고정 RVA 가 밀린다. 그때 읽히는 것은 대개 0 이거나 쓰레기다.
    CHECK(know_mgr_sane(3000, 0x7FF000000000ULL));
    CHECK(!know_mgr_sane(0, 0x7FF000000000ULL));       // 개수 0
    CHECK(!know_mgr_sane(-5, 0x7FF000000000ULL));      // 음수
    CHECK(!know_mgr_sane(999999, 0x7FF000000000ULL));  // 한도 초과
    CHECK(!know_mgr_sane(3000, 0));                    // 널 배열
    CHECK(!know_mgr_sane(3000, 0x400));                // 아래쪽 64KB
    CHECK(know_mgr_sane(cdtb::game::kKnowMaxCount, 0x10000));   // 경계는 받는다
    CHECK(!know_mgr_sane(cdtb::game::kKnowMaxCount + 1, 0x10000));
}

TEST(know_need_add_keeps_the_highest_required_level) {
    // 같은 지식을 여러 노드가 요구하면 **가장 높은 요구 레벨**을 남겨야 한다.
    // 낮은 쪽을 남기면 "배웠는데 아직 안 풀린다" 가 된다.
    std::vector<KnowNeed> v;
    know_need_add(&v, 100, 1, false);
    know_need_add(&v, 100, 3, false);
    know_need_add(&v, 100, 2, false);
    CHECK(v.size() == 1);
    CHECK(v[0].number == 100);
    CHECK(v[0].need_level == 3);
    CHECK(v[0].wanted_by == 3);
    CHECK(!v[0].any_of);
}

TEST(know_need_add_remembers_that_something_was_an_any_of_list) {
    // "중 하나" 로 한 번이라도 나왔으면 표시를 남긴다 - 그 지식은 **안 배워도**
    // 같은 목록의 다른 것으로 조건이 풀릴 수 있다는 뜻이다.
    std::vector<KnowNeed> v;
    know_need_add(&v, 7, 1, false);
    CHECK(!v[0].any_of);
    know_need_add(&v, 7, 1, true);
    CHECK(v[0].any_of);
    know_need_add(&v, 7, 1, false);
    CHECK(v[0].any_of);   // 한 번 서면 안 내린다
}

TEST(know_need_add_rejects_nonsense_and_floors_the_level) {
    std::vector<KnowNeed> v;
    know_need_add(nullptr, 1, 1, false);   // 널이어도 안 죽는다
    know_need_add(&v, -1, 1, false);
    CHECK(v.empty());
    // 요구 레벨 0 은 1 로 올린다 - 레벨 0 은 "미습득" 이라 조건이 될 수 없다.
    know_need_add(&v, 5, 0, false);
    CHECK(v.size() == 1);
    CHECK(v[0].need_level == 1);
}

TEST(know_need_drop_satisfied_keeps_only_what_is_still_missing) {
    std::vector<KnowNeed> v;
    know_need_add(&v, 1, 2, false);
    know_need_add(&v, 2, 1, false);
    know_need_add(&v, 3, 5, false);
    v[0].have_level = 2;   // 딱 맞다 - 뺀다
    v[1].have_level = 0;   // 모자라다 - 남긴다
    v[2].have_level = 9;   // 넘는다 - 뺀다
    know_need_drop_satisfied(&v);
    CHECK(v.size() == 1);
    CHECK(v[0].number == 2);
}

TEST(know_need_sort_is_a_total_order) {
    // 화면이 매 프레임 다시 그리므로 같은 입력이면 같은 순서여야 한다.
    std::vector<KnowNeed> v;
    know_need_add(&v, 30, 1, false);
    know_need_add(&v, 10, 1, false);
    know_need_add(&v, 20, 1, false);
    know_need_add(&v, 20, 1, false);   // 20 이 두 번 요구된다
    know_need_sort(&v);
    CHECK(v.size() == 3);
    CHECK(v[0].number == 20);   // 요구 수가 많은 것이 먼저
    CHECK(v[0].wanted_by == 2);
    CHECK(v[1].number == 10);   // 같은 요구 수면 번호 순
    CHECK(v[2].number == 30);
}

TEST(the_measured_offsets_are_pinned) {
    // 실행 파일에서 직접 읽어 확인한 값들이다. 바뀌면 여기서 걸린다.
    //   매니저 전역 RVA 0x06C2E2D8 (조회 함수 RVA 0x003C1C70 이 쓰는 그 전역)
    //   판정 한 줄: RVA 0x0208BC20  cmp dword [r10+rcx*8], eax  (41 39 04 CA)
    CHECK(cdtb::game::kKnowMgrGlobalRva == 0x06C2E2D8);
    CHECK(cdtb::game::kKnowMgrCount == 0x08);
    CHECK(cdtb::game::kKnowMgrArray == 0x58);
    CHECK(cdtb::game::kKnowCompData == 0x18);
    CHECK(cdtb::game::kKnowCompCount == 0x20);
    CHECK(cdtb::game::kKnowRecLevel == 0x00);
    CHECK(cdtb::game::kKnowRecFlag == 0x10);
    CHECK(cdtb::game::kInfoLevels == 0x88);
    CHECK(cdtb::game::kLevelStride == 0xE8);
    CHECK(cdtb::game::kLearnFromStride == 0x58);
    CHECK(cdtb::game::kLearnFromTag == 0x44);
    CHECK(cdtb::game::kLearnFromNeed == 0x20);
    CHECK(cdtb::game::kNeedStride == 8);
    CHECK(cdtb::game::kTagSkillTree == 3);
    CHECK(cdtb::game::kModeAnyOf == 1);
}

TEST(know_auto_upsert_never_lowers_a_remembered_level) {
    // **낮추면 안 된다.** 낮은 값으로 덮으면 재적용이 이미 올려 둔 레벨을 되돌리려
    // 들고, know_learn 이 "이미 그 레벨 이상" 으로 건너뛰어 조용히 굳는다.
    std::vector<cdtb::game::KnowWant> v;
    cdtb::game::know_auto_upsert(&v, 100, 1);
    CHECK(v.size() == 1);
    CHECK(v[0].number == 100);
    CHECK(v[0].level == 1);
    cdtb::game::know_auto_upsert(&v, 100, 3);
    CHECK(v.size() == 1);
    CHECK(v[0].level == 3);
    cdtb::game::know_auto_upsert(&v, 100, 2);   // 낮추기 시도
    CHECK(v[0].level == 3);
}

TEST(know_auto_upsert_rejects_nonsense) {
    std::vector<cdtb::game::KnowWant> v;
    cdtb::game::know_auto_upsert(nullptr, 1, 1);   // 널이어도 안 죽는다
    cdtb::game::know_auto_upsert(&v, -1, 1);
    cdtb::game::know_auto_upsert(&v, 1, 0);        // 레벨 0 = 미습득, 기억할 것이 없다
    cdtb::game::know_auto_upsert(&v, 1, -5);
    CHECK(v.empty());
    cdtb::game::know_auto_upsert(&v, 0, 1);        // 번호 0 은 유효하다
    CHECK(v.size() == 1);
}

TEST(know_auto_upsert_keeps_separate_numbers_apart) {
    std::vector<cdtb::game::KnowWant> v;
    cdtb::game::know_auto_upsert(&v, 10, 2);
    cdtb::game::know_auto_upsert(&v, 20, 1);
    cdtb::game::know_auto_upsert(&v, 10, 5);
    CHECK(v.size() == 2);
    CHECK(v[0].number == 10 && v[0].level == 5);
    CHECK(v[1].number == 20 && v[1].level == 1);
}
