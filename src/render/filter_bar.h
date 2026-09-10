#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/item_view.h"

namespace cdtb::render {

// 검색 · 지우기 · (이름 없는 것 감추기) · 등급 · 분류 한 줄의 상태.
// 아이템 목록과 인벤토리가 각자 static 으로 들고 있던 것을 합쳤다.
// 부르는 창이 하나씩 소유한다.
struct FilterBar {
    // 128 이다. 아이템 목록이 128, 인벤이 64 를 쓰고 있었다. 64 로
    // 맞추면 아이템 목록에서 긴 검색어가 잘린다 - 넓은 쪽을 쓴다.
    char query[128] = "";
    int grade_idx = 0;        // 0 = 전체, 1 = 등급 없음, 2..6 = T1..T5
    int category_idx = 0;     // 0 = 전체, 그 뒤는 categories 의 색인-1
    // 기본값이 창마다 다르다. 아이템 목록은 켜져 있고 인벤은 이 칸이
    // 아예 없었다. 부르는 쪽이 초기화한다.
    bool hide_unnamed = false;
    std::vector<std::uint8_t> categories;   // 표에 실제로 있는 분류 값
    std::string category_labels;            // Combo 용 널 구분 문자열
};

struct FilterBarOpts {
    const char* id = "filter";            // ImGui ID 범위. 창마다 다르게
    const char* hint = "이름으로 검색";    // 검색창 힌트
    bool show_hide_unnamed = false;       // 아이템 목록만 켠다
    // 검색어를 키 문자열에도 거는가. 힌트 문구와 한 쌍이다 - "이름 또는
    // 키로 검색" 이면 true, "이름으로 검색" 이면 false. 떨어져 있으면
    // 한쪽만 고쳐 어긋난다.
    bool match_key = true;
};

// 한 줄을 그린다. 값이 바뀌었으면 true - 무엇을 할지는 부르는 쪽 일이다
// (아이템 목록은 뷰를 다시 만들고 쪽을 0 으로, 인벤은 매 프레임 거르니
// 아무것도 안 한다). 분류 Combo 는 category_labels 가 비어 있으면 그리지
// 않는다 - 인벤은 읽은 뒤에야 분류가 생긴다.
bool draw_filter_bar(FilterBar* s, const FilterBarOpts& o);

// 표에 실제로 있는 분류로 Combo 표를 다시 만든다. 부르는 쪽이 두 칸을
// 짝지어 채우던 것을 여기서 한다 - 빠뜨리면 분류 Combo 가 조용히 안 뜬다.
void filter_bar_rebuild_categories(FilterBar* s);

// 상태 + 옵션 -> 필터. game::make_filter 로 위임하고 match_key 는
// 옵션에서 가져온다 - 힌트 문구와 같은 자리에서 정해진다.
game::ItemFilter to_filter(const FilterBar& s, const FilterBarOpts& o);

}  // namespace cdtb::render
