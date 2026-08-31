#include "harness.h"
#include "mem/scanner.h"

using namespace cdtb::mem;

TEST(parse_plain_bytes) {
    auto p = parse_pattern("48 8B 05");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).has_value() && p->at(0).value() == 0x48);
    CHECK(p->at(1).has_value() && p->at(1).value() == 0x8B);
    CHECK(p->at(2).has_value() && p->at(2).value() == 0x05);
}

TEST(parse_wildcard) {
    auto p = parse_pattern("48 ?? 05");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).has_value());
    CHECK(!p->at(1).has_value());
    CHECK(p->at(2).has_value());
}

TEST(parse_is_case_insensitive_and_tolerates_extra_spaces) {
    auto p = parse_pattern("  4d ?  0F  ");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).value() == 0x4D);
    CHECK(!p->at(1).has_value());
    CHECK(p->at(2).value() == 0x0F);
}

TEST(parse_rejects_bad_input) {
    CHECK(!parse_pattern("").has_value());
    CHECK(!parse_pattern("   ").has_value());
    CHECK(!parse_pattern("ZZ").has_value());
    CHECK(!parse_pattern("48 8").has_value());
    CHECK(!parse_pattern("48 8B0").has_value());
}

static const std::uint8_t kBuf[] = {
    0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44,
    0x90, 0x90,
    0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD,
};
static Range buf() { return Range{kBuf, sizeof(kBuf)}; }

TEST(find_first_at_start) {
    auto p = parse_pattern("48 8B 05");
    CHECK_EQ(find_first(buf(), *p), kBuf);
}

TEST(find_first_at_end) {
    auto p = parse_pattern("AA BB CC DD");
    CHECK_EQ(find_first(buf(), *p), kBuf + 12);
}

TEST(find_first_returns_null_when_absent) {
    auto p = parse_pattern("DE AD BE EF");
    CHECK_EQ(find_first(buf(), *p), nullptr);
}

TEST(find_first_honours_wildcards) {
    auto p = parse_pattern("48 8B 05 ?? ?? CC DD");
    CHECK_EQ(find_first(buf(), *p), kBuf + 9);
}

TEST(find_first_rejects_pattern_longer_than_range) {
    auto p = parse_pattern("48 8B 05");
    CHECK_EQ(find_first(Range{kBuf, 2}, *p), nullptr);
}

TEST(find_first_handles_empty_range) {
    auto p = parse_pattern("48");
    CHECK_EQ(find_first(Range{nullptr, 0}, *p), nullptr);
}

TEST(find_all_collects_every_hit_in_order) {
    auto p = parse_pattern("48 8B 05");
    auto hits = find_all(buf(), *p, 16);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits[0], kBuf);
    CHECK_EQ(hits[1], kBuf + 9);
}

TEST(find_all_respects_max) {
    auto p = parse_pattern("48 8B 05");
    auto hits = find_all(buf(), *p, 1);
    CHECK_EQ(hits.size(), std::size_t{1});
    CHECK_EQ(hits[0], kBuf);
}

// --------------------------------------------------------------------------
// 아래는 memchr 앵커 최적화의 안전망이다. 앵커를 "첫 바이트"가 아니라
// "첫 비-와일드카드 바이트"로 잡아야 하는 경우들을 고정한다.

static const std::uint8_t kLead[] = {
    0x00, 0x11, 0x48, 0x8B, 0x05, 0x99,
    0x00, 0x22, 0x48, 0x8B, 0x05, 0x88,
};
static Range lead() { return Range{kLead, sizeof(kLead)}; }

TEST(find_first_with_leading_wildcard) {
    auto p = parse_pattern("?? 11 48 8B 05");
    CHECK_EQ(find_first(lead(), *p), kLead);
}

TEST(find_first_with_two_leading_wildcards) {
    auto p = parse_pattern("?? ?? 48 8B 05 88");
    CHECK_EQ(find_first(lead(), *p), kLead + 6);
}

TEST(find_all_with_leading_wildcard) {
    auto p = parse_pattern("?? ?? 48 8B 05");
    auto hits = find_all(lead(), *p, 16);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits[0], kLead);
    CHECK_EQ(hits[1], kLead + 6);
}

TEST(find_first_all_wildcards_matches_at_start) {
    auto p = parse_pattern("?? ??");
    CHECK_EQ(find_first(lead(), *p), kLead);
}

TEST(find_all_all_wildcards_counts_every_offset) {
    auto p = parse_pattern("?? ??");
    auto hits = find_all(lead(), *p, 100);
    // 길이 12 버퍼에서 길이 2 패턴은 0..10 오프셋 11곳에서 일치한다.
    CHECK_EQ(hits.size(), std::size_t{11});
    CHECK_EQ(hits[0], kLead);
    CHECK_EQ(hits[10], kLead + 10);
}

TEST(find_first_leading_wildcard_cannot_match_before_buffer_start) {
    // "?? 00" 은 오프셋 0에서 시작할 수 없다면 그다음을 찾아야 한다.
    // kLead[0]=0x00 이므로 오프셋 5(값 0x99, 다음 0x00)에서 일치한다.
    auto p = parse_pattern("?? 00");
    CHECK_EQ(find_first(lead(), *p), kLead + 5);
}

TEST(find_first_anchor_near_buffer_end) {
    auto p = parse_pattern("05 88");
    CHECK_EQ(find_first(lead(), *p), kLead + 10);
}

TEST(find_first_anchor_hit_too_close_to_start_is_skipped) {
    // 앵커(0x11)가 오프셋 1에 있고 패턴상 앵커 앞에 2바이트가 필요하면
    // 그 후보는 버려야 한다. 여기서는 일치가 없다.
    auto p = parse_pattern("?? ?? 11");
    CHECK_EQ(find_first(lead(), *p), nullptr);
}
