#include "harness.h"
#include "mem/value_scanner.h"

#include <vector>

using namespace cdtb::mem;

namespace {

// 합성 버퍼를 하나의 영역으로 넘긴다. 실제 프로세스 메모리를
// 건드리지 않으므로 결정적이고 빠르다.
struct Fixture {
    alignas(4) float buf[8] = {1.0f, 2.0f, 1.0f, 3.0f,
                               1.0f, 4.0f, 5.0f, 1.0f};
    std::vector<Range> regions() const {
        return {Range{reinterpret_cast<const std::uint8_t*>(buf),
                      sizeof(buf)}};
    }
    std::uintptr_t at(int i) const {
        return reinterpret_cast<std::uintptr_t>(&buf[i]);
    }
};

}  // namespace

TEST(first_finds_all_matching_floats) {
    Fixture f;
    FloatScan s;
    CHECK_EQ(s.first(f.regions(), 1.0f, 0.0001f), std::size_t{4});
    CHECK_EQ(s.results()[0], f.at(0));
    CHECK_EQ(s.results()[3], f.at(7));
}

TEST(first_returns_zero_when_nothing_matches) {
    Fixture f;
    FloatScan s;
    CHECK_EQ(s.first(f.regions(), 99.0f, 0.0001f), std::size_t{0});
    CHECK(s.results().empty());
}

TEST(narrow_equals_keeps_only_still_matching) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 인덱스 0,2,4,7
    f.buf[2] = 7.0f;                          // 둘을 바꾼다
    f.buf[7] = 7.0f;
    CHECK_EQ(s.narrow_equals(1.0f, 0.0001f), std::size_t{2});
    CHECK_EQ(s.results()[0], f.at(0));
    CHECK_EQ(s.results()[1], f.at(4));
}

TEST(narrow_changed_keeps_only_changed) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[4] = 8.0f;
    CHECK_EQ(s.narrow_changed(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(4));
}

TEST(narrow_unchanged_keeps_only_unchanged) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[4] = 8.0f;
    CHECK_EQ(s.narrow_unchanged(), std::size_t{3});
}

TEST(narrow_increased_keeps_only_increased) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[0] = 2.0f;    // 증가
    f.buf[2] = 0.5f;    // 감소
    CHECK_EQ(s.narrow_increased(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(0));
}

TEST(narrow_decreased_keeps_only_decreased) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[0] = 2.0f;    // 증가
    f.buf[2] = 0.5f;    // 감소
    CHECK_EQ(s.narrow_decreased(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(2));
}

TEST(narrow_chain_converges_to_single_address) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 4개
    f.buf[0] = 2.0f;
    s.narrow_changed();                        // 1개
    CHECK_EQ(s.count(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(0));
}

TEST(narrow_updates_snapshot_for_next_round) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[0] = 2.0f;
    s.narrow_changed();                        // 0번만 남고 스냅샷은 2.0
    // 이제 아무것도 바꾸지 않으면 "변함"은 0개여야 한다.
    CHECK_EQ(s.narrow_changed(), std::size_t{0});
}

TEST(reset_clears_everything) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);
    s.reset();
    CHECK_EQ(s.count(), std::size_t{0});
    CHECK(!s.capped());
}

TEST(narrow_on_empty_scan_is_safe) {
    FloatScan s;
    CHECK_EQ(s.narrow_changed(), std::size_t{0});
    CHECK_EQ(s.narrow_equals(1.0f, 0.001f), std::size_t{0});
}

TEST(first_resets_previous_results) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 4개
    CHECK_EQ(s.first(f.regions(), 5.0f, 0.0001f), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(6));
}
