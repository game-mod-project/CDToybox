#include <string>

#include "game/stash.h"
#include "harness.h"

namespace {

using cdtb::game::Stash;
using cdtb::game::StashEntry;

TEST(favorite_starts_off_and_toggles) {
    Stash s;
    CHECK(!s.is_favorite(50001));
    s.toggle_favorite(50001);
    CHECK(s.is_favorite(50001));
    s.toggle_favorite(50001);
    CHECK(!s.is_favorite(50001));
}

TEST(favorites_keep_insertion_order) {
    Stash s;
    s.toggle_favorite(105);
    s.toggle_favorite(50001);
    s.toggle_favorite(2200);
    CHECK_EQ(s.favorites().size(), std::size_t{3});
    CHECK_EQ(s.favorites()[0], std::uint32_t{105});
    CHECK_EQ(s.favorites()[2], std::uint32_t{2200});
}

TEST(removing_a_favorite_keeps_the_rest_in_order) {
    Stash s;
    s.toggle_favorite(105);
    s.toggle_favorite(50001);
    s.toggle_favorite(2200);
    s.toggle_favorite(50001);
    CHECK_EQ(s.favorites().size(), std::size_t{2});
    CHECK_EQ(s.favorites()[0], std::uint32_t{105});
    CHECK_EQ(s.favorites()[1], std::uint32_t{2200});
}

TEST(a_new_set_is_empty_and_named) {
    Stash s;
    const int i = s.add_set("전투 세트");
    CHECK_EQ(i, 0);
    CHECK_EQ(s.set_count(), 1);
    CHECK_EQ(s.set_at(0)->name, std::string("전투 세트"));
    CHECK(s.set_at(0)->items.empty());
}

TEST(removing_a_set_shifts_the_later_ones) {
    Stash s;
    s.add_set("가");
    s.add_set("나");
    s.add_set("다");
    s.remove_set(1);
    CHECK_EQ(s.set_count(), 2);
    CHECK_EQ(s.set_at(1)->name, std::string("다"));
}

TEST(set_index_out_of_range_gives_nothing) {
    Stash s;
    CHECK(s.set_at(0) == nullptr);
    CHECK(s.set_at(-1) == nullptr);
}

// 저장은 사람이 읽고 고칠 수 있어야 한다. 실수로 깨져도 손으로
// 고칠 수 있는 편이 낫다.
TEST(serialize_and_parse_round_trip) {
    Stash a;
    a.toggle_favorite(50001);
    a.toggle_favorite(105);
    const int i = a.add_set("전투 세트");
    a.set_at(i)->items.push_back(StashEntry{50001, 100});
    a.set_at(i)->items.push_back(StashEntry{2200, 5});
    a.add_set("빈 세트");

    Stash b;
    CHECK(b.parse(a.serialize()));
    CHECK_EQ(b.favorites().size(), std::size_t{2});
    CHECK(b.is_favorite(50001));
    CHECK(b.is_favorite(105));
    CHECK_EQ(b.set_count(), 2);
    CHECK_EQ(b.set_at(0)->name, std::string("전투 세트"));
    CHECK_EQ(b.set_at(0)->items.size(), std::size_t{2});
    CHECK_EQ(b.set_at(0)->items[1].key, std::uint32_t{2200});
    CHECK_EQ(b.set_at(0)->items[1].count, std::int64_t{5});
    CHECK_EQ(b.set_at(1)->name, std::string("빈 세트"));
    CHECK(b.set_at(1)->items.empty());
}

TEST(parse_ignores_blank_lines_and_comments) {
    Stash s;
    CHECK(s.parse("# 주석\n\nfav 7\n\n"));
    CHECK(s.is_favorite(7));
}

// 세트 이름에 공백이 들어간다. 줄 전체를 이름으로 읽어야 한다.
TEST(set_name_may_contain_spaces) {
    Stash s;
    CHECK(s.parse("set 사냥 갈 때 쓰는 것\nitem 50001 3\n"));
    CHECK_EQ(s.set_count(), 1);
    CHECK_EQ(s.set_at(0)->name, std::string("사냥 갈 때 쓰는 것"));
    CHECK_EQ(s.set_at(0)->items.size(), std::size_t{1});
}

// 세트 밖의 item 줄은 버린다. 손으로 고치다 깨질 수 있다.
TEST(parse_drops_an_item_without_a_set) {
    Stash s;
    CHECK(s.parse("item 50001 3\n"));
    CHECK_EQ(s.set_count(), 0);
}

TEST(parse_drops_a_nonpositive_count) {
    Stash s;
    CHECK(s.parse("set 가\nitem 50001 0\nitem 105 -2\nitem 2200 1\n"));
    CHECK_EQ(s.set_at(0)->items.size(), std::size_t{1});
    CHECK_EQ(s.set_at(0)->items[0].key, std::uint32_t{2200});
}

}  // namespace
