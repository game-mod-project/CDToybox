#include <string>
#include <vector>

#include "game/item_view.h"
#include "harness.h"

namespace {

using cdtb::game::ItemCatalogEntry;
using cdtb::game::ItemFilter;
using cdtb::game::ItemSort;

std::vector<ItemCatalogEntry> sample() {
    std::vector<ItemCatalogEntry> v(4);
    v[0].key = 2200;    v[0].name_key = 300; v[0].name = "편전";
    v[1].key = 50001;   v[1].name_key = 100; v[1].name = "화살";
    v[2].key = 950002;  v[2].name_key = 400; v[2].name = "벌목용 도끼";
    v[3].key = 200997;  v[3].name_key = 200; v[3].name = "";   // 이름 없는 것
    return v;
}

}  // namespace

// ---------------------------------------------------------------- 거르기

TEST(filter_items_returns_everything_when_query_is_empty) {
    const auto all = sample();
    const auto out = cdtb::game::filter_items(all, ItemFilter{});
    CHECK_EQ(out.size(), static_cast<std::size_t>(4));
    if (!out.empty()) CHECK_EQ(out[0]->key, 2200u);
}

TEST(filter_items_matches_name_substring) {
    const auto all = sample();
    ItemFilter f;
    f.query = "화살";
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 50001u);
}

TEST(filter_items_matches_key_digits) {
    // 키로도 찾을 수 있어야 한다. 지급 대상을 키로 아는 경우가 있다.
    const auto all = sample();
    ItemFilter f;
    f.query = "9500";
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 950002u);
}

TEST(filter_items_can_hide_unnamed) {
    const auto all = sample();
    ItemFilter f;
    f.hide_unnamed = true;
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    for (const auto* e : out) CHECK(!e->name.empty());
}

TEST(filter_items_keeps_unnamed_when_query_matches_its_key) {
    // 이름이 없어도 키로는 찾을 수 있어야 한다.
    const auto all = sample();
    ItemFilter f;
    f.query = "200997";
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 200997u);
}

// ---------------------------------------------------------------- 정렬

TEST(sort_items_orders_by_key) {
    const auto all = sample();
    auto out = cdtb::game::filter_items(all, ItemFilter{});
    CHECK_EQ(out.size(), static_cast<std::size_t>(4));
    if (out.size() != 4) return;

    cdtb::game::sort_items(out, ItemSort::Key, true);
    CHECK_EQ(out[0]->key, 2200u);
    CHECK_EQ(out[3]->key, 950002u);

    cdtb::game::sort_items(out, ItemSort::Key, false);
    CHECK_EQ(out[0]->key, 950002u);
    CHECK_EQ(out[3]->key, 2200u);
}

TEST(sort_items_orders_by_name) {
    const auto all = sample();
    auto out = cdtb::game::filter_items(all, ItemFilter{});
    CHECK_EQ(out.size(), static_cast<std::size_t>(4));
    if (out.size() != 4) return;

    cdtb::game::sort_items(out, ItemSort::Name, true);
    // 이름 없는 것은 뒤로 보낸다. 앞에 몰리면 목록이 쓸모없어진다.
    CHECK(out[3]->name.empty());
    for (std::size_t i = 0; i + 1 < 3; ++i) {
        CHECK(out[i]->name <= out[i + 1]->name);
    }
}

TEST(sort_items_keeps_unnamed_last_when_descending) {
    const auto all = sample();
    auto out = cdtb::game::filter_items(all, ItemFilter{});
    CHECK_EQ(out.size(), static_cast<std::size_t>(4));
    if (out.size() != 4) return;

    cdtb::game::sort_items(out, ItemSort::Name, false);
    CHECK(out[3]->name.empty());
}

// -------------------------------------------------------------- 페이징

TEST(page_count_rounds_up) {
    CHECK_EQ(cdtb::game::page_count(100, 20), static_cast<std::size_t>(5));
    CHECK_EQ(cdtb::game::page_count(101, 20), static_cast<std::size_t>(6));
    CHECK_EQ(cdtb::game::page_count(6810, 40), static_cast<std::size_t>(171));
}

TEST(page_count_is_one_when_empty) {
    // 빈 목록에도 페이지는 하나 있다. 0 이면 UI 가 0/0 을 낸다.
    CHECK_EQ(cdtb::game::page_count(0, 20), static_cast<std::size_t>(1));
}

TEST(page_count_guards_zero_per_page) {
    CHECK_EQ(cdtb::game::page_count(100, 0), static_cast<std::size_t>(1));
}

TEST(page_range_gives_the_requested_slice) {
    const auto r = cdtb::game::page_range(100, 2, 20);
    CHECK_EQ(r.begin, static_cast<std::size_t>(40));
    CHECK_EQ(r.end, static_cast<std::size_t>(60));
}

TEST(page_range_clamps_the_last_partial_page) {
    const auto r = cdtb::game::page_range(105, 5, 20);
    CHECK_EQ(r.begin, static_cast<std::size_t>(100));
    CHECK_EQ(r.end, static_cast<std::size_t>(105));
}

TEST(page_range_clamps_page_past_the_end) {
    // 걸러서 개수가 줄면 현재 페이지가 범위를 넘을 수 있다.
    const auto r = cdtb::game::page_range(30, 99, 20);
    CHECK_EQ(r.begin, static_cast<std::size_t>(20));
    CHECK_EQ(r.end, static_cast<std::size_t>(30));
}

TEST(page_range_of_empty_list_is_empty) {
    const auto r = cdtb::game::page_range(0, 0, 20);
    CHECK_EQ(r.begin, static_cast<std::size_t>(0));
    CHECK_EQ(r.end, static_cast<std::size_t>(0));
}
