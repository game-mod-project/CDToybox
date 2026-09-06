#include "game/inv_io.h"

#include <string>
#include <vector>

#include "harness.h"

using cdtb::game::InvItemSnap;
using cdtb::game::InvSocketSnap;
using cdtb::game::inv_parse;
using cdtb::game::inv_serialize;

namespace {

InvItemSnap make_item(std::uint16_t kind, std::uint32_t index) {
    InvItemSnap it;
    it.kind = kind;
    it.index = index;
    it.count = 1;
    return it;
}

}  // namespace

// 담금질·연마·내구도·소켓이 직렬화 후 파싱으로 그대로 돌아온다.
TEST(inv_io_roundtrip_basic) {
    std::vector<InvItemSnap> items;
    InvItemSnap a = make_item(1, 6283);
    a.count = 2;
    a.temper = 3;
    a.sharpness = 100;
    a.endurance = 30;
    a.sockets.push_back({0, 3318, false});  // 박힌 보석
    a.sockets.push_back({1, -1, false});     // 빈 열린 칸
    a.sockets.push_back({2, -1, true});      // 잠긴 칸
    items.push_back(a);

    const std::string text = inv_serialize(items);
    std::vector<InvItemSnap> back;
    CHECK(inv_parse(text, &back));
    CHECK_EQ(back.size(), std::size_t{1});

    const InvItemSnap& r = back[0];
    CHECK_EQ(r.kind, std::uint16_t{1});
    CHECK_EQ(r.index, std::uint32_t{6283});
    CHECK_EQ(r.count, std::int64_t{2});
    CHECK_EQ(r.temper, std::uint16_t{3});
    CHECK_EQ(r.sharpness, std::uint16_t{100});
    CHECK_EQ(r.endurance, std::uint32_t{30});
    CHECK_EQ(r.sockets.size(), std::size_t{3});
    CHECK_EQ(r.sockets[0].slot, std::uint32_t{0});
    CHECK_EQ(r.sockets[0].gem, 3318);
    CHECK(!r.sockets[0].locked);
    CHECK_EQ(r.sockets[1].gem, -1);
    CHECK(!r.sockets[1].locked);
    CHECK_EQ(r.sockets[2].gem, -1);
    CHECK(r.sockets[2].locked);
}

// 내구도 없는 아이템(0xFFFF)은 dur=- 로 나가고 그대로 돌아온다.
TEST(inv_io_no_endurance) {
    std::vector<InvItemSnap> items;
    InvItemSnap a = make_item(4, 42);
    a.endurance = 0xFFFF;
    items.push_back(a);

    std::vector<InvItemSnap> back;
    CHECK(inv_parse(inv_serialize(items), &back));
    CHECK_EQ(back.size(), std::size_t{1});
    CHECK_EQ(back[0].endurance, std::uint32_t{0xFFFF});
    CHECK_EQ(back[0].sockets.size(), std::size_t{0});
}

// 여러 컨테이너 종류가 순서·종류를 유지한다.
TEST(inv_io_multi_kind) {
    std::vector<InvItemSnap> items;
    items.push_back(make_item(1, 10));
    items.push_back(make_item(1, 11));
    items.push_back(make_item(4, 20));
    items.push_back(make_item(0, 30));

    std::vector<InvItemSnap> back;
    CHECK(inv_parse(inv_serialize(items), &back));
    CHECK_EQ(back.size(), std::size_t{4});
    CHECK_EQ(back[0].kind, std::uint16_t{1});
    CHECK_EQ(back[0].index, std::uint32_t{10});
    CHECK_EQ(back[1].kind, std::uint16_t{1});
    CHECK_EQ(back[1].index, std::uint32_t{11});
    CHECK_EQ(back[2].kind, std::uint16_t{4});
    CHECK_EQ(back[2].index, std::uint32_t{20});
    CHECK_EQ(back[3].kind, std::uint16_t{0});
    CHECK_EQ(back[3].index, std::uint32_t{30});
}

// 주석·빈 줄·모르는 줄은 조용히 버린다.
TEST(inv_io_parse_ignores_junk) {
    const std::string text =
        "# CDToybox inventory export v2\n"
        "\n"
        "garbage line without keys\n"
        "[kind 1]\n"
        "순번=555 qty=1 temper=5 sharp=0 dur=-\n"
        "fav 999\n";   // 보관함 토큰 - 여기선 무시
    std::vector<InvItemSnap> back;
    CHECK(inv_parse(text, &back));
    CHECK_EQ(back.size(), std::size_t{1});
    CHECK_EQ(back[0].index, std::uint32_t{555});
    CHECK_EQ(back[0].temper, std::uint16_t{5});
    CHECK_EQ(back[0].endurance, std::uint32_t{0xFFFF});
}

// 순번이 없는 줄은 아이템으로 치지 않는다.
TEST(inv_io_parse_requires_index) {
    const std::string text =
        "[kind 1]\n"
        "qty=1 temper=5\n";
    std::vector<InvItemSnap> back;
    CHECK(inv_parse(text, &back));
    CHECK_EQ(back.size(), std::size_t{0});
}

// CRLF 줄끝도 읽힌다.
TEST(inv_io_parse_crlf) {
    const std::string text =
        "[kind 2]\r\n순번=7 qty=1 temper=1 sharp=2 dur=9\r\n";
    std::vector<InvItemSnap> back;
    CHECK(inv_parse(text, &back));
    CHECK_EQ(back.size(), std::size_t{1});
    CHECK_EQ(back[0].kind, std::uint16_t{2});
    CHECK_EQ(back[0].index, std::uint32_t{7});
    CHECK_EQ(back[0].sharpness, std::uint16_t{2});
    CHECK_EQ(back[0].endurance, std::uint32_t{9});
}
