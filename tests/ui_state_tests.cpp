#include <cstring>

#include "harness.h"
#include "render/notice_state.h"

using cdtb::render::Notice;
using cdtb::render::NoticeAge;
using cdtb::render::NoticeLevel;

// ---------------------------------------------------------------- 결과 줄
TEST(notice_empty_is_gone) {
    Notice n;
    CHECK(cdtb::render::notice_age(n, 100.0) == NoticeAge::Gone);
}

TEST(notice_put_stamps_time_and_level) {
    Notice n;
    cdtb::render::notice_put(&n, NoticeLevel::Bad, 12.5, "쓰기 실패");
    CHECK(n.level == NoticeLevel::Bad);
    CHECK_EQ(n.at, 12.5);
    CHECK(std::strcmp(n.text, "쓰기 실패") == 0);
}

TEST(notice_age_thresholds) {
    Notice n;
    cdtb::render::notice_put(&n, NoticeLevel::Ok, 10.0, "됐습니다");
    CHECK(cdtb::render::notice_age(n, 10.0) == NoticeAge::Fresh);
    CHECK(cdtb::render::notice_age(n, 19.9) == NoticeAge::Fresh);
    CHECK(cdtb::render::notice_age(n, 20.0) == NoticeAge::Faded);
    CHECK(cdtb::render::notice_age(n, 69.9) == NoticeAge::Faded);
    CHECK(cdtb::render::notice_age(n, 70.0) == NoticeAge::Gone);
    // 시계가 뒤로 가도(타이머 리셋) 방금 것으로 본다
    CHECK(cdtb::render::notice_age(n, 5.0) == NoticeAge::Fresh);
    // 문턱을 바꿔 부를 수 있다
    CHECK(cdtb::render::notice_age(n, 12.0, 1.0, 3.0) == NoticeAge::Faded);
    CHECK(cdtb::render::notice_age(n, 13.0, 1.0, 3.0) == NoticeAge::Gone);
}

TEST(notice_put_truncates_long_text) {
    Notice n;
    char longtext[400];
    std::memset(longtext, 'a', sizeof(longtext) - 1);
    longtext[sizeof(longtext) - 1] = 0;
    cdtb::render::notice_put(&n, NoticeLevel::Info, 1.0, longtext);
    CHECK_EQ(std::strlen(n.text), sizeof(n.text) - 1);
}

TEST(notice_clear_makes_it_gone) {
    Notice n;
    cdtb::render::notice_put(&n, NoticeLevel::Ok, 1.0, "x");
    cdtb::render::notice_clear(&n);
    CHECK(cdtb::render::notice_age(n, 1.0) == NoticeAge::Gone);
    CHECK_EQ(n.text[0], '\0');
}

#include "render/confirm_state.h"

using cdtb::render::ConfirmState;
using cdtb::render::ConfirmStep;

// ---------------------------------------------------------------- 2단 확인
TEST(confirm_idle_without_click) {
    ConfirmState s;
    CHECK(cdtb::render::confirm_step(&s, 7, false, 1.0) == ConfirmStep::Idle);
    CHECK_EQ(s.id, 0u);
}

TEST(confirm_arms_then_fires) {
    ConfirmState s;
    CHECK(cdtb::render::confirm_step(&s, 7, true, 1.0) == ConfirmStep::Armed);
    CHECK_EQ(s.id, 7u);
    CHECK(cdtb::render::confirm_step(&s, 7, false, 2.0) == ConfirmStep::Armed);
    CHECK(cdtb::render::confirm_step(&s, 7, true, 2.5) == ConfirmStep::Fired);
    // 발화하면 풀린다
    CHECK_EQ(s.id, 0u);
    CHECK(cdtb::render::confirm_step(&s, 7, false, 2.6) == ConfirmStep::Idle);
}

TEST(confirm_expires_after_window) {
    ConfirmState s;
    cdtb::render::confirm_step(&s, 7, true, 1.0);
    CHECK(cdtb::render::confirm_step(&s, 7, false, 3.9) == ConfirmStep::Armed);
    CHECK(cdtb::render::confirm_step(&s, 7, false, 4.0) == ConfirmStep::Idle);
    CHECK_EQ(s.id, 0u);
    // 만료 뒤 클릭은 다시 무장이지 발화가 아니다
    CHECK(cdtb::render::confirm_step(&s, 7, true, 4.1) == ConfirmStep::Armed);
}

TEST(confirm_other_button_disarms) {
    ConfirmState s;
    cdtb::render::confirm_step(&s, 7, true, 1.0);
    // 다른 버튼을 누르면 그쪽이 무장하고 앞의 것은 풀린다
    CHECK(cdtb::render::confirm_step(&s, 9, true, 1.5) == ConfirmStep::Armed);
    CHECK_EQ(s.id, 9u);
    CHECK(cdtb::render::confirm_step(&s, 7, false, 1.6) == ConfirmStep::Idle);
    CHECK(cdtb::render::confirm_step(&s, 7, true, 1.7) == ConfirmStep::Armed);
}

TEST(confirm_left_counts_down) {
    ConfirmState s;
    cdtb::render::confirm_step(&s, 7, true, 1.0);
    CHECK_EQ(cdtb::render::confirm_left(s, 7, 1.0), 3.0);
    CHECK_EQ(cdtb::render::confirm_left(s, 7, 3.5), 0.5);
    CHECK_EQ(cdtb::render::confirm_left(s, 7, 9.0), 0.0);
    CHECK_EQ(cdtb::render::confirm_left(s, 8, 1.0), 0.0);
}
