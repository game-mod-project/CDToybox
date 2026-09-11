#include <string>
#include <vector>

#include "harness.h"
#include "render/table_sort.h"
#include "render/view_cache.h"

namespace {

using cdtb::render::CachedView;
using cdtb::render::cmp3;
using cdtb::render::flag_bits;
using cdtb::render::SortSpec;
using cdtb::render::sort_view;
using cdtb::render::ViewKey;

struct Row {
    int id;
    std::string name;
    long long n;
};

int row_cmp(const Row& a, const Row& b, int column) {
    switch (column) {
        case 0: return cmp3(a.id, b.id);
        case 1: return cmp3(a.name, b.name);
        default: return cmp3(a.n, b.n);
    }
}

std::vector<Row> sample() {
    return {{3, "나", 10}, {1, "가", 30}, {2, "다", 20}, {4, "가", 5}};
}

}  // namespace

// ---------------------------------------------------------------- 표 정렬
TEST(table_sort_no_column_keeps_order) {
    auto v = sample();
    sort_view(v, SortSpec{}, row_cmp);
    CHECK_EQ(v[0].id, 3);
    CHECK_EQ(v[3].id, 4);
}

TEST(table_sort_by_int_asc_desc) {
    auto v = sample();
    sort_view(v, SortSpec{0, true}, row_cmp);
    CHECK_EQ(v[0].id, 1);
    CHECK_EQ(v[3].id, 4);
    sort_view(v, SortSpec{0, false}, row_cmp);
    CHECK_EQ(v[0].id, 4);
    CHECK_EQ(v[3].id, 1);
}

TEST(table_sort_by_string_is_stable) {
    auto v = sample();
    sort_view(v, SortSpec{1, true}, row_cmp);
    // "가" 둘은 원래 순서(1 앞, 4 뒤)를 지킨다
    CHECK_EQ(v[0].id, 1);
    CHECK_EQ(v[1].id, 4);
    CHECK_EQ(v[3].id, 2);
}

TEST(table_sort_by_long_long) {
    auto v = sample();
    sort_view(v, SortSpec{2, true}, row_cmp);
    CHECK_EQ(v[0].n, 5LL);
    CHECK_EQ(v[3].n, 30LL);
}

TEST(cmp3_values) {
    CHECK_EQ(cmp3(1LL, 2LL), -1);
    CHECK_EQ(cmp3(2LL, 2LL), 0);
    CHECK_EQ(cmp3(3LL, 2LL), 1);
    CHECK_EQ(cmp3(std::string("a"), std::string("b")), -1);
    CHECK_EQ(cmp3(std::string("b"), std::string("b")), 0);
    CHECK_EQ(cmp3(std::string("c"), std::string("b")), 1);
}

// ---------------------------------------------------------------- 뷰 캐시
TEST(view_cache_rebuilds_only_when_key_changes) {
    CachedView<int> cv;
    ViewKey k;
    k.query = "a";
    k.generation = &k;
    k.count = 3;
    CHECK(cv.begin(k));          // 처음은 만든다
    cv.rows.push_back(nullptr);
    cv.total = 7;
    CHECK(!cv.begin(k));         // 같은 키면 그대로
    CHECK_EQ(cv.rows.size(), static_cast<std::size_t>(1));
    CHECK_EQ(cv.total, static_cast<std::size_t>(7));

    ViewKey k2 = k;
    k2.sort.column = 1;
    CHECK(cv.begin(k2));         // 정렬이 바뀌면 다시
    CHECK(cv.rows.empty());
    CHECK_EQ(cv.total, static_cast<std::size_t>(0));
    ViewKey k3 = k2;
    k3.flags = flag_bits({true, false});
    CHECK(cv.begin(k3));         // 체크박스
    ViewKey k4 = k3;
    k4.count = 5;
    CHECK(cv.begin(k4));         // 원본 크기
    ViewKey k5 = k4;
    k5.stamp = 2.5;
    CHECK(cv.begin(k5));         // 원본 갱신 시각
    ViewKey k6 = k5;
    k6.type = 4;
    CHECK(cv.begin(k6));         // 타입 필터
}

TEST(flag_bits_packs_in_order) {
    CHECK_EQ(flag_bits({}), 0u);
    CHECK_EQ(flag_bits({true}), 1u);
    CHECK_EQ(flag_bits({false, true, true}), 6u);
}
