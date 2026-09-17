#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "game/items.h"

namespace cdtb::game {

// 목록을 거르고·정렬하고·쪽으로 나눈다. ImGui 를 모른다.
//
// UI 와 떼어 놓은 이유는 여기가 틀리기 쉬운 곳이기 때문이다 -
// 페이지 경계, 걸러서 개수가 줄었을 때의 페이지 범위 같은 것은
// 화면으로 확인하기보다 테스트로 잡는 편이 확실하다.

struct ItemFilter {
    std::string query;          // 이름 또는 키에 걸린다
    bool hide_unnamed = false;  // 현지화 표에 없는 것을 감춘다
    int grade = -1;             // -1 = 전체, 0 = 등급 없음, 1..5 = T1..T5
    int category = -1;          // -1 = 전체
    // -1 = 전체, 그 외는 EquipOwner 값(0 공용 · 1 클리프 · 2 데미안 ·
    // 3 웅카). 비장비는 늘 공용이라 "공용" 을 고르면 같이 남는다.
    int owner = -1;
    // query 를 키 문자열에도 거는가. 아이템 목록은 건다("이름 또는 키로
    // 검색"). 인벤토리는 이름만 본다 - 이 차이를 숨기면 인벤에서 숫자를
    // 쳤을 때 동작이 조용히 바뀐다.
    bool match_key = true;
};

// 한 항목이 필터를 통과하는가. filter_items 가 이것을 부르고, 인벤토리
// 창은 자기 행에 직접 건다 - 거르는 규칙이 두 곳에 있으면 갈라진다.
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key, EquipOwner owner);

// Combo 색인을 필터로 옮긴다. 색인 0 은 "전체". 등급은 색인-1 이고,
// 분류는 `categories[색인-1]` 이다(render 의 build_category_labels 가
// 만든 표). 색인이 표를 넘으면 전체로 떨어진다 - 카탈로그가 새 판으로
// 갈리면 그럴 수 있다. 아이템 목록과 인벤토리에 글자 그대로 복제돼
// 있던 것을 여기 한 곳으로 모았다.
// owner_idx 는 0 = 전체, 1..4 = 공용·클리프·데미안·웅카 (EquipOwner 차례).
ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories,
                       int owner_idx);

enum class ItemSort { Key, Name, NameKey, Grade, Category, Owner };

// 조건에 맞는 항목의 포인터를 모은다. 원본 순서를 지킨다.
//
// 포인터를 담아도 되는 이유는 목록이 준비된 뒤 다시 바뀌지 않기
// 때문이다(game::item_catalog 참고).
std::vector<const ItemCatalogEntry*> filter_items(
    const std::vector<ItemCatalogEntry>& all, const ItemFilter& filter);

// 이름 없는 항목은 오름·내림 어느 쪽이든 뒤로 보낸다. 앞에 몰리면
// 목록이 쓸모없어진다.
void sort_items(std::vector<const ItemCatalogEntry*>& items, ItemSort by,
                bool ascending);

struct ItemSortChoice {
    ItemSort sort = ItemSort::Key;
    bool ascending = true;
};

// 표 머리글 정렬 사양을 ItemSort 로 옮긴다. 0번 열은 별표(정렬 없음)라 1번이
// 키, 2 등급, 3 분류, **4 전용, 5 이름**이다. count 가 0(정렬 해제)이면 키
// 오름차순으로 돌아간다 - 예전에는 해제를 무시해 마지막 정렬이 그대로 남았다.
//
// 이 색인은 item_panel 의 TableSetupColumn 차례와 **같아야 한다**. 열을
// 끼우면 여기도 같이 밀어야 하고, 안 그러면 머리글을 눌렀을 때 엉뚱한
// 기준으로 정렬된다.
ItemSortChoice item_sort_from_specs(int count, int column, bool ascending);

// 전체 쪽수. 빈 목록도 한 쪽으로 친다 - 0 이면 UI 가 0/0 을 낸다.
std::size_t page_count(std::size_t total, std::size_t per_page);

struct PageRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// page 는 0 부터. 범위를 넘으면 마지막 쪽으로 당긴다.
PageRange page_range(std::size_t total, std::size_t page,
                     std::size_t per_page);

}  // namespace cdtb::game
