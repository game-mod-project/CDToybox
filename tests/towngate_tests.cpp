// 마을 판정(IsInTown) 풀기의 순수 부분. 게임 없이 전부 태운다.
//
// 실측 값(2026-09-17, 런타임 표 1007행을 통째로 읽어 확인):
//   _isTown=1              172행
//   _limitVehicleRun=1      15행
//   그래서 자국은 187개 - kTownPatchMax(1024) 안에 넉넉히 들어간다.
#include "game/towngate.h"
#include "harness.h"

namespace {

using cdtb::game::kRiIsTown;
using cdtb::game::kRiLimitVehicleRun;
using cdtb::game::region_count_plausible;
using cdtb::game::region_row_in_table;
using cdtb::game::town_row_gated;
using cdtb::game::town_row_plan;
using cdtb::game::TownPatch;

TEST(town_row_gated_picks_only_rows_that_block) {
    CHECK(!town_row_gated(0, 0));   // 들판 - 건드릴 것이 없다
    CHECK(town_row_gated(1, 0));    // 마을
    CHECK(town_row_gated(0, 1));    // 탈것 달리기 제한만 (성당·저택)
    CHECK(town_row_gated(1, 1));    // 둘 다 (Region_Node_Dem_DemenissCathedral)
}

TEST(town_row_gated_treats_any_nonzero_as_set) {
    // 표가 bool 을 1 로만 쓴다는 보장이 없다 - 0 이 아니면 켜진 것으로 본다.
    CHECK(town_row_gated(2, 0));
    CHECK(town_row_gated(0, 0xFF));
}

TEST(town_row_plan_emits_nothing_for_a_clean_row) {
    TownPatch out[2];
    CHECK_EQ(town_row_plan(7, 0, 0, out), 0);
}

TEST(town_row_plan_keeps_the_original_value_for_restore) {
    TownPatch out[2];
    CHECK_EQ(town_row_plan(348, 1, 1, out), 2);
    // 마을 쪽이 먼저다 - 상한에 걸려 잘려도 마을이 살아남아야 한다.
    CHECK_EQ(static_cast<int>(out[0].off), static_cast<int>(kRiIsTown));
    CHECK_EQ(static_cast<int>(out[1].off),
             static_cast<int>(kRiLimitVehicleRun));
    CHECK_EQ(static_cast<int>(out[0].row), 348);
    CHECK_EQ(static_cast<int>(out[1].row), 348);
    // 되돌릴 때 쓸 값은 **읽은 원본**이지 1 이 아니다.
    CHECK_EQ(static_cast<int>(out[0].old), 1);
    CHECK_EQ(static_cast<int>(out[1].old), 1);
}

TEST(town_row_plan_emits_only_the_field_that_is_set) {
    TownPatch out[2];
    CHECK_EQ(town_row_plan(37, 0, 3, out), 1);
    CHECK_EQ(static_cast<int>(out[0].off),
             static_cast<int>(kRiLimitVehicleRun));
    CHECK_EQ(static_cast<int>(out[0].old), 3);

    CHECK_EQ(town_row_plan(4, 5, 0, out), 1);
    CHECK_EQ(static_cast<int>(out[0].off), static_cast<int>(kRiIsTown));
    CHECK_EQ(static_cast<int>(out[0].old), 5);
}

TEST(town_row_plan_survives_a_null_out) {
    CHECK_EQ(town_row_plan(0, 1, 1, nullptr), 0);
}

TEST(region_count_plausible_rejects_empty_and_absurd_tables) {
    CHECK(!region_count_plausible(0));
    CHECK(region_count_plausible(1));
    CHECK(region_count_plausible(1007));   // 실측 행 수
    CHECK(region_count_plausible(cdtb::game::kRegionMaxRows));
    CHECK(!region_count_plausible(cdtb::game::kRegionMaxRows + 1));
    CHECK(!region_count_plausible(0xFFFFFFFFu));
}

TEST(region_row_in_table_bounds_the_index) {
    // 액터가 건네는 u16 은 _key 가 아니라 **행 번호**다(조회 함수가 그대로
    // 배열 첨자로 쓴다). 표 밖이면 버려야 남의 메모리를 안 읽는다.
    CHECK(region_row_in_table(0, 1007));
    CHECK(region_row_in_table(1006, 1007));
    CHECK(!region_row_in_table(1007, 1007));
    CHECK(!region_row_in_table(44045, 1007));   // _key 를 행으로 잘못 쓴 경우
}

TEST(region_offsets_match_the_measured_record_layout) {
    // fields.py 로 뽑은 자리. 여기가 밀리면 엉뚱한 칸을 0 으로 만든다.
    CHECK_EQ(static_cast<int>(kRiLimitVehicleRun), 0x74);
    CHECK_EQ(static_cast<int>(kRiIsTown), 0x75);
    CHECK_EQ(static_cast<int>(cdtb::game::kRiStringKey), 0x08);
    CHECK_EQ(static_cast<int>(cdtb::game::kListStride), 0x0C);
}

TEST(patch_budget_covers_the_measured_table) {
    // 실측 172 + 15 = 187. 상한이 그보다 작으면 마을이 남는다.
    CHECK(cdtb::game::kTownPatchMax >= 187 * 2);
}

}  // namespace
