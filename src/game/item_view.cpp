#include "game/item_view.h"

#include <algorithm>
#include <cstdio>

#include "game/equip_types.h"   // equip_types_line

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
        case ItemSort::Owner: {
            // 열거 차례가 곧 정렬 차례다 - 공용 · 클리프 · 데미안 · 웅카.
            const int ao = static_cast<int>(a.owner);
            const int bo = static_cast<int>(b.owner);
            if (ao < bo) return -1;
            return (ao > bo) ? 1 : 0;
        }
        case ItemSort::Key:
        default:
            if (a.key < b.key) return -1;
            return (a.key > b.key) ? 1 : 0;
    }
}

}  // namespace

bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key, EquipOwner owner,
            std::string_view text, std::string_view effects) {
    if (f.hide_unnamed && name.empty()) return false;
    if (f.grade >= 0 && grade != f.grade) return false;
    if (f.category >= 0 && category != f.category) return false;
    if (f.owner >= 0 && static_cast<int>(owner) != f.owner) return false;
    if (f.query.empty()) return true;
    if (!name.empty() && name.find(f.query) != std::string_view::npos) {
        return true;
    }
    // 설명에도 건다(스펙 §5). 키를 안 보는 창(인벤토리)에서도 설명은 본다.
    if (!text.empty() && text.find(f.query) != std::string_view::npos) {
        return true;
    }
    // 효과 문구에도 건다(스펙 §5). 스냅샷이 아직이면 비어 와서 안 걸린다.
    if (!effects.empty() && effects.find(f.query) != std::string_view::npos) {
        return true;
    }
    // 키에도 건다(match_key 일 때). 지급 대상을 키로만 아는 경우가 있다.
    if (!f.match_key) return false;
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%u", key);
    return std::string_view(digits).find(f.query) != std::string_view::npos;
}

ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories,
                       int owner_idx) {
    ItemFilter f;
    f.query.assign(query);
    f.hide_unnamed = hide_unnamed;
    f.grade = (grade_idx <= 0) ? -1 : grade_idx - 1;
    // 색인 0 = 전체, 1..4 = Shared·Kliff·Demian·Oongka (EquipOwner 순서).
    f.owner = (owner_idx <= 0) ? -1 : owner_idx - 1;
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
    // 효과 문구는 아이템 표와 따로 게시된 스냅샷에서 온다(명세 §4.5).
    // 스냅샷이 아직이면 전부 nullptr 이라 효과로는 안 걸린다. 검색어가
    // 없으면 아예 조회하지 않는다 - 6,816개를 공짜로 훑을 이유가 없다.
    const bool fx_ready = !filter.query.empty() && item_effects_ready();
    for (const auto& e : all) {
        const std::string* fx =
            fx_ready ? item_effects_text_for(e.key) : nullptr;
        if (!passes(filter, e.name, e.grade, e.category, e.key, e.owner, e.desc,
                    fx == nullptr ? std::string_view{} : std::string_view(*fx))) {
            continue;
        }
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
    // 열 색인은 화면(item_panel 의 TableSetupColumn 차례)과의 계약이다.
    // "전용" 을 분류 뒤에 끼우면서 이름이 4 -> 5 로 밀렸다.
    switch (column) {
        case 2: c.sort = ItemSort::Grade; break;
        case 3: c.sort = ItemSort::Category; break;
        case 4: c.sort = ItemSort::Owner; break;
        case 5: c.sort = ItemSort::Name; break;
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

// ------------------------------------------------------------- 설명 툴팁

std::string unresolved_line(int count) {
    return "해석 못 한 효과 " + std::to_string(count) + "개";
}

std::vector<TooltipSection> tooltip_sections(
    std::string_view desc, const ItemEffects* effects,
    const std::vector<std::string>* equip_types, bool effects_ready) {
    std::vector<TooltipSection> out;

    // 효과 · 장착 부위는 스냅샷이 선 뒤에만 본다. 준비 전에 들어온 것은
    // 반쯤 채워진 판일 수 있어 아예 안 그린다(맨 아래 Pending 으로 알린다).
    if (effects_ready && effects != nullptr) {
        for (const auto& line : effects->lines) {
            if (line.text.empty()) continue;
            out.push_back({TooltipSection::Kind::Effect, line.text});
        }
    }
    if (effects_ready && equip_types != nullptr && !equip_types->empty()) {
        std::string line = equip_types_line(*equip_types);
        if (!line.empty()) {
            out.push_back({TooltipSection::Kind::EquipTypes, std::move(line)});
        }
    }

    // 구분선은 위에 뭔가 있고 아래에 설명이 있을 때만이다 - 빈 칸 하나만
    // 남는 줄을 만들지 않는다.
    if (!out.empty() && !desc.empty()) {
        out.push_back({TooltipSection::Kind::Separator, {}});
    }
    if (!desc.empty()) {
        out.push_back({TooltipSection::Kind::Desc, std::string(desc)});
    }

    if (!effects_ready) {
        out.push_back({TooltipSection::Kind::Pending,
                       std::string(kEffectsPendingText)});
    } else if (effects != nullptr && effects->unresolved > 0) {
        out.push_back({TooltipSection::Kind::Unresolved,
                       unresolved_line(effects->unresolved)});
    }
    return out;
}

}  // namespace cdtb::game
