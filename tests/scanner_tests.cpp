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
