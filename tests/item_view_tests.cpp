#include <string>
#include <vector>

#include "game/item_view.h"
#include "harness.h"

namespace {

using cdtb::game::EquipOwner;
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
    // 판정을 내야 한다 - 이름·설명·키·이름없음·등급·분류를 전부 돈다.
    const auto named = [] {
        auto v = sample();
        v[2].desc = "나무를 베는 데 쓴다";   // 설명으로만 걸리는 줄
        return v;
    }();
    const auto tiers = graded();

    ItemFilter fs[7];
    fs[1].query = "화살";
    fs[2].query = "9500";
    fs[3].hide_unnamed = true;
    fs[4].grade = 0;
    fs[5].category = 56;
    fs[6].query = "나무를";

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
                                                  e.category, e.key, e.owner,
                                                  e.desc);
                CHECK_EQ(p, in);
                if (p) ++n;
            }
            CHECK_EQ(n, out.size());
        }
    }
}

TEST(passes_ignores_key_when_match_key_is_off) {
    // 인벤토리는 키를 안 본다(이름 · 설명만). 숫자를 쳐도 키가 걸리면 안 된다.
    ItemFilter f;
    f.query = "9500";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u, EquipOwner::Shared));
    f.match_key = true;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u, EquipOwner::Shared));
}

TEST(passes_with_key_off_still_matches_name) {
    ItemFilter f;
    f.query = "도끼";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u, EquipOwner::Shared));
}

TEST(passes_with_key_off_drops_unnamed_even_if_key_matches) {
    // 이름 없는 것은 키로만 찾을 수 있는데, 키를 안 보면 못 찾는다.
    ItemFilter f;
    f.query = "200997";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u, EquipOwner::Shared));
}

TEST(passes_matches_the_description_text) {
    ItemFilter f;
    f.query = "관통력";
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared,
                             "막강한 관통력과 파괴력"));
    CHECK(!cdtb::game::passes(f, "화살", 0, 0, 50001u, EquipOwner::Shared,
                              "기본 화살"));
}

TEST(passes_matches_description_even_when_key_is_off) {
    // 인벤토리는 키로는 안 찾지만 설명으로는 찾는다(스펙 §5).
    ItemFilter f;
    f.query = "관통력";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared,
                             "막강한 관통력"));
}

TEST(passes_still_hides_unnamed_even_if_the_description_matches) {
    ItemFilter f;
    f.query = "관통력";
    f.hide_unnamed = true;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u, EquipOwner::Shared,
                              "관통력"));
}

TEST(filter_items_matches_the_catalog_description) {
    auto all = sample();
    all[2].desc = "나무를 베는 데 쓴다";
    ItemFilter f;
    f.query = "나무를";
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 950002u);
}

// ------------------------------------------------- Combo 색인 -> 필터

TEST(make_filter_index_zero_means_all) {
    const std::vector<std::uint8_t> cats = {56, 22};
    const auto f = cdtb::game::make_filter("", 0, 0, false, cats, 0);
    CHECK_EQ(f.grade, -1);
    CHECK_EQ(f.category, -1);
    CHECK(f.query.empty());
    CHECK(!f.hide_unnamed);
    CHECK(f.match_key);
}

TEST(make_filter_grade_index_is_one_past_the_grade) {
    // Combo 는 0 이 "전체" 라 등급이 한 칸 밀려 있다. 1 이 등급 0(없음).
    const std::vector<std::uint8_t> cats;
    CHECK_EQ(cdtb::game::make_filter("", 1, 0, false, cats, 0).grade, 0);
    CHECK_EQ(cdtb::game::make_filter("", 6, 0, false, cats, 0).grade, 5);
}

TEST(make_filter_category_index_looks_up_the_table) {
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, cats, 0).category, 56);
    CHECK_EQ(cdtb::game::make_filter("", 0, 2, false, cats, 0).category, 22);
}

TEST(make_filter_category_index_past_the_table_means_all) {
    // 카탈로그가 새 판으로 갈리면 Combo 색인이 표 길이를 넘을 수 있다.
    // 그때 배열 밖을 읽지 말고 "전체" 로 떨어져야 한다.
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 3, false, cats, 0).category, -1);
    const std::vector<std::uint8_t> none;
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, none, 0).category, -1);
}

