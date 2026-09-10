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
