#include "harness.h"
#include "input/cursor.h"

namespace {

using cdtb::input::cursor_show_delta;

// ShowCursor 는 카운터다. 0 이상이면 커서가 보인다. 게임이 매 프레임
// ShowCursor(FALSE) 를 부르는 동안 우리가 원하는 상태로 맞추려면 몇 번
// 불러야 하는지 계산해야 한다.

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

}  // namespace
