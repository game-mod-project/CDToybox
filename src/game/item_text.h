#pragma once

#include <string>
#include <string_view>

namespace cdtb::game {

// 게임 아이템 설명을 화면용으로 정리한다(순수 함수, ImGui 를 모른다).
//
// 2949 설명 6736개 전수(specs/2026-09-22-item-description-effects-design.md §3 표기):
//   - 태그는 `<br/>` 하나다(2656회, 연속 196건). '\n' 으로 바꾼다.
//   - 자리표시 `{Staticinfo:<표>:<키>#<표시 문구>}` 56건 - `#` 뒤 표시 문구로 바꾼다.
//     예 `{Staticinfo:Knowledge:Knowledge_Hp#생명}` -> `생명`.
//   - 줄마다 앞뒤 ASCII 공백을 지운다(`<br/>` 옆 공백 10건).
// 다음 자리표시는 전수에 없는 꼴이라 손대지 않고 그대로 둔다: 안 닫힌 것 · `#` 없는 것 ·
// `#` 가 둘 이상인 것 · 표시 문구가 빈 것 · 안에 `{` 가 또 있는 것(그 안의 온전한
// 자리표시는 따로 푼다).
std::string clean_item_desc(std::string_view raw);

// 정리된 설명의 첫 줄 - 설명 칸의 요약이다. 빈 줄은 건너뛴다. 없으면 빈 view.
// 돌려주는 view 는 `cleaned` 를 가리킨다 - 원본이 살아 있는 동안만 쓴다.
std::string_view desc_first_line(std::string_view cleaned);

}  // namespace cdtb::game
