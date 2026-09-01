#pragma once

#include <cstddef>
#include <string>
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
};

enum class ItemSort { Key, Name, NameKey };

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
