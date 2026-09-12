#include <cstring>
#include <string>

#include "harness.h"
#include "render/confirm_state.h"
#include "render/notice_state.h"

using cdtb::render::ConfirmState;
using cdtb::render::ConfirmStep;
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
    // 시계가 뒤로 갔으면(오버레이 재초기화로 타이머 리셋) 옛 시계의 결과다 -
    // 방금 것으로 보면 at 초가 지날 때까지 새것처럼 남았다
    CHECK(cdtb::render::notice_age(n, 5.0) == NoticeAge::Gone);
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

TEST(notice_put_cuts_on_utf8_boundary) {
    // 199바이트 자리에서 한글(3바이트)이 걸리면 그 글자를 통째로 버린다
    std::string s;
    for (int i = 0; i < 70; ++i) s += "가";   // 210바이트
    Notice n;
    cdtb::render::notice_put(&n, NoticeLevel::Info, 1.0, s.c_str());
    std::size_t len = std::strlen(n.text);
    CHECK_EQ(len, static_cast<std::size_t>(198));   // 66글자
    // 끝이 온전한 한 글자다 - 마지막 세 바이트가 한글 한 자(1110xxxx 로 시작).
    // 마지막 바이트로는 못 본다 - 온전해도 이어지는 바이트(0x80)로 끝난다.
    CHECK((static_cast<unsigned char>(n.text[len - 3]) & 0xF0) == 0xE0);

    // 셋 중 둘이 들어온 경우(195 + "ab" + 가 = 200바이트)
    std::string t;
    for (int i = 0; i < 65; ++i) t += "가";
    t += "ab";
    t += "가";
    cdtb::render::notice_put(&n, NoticeLevel::Info, 1.0, t.c_str());
    len = std::strlen(n.text);
    CHECK_EQ(len, static_cast<std::size_t>(197));
    CHECK_EQ(n.text[len - 1], 'b');

    // ASCII 로 끝나면 199바이트 그대로
    std::string u;
    for (int i = 0; i < 66; ++i) u += "가";
    u += "ab";
    cdtb::render::notice_put(&n, NoticeLevel::Info, 1.0, u.c_str());
    CHECK_EQ(std::strlen(n.text), static_cast<std::size_t>(199));
}

TEST(notice_clear_makes_it_gone) {
    Notice n;
    cdtb::render::notice_put(&n, NoticeLevel::Ok, 1.0, "x");
    cdtb::render::notice_clear(&n);
    CHECK(cdtb::render::notice_age(n, 1.0) == NoticeAge::Gone);
    CHECK_EQ(n.text[0], '\0');
}

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

TEST(confirm_clock_rewind_disarms) {
    ConfirmState s;
    cdtb::render::confirm_step(&s, 7, true, 137.4);
    // ImGui 컨텍스트 재생성으로 GetTime 이 0 부터 다시 - 무장은 풀려야 한다
    CHECK(cdtb::render::confirm_step(&s, 7, true, 0.2) == ConfirmStep::Armed);
    CHECK_EQ(cdtb::render::confirm_left(s, 7, 0.2), 3.0);
}
