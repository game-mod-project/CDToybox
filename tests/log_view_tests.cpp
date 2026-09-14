// 로그 창의 **색인 관리**. 이 창에서 제일 조용히 깨지는 부분이다 - 5000줄을 넘겨야
// 나타나서 손으로 보기도 어렵다(리뷰 A-6). 그래서 ImGui 없는 조각으로 떼어 두었다.
#include <string>
#include <vector>

#include "core/log.h"
#include "harness.h"
#include "render/log_filter.h"
#include "render/log_view.h"

using cdtb::log::Level;
using cdtb::log::RingLine;
using cdtb::render::kLogTrimChunk;
using cdtb::render::kLogViewMax;
using cdtb::render::LogFilter;
using cdtb::render::LogView;
using cdtb::render::log_view_absorb;
using cdtb::render::log_view_clear;
using cdtb::render::log_view_rebuild;
using cdtb::render::log_view_trim;

namespace {
// 고리에서 받아 온 것처럼 lines 뒤에 붙이고 absorb 를 부른다.
void feed(LogView& v, const LogFilter& f, int n, Level level = Level::Info,
          const char* text = "줄", std::uint64_t missed = 0) {
    const std::size_t before = v.lines.size();
    for (int i = 0; i < n; ++i) {
        RingLine l;
        l.seq = v.seen + static_cast<std::uint64_t>(i) + 1;
        l.level = level;
        l.time = "12:00:00.000";
        l.text = text;
        v.lines.push_back(l);
    }
    log_view_absorb(v, before, missed, f);
}
}  // namespace

TEST(log_view_first_fill_is_not_counted_as_missed) {
    // **창을 열기 전의 줄은 놓친 것이 아니다.** 10분 플레이 뒤 창을 처음 열면
    // ring_since(0, ...) 이 "1번부터 달라" 는 요청이 되어 고리 밖의 수천 줄이
    // 전부 놓친 줄로 잡힌다. 사용자는 창을 닫아 둔 적도, 잃은 것도 없다
    // (전부 파일에 있다, 리뷰 A-2).
    LogView v;
    const LogFilter f;
    feed(v, f, 3, Level::Info, "줄", /*missed=*/13000);
    CHECK(v.missed == 0);
    CHECK(v.primed);

    // 두 번째부터는 진짜로 센다.
    feed(v, f, 2, Level::Info, "줄", /*missed=*/7);
    CHECK(v.missed == 7);
}

TEST(log_view_first_fill_counts_nothing_even_when_empty) {
    // 한 줄도 안 받은 첫 채움도 primed 를 세워야 한다 - 안 그러면 창을 연 채
    // 조용한 몇 프레임 뒤 첫 줄이 올 때 그 앞 간격을 놓친 것으로 센다.
    LogView v;
    const LogFilter f;
    log_view_absorb(v, 0, 5000, f);
    CHECK(v.missed == 0);
    CHECK(v.primed);
    log_view_absorb(v, 0, 3, f);
    CHECK(v.missed == 3);
}

TEST(log_view_only_filters_the_new_lines) {
    LogView v;
    LogFilter f;
    f.warn = false;
    feed(v, f, 4, Level::Info);
    CHECK(v.lines.size() == 4);
    CHECK(v.view.size() == 4);
    feed(v, f, 3, Level::Warn);
    CHECK(v.lines.size() == 7);
    CHECK(v.view.size() == 4);   // 경고는 안 보인다
    // 색인은 실제 줄을 가리킨다.
    for (const int i : v.view) {
        CHECK(v.lines[static_cast<std::size_t>(i)].level == Level::Info);
    }
}

TEST(log_view_tracks_the_last_sequence_number) {
    LogView v;
    const LogFilter f;
    feed(v, f, 5);
    CHECK(v.seen == 5);
    feed(v, f, 2);
    CHECK(v.seen == 7);
}

TEST(log_view_rebuild_matches_a_fresh_filter) {
    LogView v;
    LogFilter f;
    feed(v, f, 3, Level::Info, "가방 확장");
    feed(v, f, 3, Level::Error, "터졌다");
    CHECK(v.view.size() == 6);

    std::snprintf(f.query, sizeof(f.query), "%s", "가방");
    log_view_rebuild(v, f);
    CHECK(v.view.size() == 3);
    for (const int i : v.view) {
        CHECK(v.lines[static_cast<std::size_t>(i)].text == "가방 확장");
    }

    f.info = false;
    log_view_rebuild(v, f);
    CHECK(v.view.empty());
}

