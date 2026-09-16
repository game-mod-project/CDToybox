#include <string>

#include "harness.h"
#include "render/ui_persist_state.h"

using cdtb::render::UiPersist;

// ------------------------------------------------------------ 헤더 펼침 상태
TEST(header_remembers_what_was_set) {
    UiPersist s;
    s.set_header_open("player.element", true);
    CHECK(s.header_open("player.element"));
}

TEST(header_unknown_key_is_collapsed) {
    UiPersist s;
    CHECK(!s.header_open("never.seen"));
}

// -------------------------------------------------------------- 창 열림 상태
// 창은 헤더와 달리 "기본값" 이 창마다 다르다(본창만 열림). 저장값이 없으면
// 부르는 쪽이 준 기본값을 그대로 돌려줘야 한다.
TEST(window_unknown_key_falls_back_to_given_default) {
    UiPersist s;
    CHECK(s.window_open("main", true));
    CHECK(!s.window_open("inventory", false));
}

TEST(window_set_wins_over_default) {
    UiPersist s;
    s.set_window_open("inventory", true);
    CHECK(s.window_open("inventory", false));
    s.set_window_open("inventory", false);
    CHECK(!s.window_open("inventory", true));
}

// ---------------------------------------------------------------- 저장·복원
TEST(serialize_then_parse_restores_both_kinds) {
    UiPersist a;
    a.set_header_open("player.element", true);
    a.set_header_open("grant.socket", false);
    a.set_window_open("inventory", true);
    a.set_window_open("log", false);

    UiPersist b;
    std::string text = a.serialize();
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        b.parse_line(std::string_view(text).substr(at, nl - at));
        at = nl + 1;
    }

    CHECK(b.header_open("player.element"));
    CHECK(!b.header_open("grant.socket"));
    CHECK(b.window_open("inventory", false));
    CHECK(!b.window_open("log", true));
}

// 보관함 세트 이름이 키로 들어가는데 그것은 사용자가 짓는다 - `=` 가 들어올
// 수 있다. 값은 마지막 `=` 뒤로 갈라야 그런 이름도 살아 돌아온다.
TEST(key_may_contain_equals_sign) {
    UiPersist a;
    a.set_header_open("stash.set.a=b", true);

    UiPersist b;
    b.parse_line(a.serialize().substr(0, a.serialize().size() - 1));
    CHECK(b.header_open("stash.set.a=b"));
}

// ini 는 사람이 고칠 수 있고 형식이 바뀔 수도 있다. 못 읽는 줄은 조용히
// 버리고 아무 상태도 건드리지 않는다.
TEST(garbage_lines_change_nothing) {
    UiPersist s;
    s.set_header_open("keep", true);
    for (const char* bad : {"", "H", "H=", "H=k=", "H=k=2", "X=k=1", "=k=1",
                            "H==1", "nonsense"}) {
        s.parse_line(bad);
    }
    CHECK(s.header_open("keep"));
    CHECK(!s.header_open("k"));
    CHECK(!s.window_open("k", false));
}