TEST(make_filter_copies_query_and_hide_unnamed) {
    const std::vector<std::uint8_t> cats;
    const auto f = cdtb::game::make_filter("화살", 0, 0, true, cats, 0);
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
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u, EquipOwner::Shared));
    const auto all = sample();
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(0));
}

TEST(make_filter_negative_index_means_all) {
    // Combo 는 음수를 내지 않지만, 낸다 해도 전체로 떨어져야 한다.
    const std::vector<std::uint8_t> cats = {56, 22};
    const auto f = cdtb::game::make_filter("", -1, -1, false, cats, 0);
    CHECK_EQ(f.grade, -1);
    CHECK_EQ(f.category, -1);
}

TEST(item_sort_from_specs_maps_columns) {
    // "전용" 열이 4 로 들어오면서 이름이 5 로 밀렸다.
    auto c = cdtb::game::item_sort_from_specs(1, 5, false);
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

// --- 캐릭터 전용 구분: 필터와 정렬 ------------------------------------
//
// 컬럼을 "분류" 뒤(색인 4)에 끼우므로 "이름" 이 5 로 밀린다. 열 색인은
// 화면과 정렬 사이의 계약이라 시험으로 못박아 둔다.


TEST(filter_owner_all_passes_everything) {
    cdtb::game::ItemFilter f;
    f.owner = -1;   // 전체
    CHECK(cdtb::game::passes(f, "투구", 5, 3, 1, EquipOwner::Demian));
    CHECK(cdtb::game::passes(f, "투구", 5, 3, 1, EquipOwner::Shared));
}

TEST(filter_owner_keeps_only_the_chosen_character) {
    cdtb::game::ItemFilter f;
    f.owner = static_cast<int>(EquipOwner::Demian);
    CHECK(cdtb::game::passes(f, "황금 광휘 판금 투구", 5, 3, 1,
                             EquipOwner::Demian));
    CHECK(!cdtb::game::passes(f, "디스카오 판금 투구", 3, 3, 2,
                              EquipOwner::Shared));
    CHECK(!cdtb::game::passes(f, "벨칸드 판금 투구", 4, 3, 3,
                              EquipOwner::Oongka));
}

TEST(filter_owner_can_select_shared_only) {
    // "공용만" 은 전용 장비를 다 걷어낸다 - 셋 중 아무에게도 안 묶인 것.
    cdtb::game::ItemFilter f;
    f.owner = static_cast<int>(EquipOwner::Shared);
    CHECK(cdtb::game::passes(f, "비지오네", 1, 3, 1, EquipOwner::Shared));
    CHECK(!cdtb::game::passes(f, "카이로스 판금 투구", 5, 3, 2,
                              EquipOwner::Kliff));
}

TEST(sort_items_by_owner_groups_characters) {
    std::vector<cdtb::game::ItemCatalogEntry> v(4);
    v[0].key = 1; v[0].owner = EquipOwner::Oongka;
    v[1].key = 2; v[1].owner = EquipOwner::Shared;
    v[2].key = 3; v[2].owner = EquipOwner::Demian;
    v[3].key = 4; v[3].owner = EquipOwner::Kliff;
    std::vector<const cdtb::game::ItemCatalogEntry*> p;
    for (const auto& e : v) p.push_back(&e);
    cdtb::game::sort_items(p, cdtb::game::ItemSort::Owner, true);
    // Shared(0) < Kliff(1) < Demian(2) < Oongka(3)
    CHECK_EQ(p[0]->key, 2u);
    CHECK_EQ(p[1]->key, 4u);
    CHECK_EQ(p[2]->key, 3u);
    CHECK_EQ(p[3]->key, 1u);
}

TEST(item_sort_from_specs_maps_the_owner_column) {
    auto c = cdtb::game::item_sort_from_specs(1, 4, true);
    CHECK(c.sort == ItemSort::Owner);
    // 이름이 5 로 밀렸다
    c = cdtb::game::item_sort_from_specs(1, 5, false);
    CHECK(c.sort == ItemSort::Name);
    CHECK(!c.ascending);
}

// ------------------------------------------------------------ 효과로 검색

TEST(passes_matches_the_effect_text) {
    // 스펙 §5: 이름 · 설명에 더해 효과 문구에도 걸린다.
    ItemFilter f;
    f.query = "치명타";
    CHECK(cdtb::game::passes(f, "관통 I", 0, 0, 1003765u, EquipOwner::Shared,
                             "어비스 기어입니다",
                             "천 갑옷 타격 시 치명타 확률 2% 증가"));
    CHECK(!cdtb::game::passes(f, "화살", 0, 0, 50001u, EquipOwner::Shared,
                              "기본 화살", "공격력 3 증가"));
}

TEST(passes_matches_the_effect_text_even_when_key_is_off) {
    // 인벤토리는 키로는 안 찾지만(match_key=false) 효과로는 찾는다.
    ItemFilter f;
    f.query = "생명";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "하급 비약", 0, 0, 751123u, EquipOwner::Shared,
                             "", "생명 500 회복 (1분)"));
}