TEST(log_view_trim_keeps_indices_pointing_at_the_same_lines) {
    // **여기가 A-6 의 핵심이다.** 자르기 뒤 색인을 안 맞추면 스크롤이 엉뚱한 줄을
    // 그린다. `+ kLogTrimChunk` 를 빼거나 `>= drop` 을 `> drop` 으로 바꾸면 여기서
    // 걸린다.
    LogView v;
    const LogFilter f;
    const int n = static_cast<int>(kLogViewMax + kLogTrimChunk + 7);
    for (int i = 0; i < n; ++i) {
        RingLine l;
        l.seq = static_cast<std::uint64_t>(i) + 1;
        l.time = "12:00:00.000";
        l.text = "줄 " + std::to_string(i + 1);
        v.lines.push_back(l);
        v.view.push_back(i);
    }
    v.primed = true;
    log_view_trim(v);

    CHECK(v.lines.size() <= kLogViewMax);
    CHECK(!v.lines.empty());
    CHECK(v.view.size() == v.lines.size());
    // 색인이 가리키는 줄이 실제로 그 줄인가. 자른 뒤에도 끝은 마지막 줄이다.
    for (std::size_t k = 0; k < v.view.size(); ++k) {
        const int i = v.view[k];
        CHECK(i >= 0);
        CHECK(static_cast<std::size_t>(i) < v.lines.size());
        CHECK(v.lines[static_cast<std::size_t>(i)].seq ==
              v.lines[k].seq);   // 순서가 보존됐다(전부 통과하는 거르개였다)
    }
    CHECK(v.lines.back().seq == static_cast<std::uint64_t>(n));
}

TEST(log_view_trim_drops_only_the_lines_that_left) {
    // 거르개를 통과한 것이 드문드문일 때도 색인이 맞아야 한다.
    LogView v;
    LogFilter f;
    f.info = false;   // 경고만 보인다
    const int n = static_cast<int>(kLogViewMax + kLogTrimChunk + 10);
    for (int i = 0; i < n; ++i) {
        RingLine l;
        l.seq = static_cast<std::uint64_t>(i) + 1;
        l.level = (i % 5 == 0) ? Level::Warn : Level::Info;
        l.time = "12:00:00.000";
        l.text = "줄 " + std::to_string(i + 1);
        v.lines.push_back(l);
        if (l.level == Level::Warn) v.view.push_back(i);
    }
    v.primed = true;
    log_view_trim(v);

    CHECK(v.lines.size() <= kLogViewMax);
    for (const int i : v.view) {
        CHECK(i >= 0);
        CHECK(static_cast<std::size_t>(i) < v.lines.size());
        // 보이는 것은 전부 경고여야 한다 - 색인이 밀렸으면 여기서 깨진다.
        CHECK(v.lines[static_cast<std::size_t>(i)].level == Level::Warn);
    }
}

TEST(log_view_does_not_trim_below_the_cap) {
    LogView v;
    const LogFilter f;
    feed(v, f, static_cast<int>(kLogViewMax));
    CHECK(v.lines.size() == kLogViewMax);
    log_view_trim(v);
    CHECK(v.lines.size() == kLogViewMax);   // 딱 상한이면 안 자른다
    CHECK(v.view.size() == kLogViewMax);
}

TEST(log_view_clear_keeps_the_sequence_so_lines_do_not_come_back) {
    // seen 을 0 으로 되돌리면 고리에 남은 줄이 통째로 다시 들어와, "지우기" 가
    // 아무 일도 안 한 것처럼 보인다.
    LogView v;
    const LogFilter f;
    feed(v, f, 6);
    const std::uint64_t seen = v.seen;
    v.missed = 9;
    log_view_clear(v);
    CHECK(v.lines.empty());
    CHECK(v.view.empty());
    CHECK(v.missed == 0);
    CHECK(v.seen == seen);
    CHECK(v.primed);   // 지운 뒤의 간격은 진짜 놓친 것이다
    log_view_absorb(v, 0, 4, f);
    CHECK(v.missed == 4);
}
