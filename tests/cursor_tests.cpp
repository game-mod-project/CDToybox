#include "harness.h"
#include "input/cursor.h"

namespace {

using cdtb::input::cursor_show_delta;

// ShowCursor 는 카운터다. 0 이상이면 커서가 보인다. 게임이 옮겨 둔 값에서 우리가
// 원하는 상태로 맞추려면 몇 번 불러야 하는지 계산해야 한다(게임은 오버레이가 열린
// 동안 ShowCursor(FALSE) 를 연타하지 않는다 - OS 커서가 멀쩡히 보인다, cursor.h).

TEST(cursor_delta_is_zero_when_already_at_target) {
    CHECK_EQ(cursor_show_delta(0, 0), 0);
    CHECK_EQ(cursor_show_delta(-1, -1), 0);
}

TEST(cursor_delta_counts_up_to_reach_visible) {
    // -3 에서 0 으로 가려면 ShowCursor(TRUE) 를 세 번.
    CHECK_EQ(cursor_show_delta(-3, 0), 3);
}

TEST(cursor_delta_counts_down_to_hide) {
    // 0 에서 -1 로 가려면 ShowCursor(FALSE) 를 한 번. 음수로 돌려준다.
    CHECK_EQ(cursor_show_delta(0, -1), -1);
    CHECK_EQ(cursor_show_delta(2, -1), -3);
}

// ShowCursor 훅이 고정값을 돌려주다 게임을 멎게 한 적이 있다
// (`while (ShowCursor(TRUE) < 0);`). 남은 훅도 같은 식으로 게임을
// 가둘 수 있으므로, 한 프레임에 막는 횟수에 상한을 둔다.
TEST(cursor_blocks_until_budget_runs_out) {
    CHECK(cdtb::input::cursor_should_block(0, 500));
    CHECK(cdtb::input::cursor_should_block(499, 500));
}

TEST(cursor_stops_blocking_past_budget) {
    CHECK(!cdtb::input::cursor_should_block(500, 500));
    CHECK(!cdtb::input::cursor_should_block(9999, 500));
}

TEST(cursor_guard_is_not_installed_before_install) {
    CHECK(!cdtb::input::cursor_guard_installed());
}

// 닫을 때 가두기: 게임이 이번·직전 프레임에도 가두려 했으면(마우스룩) 그것을 다시
// 걸고, 그보다 오래됐거나 없거나 풀기였으면 푼다 - 오버레이를 연 채 인벤을 열면 게임이
// 가두기를 그만두므로(또는 nullptr 로 풀므로) 풀려야 한다. 시계가 아니라 프레임 번호라
// "인벤 열기 → 오버레이 닫기" 두 키 입력 사이에 옛 사각형이 다시 걸릴 창이 없다.
using cdtb::input::ClipRestore;
using cdtb::input::cursor_clip_restore;
using cdtb::input::cursor_restore_count;

TEST(cursor_clip_restore_reapplies_a_request_from_this_or_last_frame) {
    CHECK(cursor_clip_restore(true, false, 10, 10) == ClipRestore::Reapply);
    CHECK(cursor_clip_restore(true, false, 10, 11) == ClipRestore::Reapply);   // 직전 프레임
    CHECK(cursor_clip_restore(true, false, 10, 12, 2) == ClipRestore::Reapply);
}

TEST(cursor_clip_restore_releases_when_stale_or_absent) {
    CHECK(cursor_clip_restore(false, false, 0, 10) == ClipRestore::Release);
    CHECK(cursor_clip_restore(true, false, 10, 12) == ClipRestore::Release);   // 두 프레임 전
    CHECK(cursor_clip_restore(true, false, 10, 500) == ClipRestore::Release);
    CHECK(cursor_clip_restore(true, false, 12, 10) == ClipRestore::Release);   // 번호 되감김
    CHECK(cursor_clip_restore(true, false, 10, 13, 2) == ClipRestore::Release);
}

// 인벤이 ClipCursor(nullptr) 로 풀었으면 아무리 방금이라도 다시 걸지 않는다.
TEST(cursor_clip_restore_releases_when_the_last_request_was_null) {
    CHECK(cursor_clip_restore(true, true, 10, 10) == ClipRestore::Release);
    CHECK(cursor_clip_restore(true, true, 10, 11) == ClipRestore::Release);
}

// 닫을 때 카운터: 열 때 기억한 값에 그 사이 게임이 바꾼 만큼을 얹는다.
TEST(cursor_restore_count_keeps_the_games_change) {
    CHECK_EQ(cursor_restore_count(-1, 0), -1);   // 게임이 안 건드림 → 열기 전으로
    CHECK_EQ(cursor_restore_count(-1, 1), 0);    // 인벤을 열며 +1 → 보인 채로
    CHECK_EQ(cursor_restore_count(0, -2), -2);
    // 인벤을 먼저 연 채(0) 열고, 연 채 인벤을 닫아 게임이 -1 로 내림 → 숨김. 열린 동안
    // 카운터를 0 으로 되돌리면 이 값이 0 이 되어 닫은 뒤 커서가 남는다(리뷰 F-1).
    CHECK_EQ(cursor_restore_count(0, -1), -1);
}

}  // namespace
