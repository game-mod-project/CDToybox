#pragma once

#include <cstddef>
#include <cstdint>

#include "game/items.h"

namespace cdtb::render {

// 보석 고르기 팝업. 장비 소켓 창과 지급 창이 같은 것을 쓴다.
//
// 상태는 부르는 창이 하나씩 가진다. 두 창이 동시에 열려 있을 수 있는데,
// 열림 요청을 전역 하나에 두면 다른 창의 그리기가 먼저 돌아 그쪽에서
// 팝업이 뜬다.
struct GemPicker {
    bool open_requested = false;
    char search[64] = "";
    int pick = -1;   // 고른 카탈로그 색인. -1 없음, -2 "(빈 칸으로 열기)"
};

// 고른 것. 두 창이 원하는 값이 다르다 - 장비 창은 카탈로그 순번을 쓰고
// (eq_write_socket), 지급 창은 키를 쓴다(g_socket_keys). 같은 카탈로그를
// 보므로 둘을 함께 준다. allow_empty 로 "(빈 칸으로 열기)" 를 고르면
// entry 가 nullptr 이다.
//
// 줄 클릭은 고르기만 하고 [적용] 을 눌러야 true 다 - 장비 창에서는 곧 게임 메모리 쓰기라서.
struct GemChoice {
    const game::ItemCatalogEntry* entry = nullptr;
    std::size_t index = 0;
};

struct GemPickerOpts {
    const char* title = "소켓에 박을 강화 보석(분류 74)";
    bool allow_empty = false;         // "(빈 칸으로 열기)" 행 - 지급 창만
    std::uint32_t selected_key = 0;   // 현재 값. 목록에서 강조한다
};

// 검색어·선택을 비운다. open 이 이것을 부른다. 장비 창의 소켓 팝업은 자기가
// 팝업을 열므로 이것만 쓴다.
void gem_picker_reset(GemPicker* p);

// 다음 gem_picker_draw 에서 팝업을 연다. 버튼이 PushID 안에 있어도
// 된다 - 여는 것은 draw 가 자기 ID 범위에서 한다.
void gem_picker_open(GemPicker* p);

// 팝업 없이 목록만 - 제목·검색·목록·"선택:"·[적용]. 장비 창의 칸 팝업이 안에
// 넣어 쓴다. [적용] 이 눌리면 out 을 채우고 true.
bool gem_list_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out);

// 팝업을 그린다. 창의 최상위 ID 범위에서 매 프레임 부른다. 골랐으면
// true 를 내고 팝업을 닫는다.
bool gem_picker_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out);

}  // namespace cdtb::render