TEST(passes_does_not_match_the_effect_when_the_snapshot_is_missing) {
    // 스냅샷이 아직이면 효과 문구가 비어 온다 - 그냥 안 걸리면 된다.
    ItemFilter f;
    f.query = "치명타";
    CHECK(!cdtb::game::passes(f, "관통 I", 0, 0, 1003765u, EquipOwner::Shared,
                              "어비스 기어입니다"));
}

TEST(passes_still_hides_unnamed_even_if_the_effect_matches) {
    ItemFilter f;
    f.query = "치명타";
    f.hide_unnamed = true;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u, EquipOwner::Shared, "",
                              "치명타 확률 2% 증가"));
}

TEST(passes_keeps_matching_name_and_desc_alongside_the_effect) {
    // 효과를 끼워 넣어도 예전 세 갈래(이름 · 설명 · 키)가 그대로여야 한다.
    ItemFilter f;
    f.query = "관통력";
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared,
                             "막강한 관통력", "공격력 3 증가"));
    f.query = "편전";
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared, "",
                             "공격력 3 증가"));
    f.query = "2200";
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared, "",
                             "공격력 3 증가"));
}

// --------------------------------------------------------- 툴팁 절 차례

namespace {

using cdtb::game::ItemEffect;
using cdtb::game::ItemEffects;
using cdtb::game::TooltipSection;

ItemEffects two_lines() {
    ItemEffects fx;
    fx.lines.push_back(ItemEffect{"생명 500 회복 (1분)", 60000});
    fx.lines.push_back(ItemEffect{"치명타 확률 2% 증가", 0});
    return fx;
}

std::vector<TooltipSection::Kind> kinds(
    const std::vector<TooltipSection>& s) {
    std::vector<TooltipSection::Kind> out;
    for (const auto& x : s) out.push_back(x.kind);
    return out;
}

}  // namespace

TEST(tooltip_sections_orders_effects_then_equip_then_desc) {
    // 게임 툴팁 차례: 효과 줄들 -> 장착 부위 -> 구분선 -> 설명 전문.
    const auto fx = two_lines();
    const std::vector<std::string> types = {"무기", "장갑"};
    const auto s = cdtb::game::tooltip_sections("어비스 기어입니다", &fx,
                                                &types, true);
    const auto k = kinds(s);
    CHECK_EQ(k.size(), static_cast<std::size_t>(5));
    if (k.size() == 5) {
        CHECK(k[0] == TooltipSection::Kind::Effect);
        CHECK(k[1] == TooltipSection::Kind::Effect);
        CHECK(k[2] == TooltipSection::Kind::EquipTypes);
        CHECK(k[3] == TooltipSection::Kind::Separator);
        CHECK(k[4] == TooltipSection::Kind::Desc);
        CHECK_EQ(s[0].text, std::string("생명 500 회복 (1분)"));
        CHECK_EQ(s[2].text, std::string("무기 · 장갑"));
        CHECK_EQ(s[4].text, std::string("어비스 기어입니다"));
    }
}

