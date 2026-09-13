// 로그 고리 버퍼와 로그 창 거르개. 둘 다 전역·ImGui·파일을 안 건드리는 조각으로
// 떼어 두었기 때문에 여기서 그대로 부를 수 있다 - 안 그러면 고리를 시험하려고
// 진짜 로그 파일을 열거나 표준 출력을 더럽혀야 한다.
#include <string>
#include <vector>

#include "core/log.h"
#include "harness.h"
#include "render/log_filter.h"

using cdtb::log::Level;
using cdtb::log::RingLine;
using cdtb::log::ring_collect;
using cdtb::log::ring_push;
using cdtb::render::LogFilter;
using cdtb::render::log_line_matches;

namespace {
// 작은 고리로 돌린다. 2000 칸짜리로는 "한 바퀴 돌았을 때" 를 시험하기 어렵다.
std::vector<RingLine> make_ring(std::size_t n) {
    return std::vector<RingLine>(n);
}

void push_n(std::vector<RingLine>& ring, std::uint64_t& total, int n,
            Level level = Level::Info) {
    for (int i = 0; i < n; ++i) {
        ring_push(ring, total, level, "12:00:00.000",
                  "줄 " + std::to_string(total + 1));
    }
}
}  // namespace

TEST(log_ring_numbers_lines_from_one_without_gaps) {
    auto ring = make_ring(8);
    std::uint64_t total = 0;
    push_n(ring, total, 5);
    CHECK(total == 5);

    std::vector<RingLine> out;
    CHECK(ring_collect(ring, total, 0, &out) == 0);
    CHECK(out.size() == 5);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i].seq == i + 1);
    }
}

TEST(log_ring_gives_only_what_is_new) {
    // 창이 매 프레임 전체를 복사하지 않는 근거다. 이미 본 번호 뒤만 받는다.
    auto ring = make_ring(8);
    std::uint64_t total = 0;
    push_n(ring, total, 3);

    std::vector<RingLine> out;
    ring_collect(ring, total, 0, &out);
    CHECK(out.size() == 3);

    const std::uint64_t seen = out.back().seq;
    out.clear();
    CHECK(ring_collect(ring, total, seen, &out) == 0);
    CHECK(out.empty());   // 새 줄이 없으면 아무것도 안 준다

    push_n(ring, total, 2);
    CHECK(ring_collect(ring, total, seen, &out) == 0);
    CHECK(out.size() == 2);
    CHECK(out[0].seq == 4);
    CHECK(out[1].seq == 5);
}

TEST(log_ring_appends_and_does_not_clear_the_caller_buffer) {
    // 창은 받은 줄을 계속 이어 붙인다. 여기서 비워 버리면 화면이 매 프레임
    // 최근 몇 줄만 남는다.
    auto ring = make_ring(8);
    std::uint64_t total = 0;
    push_n(ring, total, 2);

    std::vector<RingLine> out;
    out.push_back(RingLine{});   // 이미 들고 있던 것
    ring_collect(ring, total, 0, &out);
    CHECK(out.size() == 3);
}

TEST(log_ring_drops_the_oldest_when_it_wraps) {
    auto ring = make_ring(4);
    std::uint64_t total = 0;
    push_n(ring, total, 6);   // 1·2 는 덮였다

    std::vector<RingLine> out;
    CHECK(ring_collect(ring, total, 0, &out) == 2);   // 놓친 줄 2
    CHECK(out.size() == 4);
    CHECK(out.front().seq == 3);
    CHECK(out.back().seq == 6);
}

TEST(log_ring_reports_exactly_what_the_reader_missed) {
    // **놓친 수를 틀리게 세면 화면이 거짓말을 한다.** 4칸 고리에 8줄을 쓰면 남은
    // 것은 5~8 이다. 3번까지 본 독자가 잃은 것은 **4번 한 줄뿐**이다 - 5번은
    // 아직 고리에 있다. 경계를 한 칸 어긋나게 세기 쉬운 자리라 딱 집어 둔다.
    auto ring = make_ring(4);
    std::uint64_t total = 0;
    push_n(ring, total, 3);
    std::vector<RingLine> out;
    ring_collect(ring, total, 0, &out);
    CHECK(out.size() == 3);
    out.clear();

    push_n(ring, total, 5);   // 총 8, 고리에는 5~8 만 남는다
    CHECK(ring_collect(ring, total, 3, &out) == 1);   // 4번만 잃었다
    CHECK(out.size() == 4);
    CHECK(out.front().seq == 5);
    CHECK(out.back().seq == 8);
    out.clear();

    // 딱 따라잡고 있던 독자는 아무것도 안 잃는다(경계의 반대쪽).
    CHECK(ring_collect(ring, total, 4, &out) == 0);
    CHECK(out.size() == 4);
    out.clear();

    // 한참 뒤처진 독자는 고리에 든 것만 받고, 나머지를 잃은 것으로 센다.
    push_n(ring, total, 100);   // 총 108, 고리에는 105~108
    CHECK(ring_collect(ring, total, 8, &out) == 96);   // 9~104
    CHECK(out.size() == 4);
    CHECK(out.front().seq == 105);
}

