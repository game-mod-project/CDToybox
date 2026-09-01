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

TEST(cursor_guard_is_not_installed_before_install) {
    CHECK(!cdtb::input::cursor_guard_installed());
}

}  // namespace
