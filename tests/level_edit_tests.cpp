#include "harness.h"
#include "render/level_edit.h"

using cdtb::render::LevelEdit;
using cdtb::render::level_edit_sync;

TEST(level_edit_seeds_from_the_game_value_on_first_sight) {
    LevelEdit e;
    level_edit_sync(&e, 25);
    CHECK_EQ(e.v, 25);
    CHECK_EQ(e.seen, 25);
}

TEST(level_edit_keeps_what_the_user_typed_while_the_game_value_stands) {
    LevelEdit e;
    level_edit_sync(&e, 2);
    e.v = 40;                 // 사용자가 40 을 넣었다
    level_edit_sync(&e, 2);   // 다음 프레임 - 게임 값은 그대로다
    CHECK_EQ(e.v, 40);
}

TEST(level_edit_follows_the_game_value_when_it_changes) {
    // "전부 연마 최대" 뒤의 그 자리다 - 메모리는 40 인데 칸이 2 를 들고 있던 결함.
    LevelEdit e;
    level_edit_sync(&e, 2);
    level_edit_sync(&e, 40);
    CHECK_EQ(e.v, 40);
    CHECK_EQ(e.seen, 40);
    // 게임이 값을 낮춰도(내구도 감소) 따라간다.
    level_edit_sync(&e, 37);
    CHECK_EQ(e.v, 37);
}

TEST(level_edit_seeds_zero_and_ignores_a_null_state) {
    LevelEdit e;
    level_edit_sync(&e, 0);   // seen 기본값 -1 과 달라 씨앗이 들어간다
    CHECK_EQ(e.v, 0);
    CHECK_EQ(e.seen, 0);
    level_edit_sync(nullptr, 5);   // 죽지 않는다
}