TEST(tooltip_sections_drops_the_equip_line_when_there_is_none) {
    // 장착 부위가 없는 아이템(해시 0)에 빈 줄을 만들지 않는다.
    const auto fx = two_lines();
    const auto s = cdtb::game::tooltip_sections("음식입니다", &fx, nullptr,
                                                true);
    for (const auto& x : s) {
        CHECK(x.kind != TooltipSection::Kind::EquipTypes);
    }
    const auto k = kinds(s);
    CHECK_EQ(k.size(), static_cast<std::size_t>(4));   // 효과 둘 · 구분선 · 설명
}

TEST(tooltip_sections_has_no_separator_without_effects) {
    // 효과도 부위도 없으면 설명 한 절뿐이다 - 구분선이 홀로 남으면 안 된다.
    const auto s = cdtb::game::tooltip_sections("그냥 재료입니다", nullptr,
                                                nullptr, true);
    CHECK_EQ(s.size(), static_cast<std::size_t>(1));
    if (!s.empty()) {
        CHECK(s[0].kind == TooltipSection::Kind::Desc);
    }
}

TEST(tooltip_sections_has_no_separator_without_desc) {
    // 설명이 없으면 구분선도 없다(위쪽 절만 남는다).
    const auto fx = two_lines();
    const auto s = cdtb::game::tooltip_sections("", &fx, nullptr, true);
    CHECK_EQ(s.size(), static_cast<std::size_t>(2));
    for (const auto& x : s) {
        CHECK(x.kind == TooltipSection::Kind::Effect);
    }
}

TEST(tooltip_sections_appends_the_unresolved_count_last) {
    ItemEffects fx = two_lines();
    fx.unresolved = 3;
    const auto s = cdtb::game::tooltip_sections("설명", &fx, nullptr, true);
    CHECK(!s.empty());
    if (!s.empty()) {
        CHECK(s.back().kind == TooltipSection::Kind::Unresolved);
        CHECK_EQ(s.back().text, std::string("해석 못 한 효과 3개"));
    }
}

TEST(tooltip_sections_omits_the_unresolved_line_when_zero) {
    const auto fx = two_lines();   // unresolved = 0
    const auto s = cdtb::game::tooltip_sections("설명", &fx, nullptr, true);
    for (const auto& x : s) {
        CHECK(x.kind != TooltipSection::Kind::Unresolved);
    }
}

TEST(tooltip_sections_shows_only_the_desc_while_effects_are_pending) {
    // 스냅샷이 아직이면 "효과가 없다" 와 갈라 보여야 한다 - 설명 + 흐린 한 줄.
    const auto fx = two_lines();
    const std::vector<std::string> types = {"무기"};
    const auto s = cdtb::game::tooltip_sections("설명", &fx, &types, false);
    CHECK_EQ(s.size(), static_cast<std::size_t>(2));
    if (s.size() == 2) {
        CHECK(s[0].kind == TooltipSection::Kind::Desc);
        CHECK(s[1].kind == TooltipSection::Kind::Pending);
        CHECK_EQ(s[1].text, std::string("효과를 읽는 중입니다"));
    }
}

TEST(tooltip_sections_skips_blank_effect_lines) {
    ItemEffects fx;
    fx.lines.push_back(ItemEffect{"", 0});
    fx.lines.push_back(ItemEffect{"공격력 3 증가", 0});
    const auto s = cdtb::game::tooltip_sections("", &fx, nullptr, true);
    CHECK_EQ(s.size(), static_cast<std::size_t>(1));
    if (!s.empty()) CHECK_EQ(s[0].text, std::string("공격력 3 증가"));
}

TEST(tooltip_sections_cuts_a_long_equip_list) {
    // equip_types_line 의 "외 N종" 이 그대로 쓰인다(기본 4개).
    const std::vector<std::string> types = {"투구", "갑옷", "장갑", "신발",
                                            "무기", "망토"};
    const auto s = cdtb::game::tooltip_sections("", nullptr, &types, true);
    CHECK_EQ(s.size(), static_cast<std::size_t>(1));
    if (!s.empty()) {
        CHECK(s[0].kind == TooltipSection::Kind::EquipTypes);
        CHECK_EQ(s[0].text,
                 std::string("투구 · 갑옷 · 장갑 · 신발 외 2종"));
    }
}
