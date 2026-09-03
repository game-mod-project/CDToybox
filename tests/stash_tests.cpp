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

// ------------------------------------------------- 담금질과 소켓

// 지급은 담금질·소켓이 빈 장비를 준다. 캐릭터가 손댄 장비를 그대로
// 보관했다 꺼내려면 그 둘을 파일에 실어야 한다.
//
// 줄 형식은 기존 것을 늘린다 - 뒤에 토큰을 붙일 뿐이라 옛 파일이
// 그대로 읽힌다.
//
//   item <키> <개수> [t<담금질>] [s<슬롯>:<보석키>:<원본12자리hex>]...

TEST(serialize_writes_temper_and_sockets) {
    cdtb::game::Stash s;
    const int i = s.add_set("군주의 검");
    cdtb::game::StashEntry e{200914, 1};
    e.temper = 1;
    e.sockets.push_back(
        cdtb::game::StashSocket{0, 1002569, {0x24, 0x0D, 0xFF, 0xFF, 0x00, 0xFF}});
    s.set_at(i)->items.push_back(e);

    const std::string text = s.serialize();
    CHECK(text.find("item 200914 1 t1 s0:1002569:240DFFFF00FF\n") !=
          std::string::npos);
}

TEST(serialize_omits_temper_zero_and_empty_sockets) {
    // 대부분의 아이템은 둘 다 없다. 그런 줄까지 길어지면 옛 파일과
    // 달라 보이고 눈으로 읽기도 나빠진다.
    cdtb::game::Stash s;
    const int i = s.add_set("잡동사니");
    s.set_at(i)->items.push_back(cdtb::game::StashEntry{50001, 100});

    CHECK(s.serialize().find("item 50001 100\n") != std::string::npos);
}

TEST(parse_reads_temper_and_sockets) {
    cdtb::game::Stash s;
    CHECK(s.parse("set 군주의 검\n"
                  "item 200914 1 t1 s0:1002569:240DFFFF00FF"
                  " s2:1002810:900CFFFF02FF\n"));
    CHECK_EQ(s.set_count(), 1);
    if (s.set_count() != 1) return;
    const auto& items = s.set_at(0)->items;
    CHECK_EQ(items.size(), std::size_t{1});
    if (items.empty()) return;
    CHECK_EQ(items[0].key, std::uint32_t{200914});
    CHECK_EQ(items[0].temper, std::uint32_t{1});
    CHECK_EQ(items[0].sockets.size(), std::size_t{2});
    if (items[0].sockets.size() < 2) return;
    CHECK_EQ(items[0].sockets[0].slot, std::uint32_t{0});
    CHECK_EQ(items[0].sockets[0].key, std::uint32_t{1002569});
    CHECK_EQ(items[0].sockets[0].raw[0], std::uint8_t{0x24});
    CHECK_EQ(items[0].sockets[0].raw[5], std::uint8_t{0xFF});
    CHECK_EQ(items[0].sockets[1].slot, std::uint32_t{2});
    CHECK_EQ(items[0].sockets[1].key, std::uint32_t{1002810});
}

TEST(parse_still_reads_an_item_line_without_extras) {
    // 옛 파일이 그대로 읽혀야 한다.
    cdtb::game::Stash s;
    CHECK(s.parse("set 옛 세트\nitem 50001 100\n"));
    CHECK_EQ(s.set_count(), 1);
    if (s.set_count() != 1) return;
    const auto& items = s.set_at(0)->items;
    CHECK_EQ(items.size(), std::size_t{1});
    if (items.empty()) return;
    CHECK_EQ(items[0].count, std::int64_t{100});
    CHECK_EQ(items[0].temper, std::uint32_t{0});
    CHECK(items[0].sockets.empty());
}

TEST(parse_drops_a_broken_socket_token) {
    // 손으로 고치다 깨질 수 있다. 그 토큰만 버리고 줄은 살린다.
    cdtb::game::Stash s;
    CHECK(s.parse("set 깨진 줄\n"
                  "item 200914 1 t2 s0:1002569 sXYZ s1:1002785:8E0CFFFF01FF\n"));
    CHECK_EQ(s.set_count(), 1);
    if (s.set_count() != 1) return;
    const auto& items = s.set_at(0)->items;
    CHECK_EQ(items.size(), std::size_t{1});
    if (items.empty()) return;
    CHECK_EQ(items[0].temper, std::uint32_t{2});
    CHECK_EQ(items[0].sockets.size(), std::size_t{1});
    if (items[0].sockets.empty()) return;
    CHECK_EQ(items[0].sockets[0].key, std::uint32_t{1002785});
}

TEST(round_trip_keeps_temper_and_sockets) {
    cdtb::game::Stash a;
    const int i = a.add_set("가방 export");
    cdtb::game::StashEntry e{121054, 1};
    e.temper = 2;
    e.sockets.push_back(
        cdtb::game::StashSocket{1, 1002791, {0x8F, 0x0C, 0xFF, 0xFF, 0x01, 0xFF}});
    a.set_at(i)->items.push_back(e);

    cdtb::game::Stash b;
    CHECK(b.parse(a.serialize()));
    CHECK_EQ(b.set_count(), 1);
    if (b.set_count() != 1) return;
    const auto& items = b.set_at(0)->items;
    CHECK_EQ(items.size(), std::size_t{1});
    if (items.empty()) return;
    CHECK_EQ(items[0].key, std::uint32_t{121054});
    CHECK_EQ(items[0].temper, std::uint32_t{2});
    CHECK_EQ(items[0].sockets.size(), std::size_t{1});
    if (items[0].sockets.empty()) return;
    CHECK_EQ(items[0].sockets[0].slot, std::uint32_t{1});
    CHECK_EQ(items[0].sockets[0].key, std::uint32_t{1002791});
    CHECK_EQ(items[0].sockets[0].raw[4], std::uint8_t{0x01});
    // 여섯 번째까지 봐야 한다. 실측에서 원본은 0xFF 이고
    // 지급분이 0x00 으로 갈린 적이 있다 - 왕복에서 잃으면
    // 그것을 못 잡는다.
    CHECK_EQ(items[0].sockets[0].raw[5], std::uint8_t{0xFF});
    CHECK_EQ(items[0].sockets[0].raw[0], std::uint8_t{0x8F});
}