TEST(log_ring_is_quiet_when_empty_or_unstarted) {
    std::vector<RingLine> out;
    auto empty = make_ring(0);
    std::uint64_t total = 0;
    // 자리가 없으면 넣지도 않는다(파일에는 남는다). total 이 올라가면 화면의
    // "전체" 수가 실제보다 커진다.
    ring_push(empty, total, Level::Info, "t", "x");
    CHECK(total == 0);
    CHECK(ring_collect(empty, 0, 0, &out) == 0);
    CHECK(out.empty());

    auto ring = make_ring(4);
    CHECK(ring_collect(ring, 0, 0, &out) == 0);   // 한 줄도 안 쓴 상태
    CHECK(out.empty());
    CHECK(ring_collect(ring, 0, 0, nullptr) == 0);
}

TEST(log_ring_keeps_level_and_time) {
    auto ring = make_ring(4);
    std::uint64_t total = 0;
    ring_push(ring, total, Level::Error, "12:34:56.789", "터졌다");
    std::vector<RingLine> out;
    ring_collect(ring, total, 0, &out);
    CHECK(out.size() == 1);
    CHECK(out[0].level == Level::Error);
    CHECK(out[0].time == "12:34:56.789");
    CHECK(out[0].text == "터졌다");
}

TEST(log_filter_gates_each_level_on_its_own) {
    LogFilter f;
    CHECK(log_line_matches(f, Level::Info, "아무거나"));
    CHECK(log_line_matches(f, Level::Warn, "아무거나"));
    CHECK(log_line_matches(f, Level::Error, "아무거나"));

    f.info = false;
    CHECK(!log_line_matches(f, Level::Info, "아무거나"));
    CHECK(log_line_matches(f, Level::Warn, "아무거나"));   // 나머지는 그대로
    CHECK(log_line_matches(f, Level::Error, "아무거나"));

    f = LogFilter{};
    f.warn = false;
    CHECK(!log_line_matches(f, Level::Warn, "아무거나"));
    CHECK(log_line_matches(f, Level::Info, "아무거나"));

    f = LogFilter{};
    f.error = false;
    CHECK(!log_line_matches(f, Level::Error, "아무거나"));
    CHECK(log_line_matches(f, Level::Info, "아무거나"));
}

TEST(log_filter_matches_substrings_ignoring_ascii_case) {
    LogFilter f;
    std::snprintf(f.query, sizeof(f.query), "%s", "inven");
    CHECK(log_line_matches(f, Level::Info, "INVENTORY component 0x1234"));
    CHECK(log_line_matches(f, Level::Info, "inventory"));
    CHECK(!log_line_matches(f, Level::Info, "가방 확장"));
}

TEST(log_filter_matches_korean_bytes_as_they_are) {
    // 한글은 대소문자가 없어 UTF-8 바이트 그대로 맞아야 한다. tolower 를 바이트마다
    // 거는 구현이라, 0x80 이상을 건드리면 여기서 깨진다.
    LogFilter f;
    std::snprintf(f.query, sizeof(f.query), "%s", "가방");
    CHECK(log_line_matches(f, Level::Info, "가방 확장: 목표 300 칸 A(+0x18)"));
    CHECK(!log_line_matches(f, Level::Info, "인벤토리 컴포넌트 0x45900199B00"));
}

TEST(log_filter_empty_query_matches_everything) {
    LogFilter f;
    CHECK(f.query[0] == '\0');
    CHECK(log_line_matches(f, Level::Info, ""));
    CHECK(log_line_matches(f, Level::Info, "무엇이든"));
}

TEST(log_filter_query_longer_than_the_line_never_matches) {
    LogFilter f;
    std::snprintf(f.query, sizeof(f.query), "%s", "아주 긴 검색어입니다");
    CHECK(!log_line_matches(f, Level::Info, "짧다"));
}

TEST(log_filter_level_gate_wins_over_the_query) {
    // 글자가 맞아도 레벨이 꺼져 있으면 안 보인다. 순서를 뒤집으면 꺼 둔 레벨이
    // 검색할 때만 되살아난다.
    LogFilter f;
    f.info = false;
    std::snprintf(f.query, sizeof(f.query), "%s", "가방");
    CHECK(!log_line_matches(f, Level::Info, "가방 확장"));
    CHECK(log_line_matches(f, Level::Warn, "가방 확장"));
}
