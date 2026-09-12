#include "game/item_view.h"

#include <algorithm>
#include <cstdio>

namespace cdtb::game {
namespace {

int compare_by(const ItemCatalogEntry& a, const ItemCatalogEntry& b,
               ItemSort by) {
    switch (by) {
        case ItemSort::Name:
            return a.name.compare(b.name);
        case ItemSort::NameKey:
            if (a.name_key < b.name_key) return -1;
            return (a.name_key > b.name_key) ? 1 : 0;
        case ItemSort::Grade:
            if (a.grade < b.grade) return -1;
            return (a.grade > b.grade) ? 1 : 0;
        case ItemSort::Category:
            if (a.category < b.category) return -1;
            return (a.category > b.category) ? 1 : 0;
        case ItemSort::Key:
        default:
            if (a.key < b.key) return -1;
            return (a.key > b.key) ? 1 : 0;
    }
}

}  // namespace

bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key) {
    if (f.hide_unnamed && name.empty()) return false;
    if (f.grade >= 0 && grade != f.grade) return false;
    if (f.category >= 0 && category != f.category) return false;
    if (f.query.empty()) return true;
    if (!name.empty() && name.find(f.query) != std::string_view::npos) {
        return true;
    }
    // 이름과 키 둘 다에 건다. 지급 대상을 키로만 아는 경우가 있다.
    if (!f.match_key) return false;
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%u", key);
    return std::string_view(digits).find(f.query) != std::string_view::npos;
}

ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories) {
    ItemFilter f;
    f.query.assign(query);
    f.hide_unnamed = hide_unnamed;
    f.grade = (grade_idx <= 0) ? -1 : grade_idx - 1;
    const bool cat_ok = category_idx > 0 &&
                        category_idx <= static_cast<int>(categories.size());
    f.category = cat_ok
                     ? categories[static_cast<std::size_t>(category_idx - 1)]
                     : -1;
    return f;
}

std::vector<const ItemCatalogEntry*> filter_items(
    const std::vector<ItemCatalogEntry>& all, const ItemFilter& filter) {
    std::vector<const ItemCatalogEntry*> out;
    out.reserve(all.size());
    for (const auto& e : all) {
        if (!passes(filter, e.name, e.grade, e.category, e.key)) continue;
        out.push_back(&e);
    }
    return out;
}

void sort_items(std::vector<const ItemCatalogEntry*>& items, ItemSort by,
                bool ascending) {
    // 안정 정렬이라 같은 값끼리는 원래(게임 표) 순서를 지킨다.
    std::stable_sort(
        items.begin(), items.end(),
        [by, ascending](const ItemCatalogEntry* a, const ItemCatalogEntry* b) {
            // 이름으로 정렬할 때만 이름 없는 것을 뒤로 보낸다. 빈
            // 문자열이 앞에 몰리면 목록이 쓸모없어진다. 키로 정렬할
            // 때는 순수한 키 순서여야 하므로 건드리지 않는다.
            if (by == ItemSort::Name) {
                const bool ea = a->name.empty();
                const bool eb = b->name.empty();
                if (ea != eb) return !ea;
            }
            const int cmp = compare_by(*a, *b, by);
            return ascending ? (cmp < 0) : (cmp > 0);
        });
}

ItemSortChoice item_sort_from_specs(int count, int column, bool ascending) {
    ItemSortChoice c;
    if (count <= 0) return c;
    switch (column) {
        case 2: c.sort = ItemSort::Grade; break;
        case 3: c.sort = ItemSort::Category; break;
        case 4: c.sort = ItemSort::Name; break;
        default: c.sort = ItemSort::Key; break;
    }
    c.ascending = ascending;
    return c;
}

std::size_t page_count(std::size_t total, std::size_t per_page) {
    if (per_page == 0 || total == 0) return 1;
    return (total + per_page - 1) / per_page;
}

PageRange page_range(std::size_t total, std::size_t page,
                     std::size_t per_page) {
    PageRange r;
    if (total == 0 || per_page == 0) return r;
    // 걸러서 개수가 줄면 현재 쪽이 범위를 넘을 수 있다. 마지막
    // 쪽으로 당긴다 - 빈 화면을 내는 것보다 낫다.
    const std::size_t pages = page_count(total, per_page);
    const std::size_t p = (page >= pages) ? pages - 1 : page;
    r.begin = p * per_page;
    r.end = std::min(r.begin + per_page, total);
    return r;
}

}  // namespace cdtb::game
