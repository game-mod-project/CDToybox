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

// ------------------------------------------------------- 등급·분류 거르기

namespace {

std::vector<ItemCatalogEntry> graded() {
    std::vector<ItemCatalogEntry> v(4);
    v[0].key = 1; v[0].name = "T5 검";  v[0].grade = 5; v[0].category = 56;
    v[1].key = 2; v[1].name = "T1 검";  v[1].grade = 1; v[1].category = 56;
    v[2].key = 3; v[2].name = "장갑";   v[2].grade = 3; v[2].category = 22;
    v[3].key = 4; v[3].name = "화살";   v[3].grade = 0; v[3].category = 70;
    return v;
}

}  // namespace

TEST(filter_items_by_grade) {
    const auto all = graded();
    ItemFilter f;
    f.grade = 5;
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 1u);
}

TEST(filter_items_by_grade_zero_means_ungraded_not_all) {
    // 0 은 '등급 없음' 이라는 뜻이지 '전체' 가 아니다. 전체는 -1 이다.
    const auto all = graded();
    ItemFilter f;
    f.grade = 0;
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 4u);
}

TEST(filter_items_by_category) {
    const auto all = graded();
    ItemFilter f;
    f.category = 56;
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(2));
}

TEST(sort_items_orders_by_grade) {
    const auto all = graded();
    auto out = cdtb::game::filter_items(all, ItemFilter{});
    CHECK_EQ(out.size(), static_cast<std::size_t>(4));
    if (out.size() != 4) return;
    cdtb::game::sort_items(out, ItemSort::Grade, false);
    CHECK_EQ(out[0]->grade, static_cast<std::uint8_t>(5));
    CHECK_EQ(out[3]->grade, static_cast<std::uint8_t>(0));
}

// ------------------------------------------------------- 술어 (passes)

TEST(passes_agrees_with_filter_items) {
    // filter_items 가 passes 를 부르도록 바뀌었다. 같은 입력에 같은
    // 판정을 내야 한다 - 이름·키·이름없음·등급·분류를 전부 돈다.
    const auto named = sample();
    const auto tiers = graded();

    ItemFilter fs[6];
    fs[1].query = "화살";
    fs[2].query = "9500";
    fs[3].hide_unnamed = true;
    fs[4].grade = 0;
    fs[5].category = 56;

    for (const auto* all : {&named, &tiers}) {
        for (const auto& f : fs) {
            const auto out = cdtb::game::filter_items(*all, f);
            std::size_t n = 0;
            for (const auto& e : *all) {
                bool in = false;
                for (const auto* o : out) {
                    if (o == &e) in = true;
                }
                const bool p = cdtb::game::passes(f, e.name, e.grade,
                                                  e.category, e.key);
                CHECK_EQ(p, in);
                if (p) ++n;
            }
            CHECK_EQ(n, out.size());
        }
    }
}

TEST(passes_ignores_key_when_match_key_is_off) {
    // 인벤토리는 이름만 본다. 숫자를 쳐도 키가 걸리면 안 된다.
    ItemFilter f;
    f.query = "9500";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
    f.match_key = true;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
}

TEST(passes_with_key_off_still_matches_name) {
    ItemFilter f;
    f.query = "도끼";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
}

TEST(passes_with_key_off_drops_unnamed_even_if_key_matches) {
    // 이름 없는 것은 키로만 찾을 수 있는데, 키를 안 보면 못 찾는다.
    ItemFilter f;
    f.query = "200997";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u));
}

// ------------------------------------------------- Combo 색인 -> 필터

TEST(make_filter_index_zero_means_all) {
    const std::vector<std::uint8_t> cats = {56, 22};
    const auto f = cdtb::game::make_filter("", 0, 0, false, cats);
    CHECK_EQ(f.grade, -1);
    CHECK_EQ(f.category, -1);
    CHECK(f.query.empty());
    CHECK(!f.hide_unnamed);
    CHECK(f.match_key);
}

TEST(make_filter_grade_index_is_one_past_the_grade) {
    // Combo 는 0 이 "전체" 라 등급이 한 칸 밀려 있다. 1 이 등급 0(없음).
    const std::vector<std::uint8_t> cats;
    CHECK_EQ(cdtb::game::make_filter("", 1, 0, false, cats).grade, 0);
    CHECK_EQ(cdtb::game::make_filter("", 6, 0, false, cats).grade, 5);
}

TEST(make_filter_category_index_looks_up_the_table) {
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, cats).category, 56);
    CHECK_EQ(cdtb::game::make_filter("", 0, 2, false, cats).category, 22);
}

TEST(make_filter_category_index_past_the_table_means_all) {
    // 카탈로그가 새 판으로 갈리면 Combo 색인이 표 길이를 넘을 수 있다.
    // 그때 배열 밖을 읽지 말고 "전체" 로 떨어져야 한다.
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 3, false, cats).category, -1);
    const std::vector<std::uint8_t> none;
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, none).category, -1);
}

TEST(make_filter_copies_query_and_hide_unnamed) {
    const std::vector<std::uint8_t> cats;
    const auto f = cdtb::game::make_filter("화살", 0, 0, true, cats);
    CHECK_EQ(f.query, std::string("화살"));
    CHECK(f.hide_unnamed);
}

// ------------------------------------------------- 후속 정리에서 더한 것

TEST(passes_hides_unnamed_before_matching_its_key) {
    // 판정 순서를 못박는다 - hide_unnamed 가 키 매칭보다 먼저다. 이름 없는
    // 항목은 키로 찾을 수 있지만, 감추기가 켜져 있으면 키가 맞아도 뺀다.
    ItemFilter f;
    f.hide_unnamed = true;
    f.query = "200997";
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u));
    const auto all = sample();
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(0));
}

TEST(make_filter_negative_index_means_all) {
    // Combo 는 음수를 내지 않지만, 낸다 해도 전체로 떨어져야 한다.
    const std::vector<std::uint8_t> cats = {56, 22};
    const auto f = cdtb::game::make_filter("", -1, -1, false, cats);
    CHECK_EQ(f.grade, -1);
    CHECK_EQ(f.category, -1);
}

TEST(item_sort_from_specs_maps_columns) {
    auto c = cdtb::game::item_sort_from_specs(1, 4, false);
    CHECK(c.sort == ItemSort::Name);
    CHECK(!c.ascending);
    c = cdtb::game::item_sort_from_specs(1, 2, true);
    CHECK(c.sort == ItemSort::Grade);
    c = cdtb::game::item_sort_from_specs(1, 3, true);
    CHECK(c.sort == ItemSort::Category);
    c = cdtb::game::item_sort_from_specs(1, 1, true);
    CHECK(c.sort == ItemSort::Key);
    CHECK(c.ascending);
}

TEST(item_sort_from_specs_cleared_returns_to_key_ascending) {
    // 정렬 해제(사양 0개)는 마지막 정렬을 남기지 않고 키 오름차순으로 돌아간다
    const auto c = cdtb::game::item_sort_from_specs(0, 4, false);
    CHECK(c.sort == ItemSort::Key);
    CHECK(c.ascending);
}
