#pragma once

#include <cstddef>

namespace cdtb::render {

// ready 가 false 면 점 애니메이션 + "<what> 읽는 중..." 과 고정 문단을 그리고
// false 를 낸다(부른 쪽이 ImGui::End 한다). total 이 0 보다 크면
// "<what> 불러오는 중...  (done / total)" 로 진행 수를 낸다.
//
// 아이템 목록에 있던 것을 창 8개가 같이 쓰게 뺐다. 지급 창은 회색 한 줄,
// 보관함은 아무것도 없어 창마다 로딩이 달라 보였다.
bool loading_gate(bool ready, const char* what, std::size_t done = 0,
                  std::size_t total = 0);

// 표 안에 한 줄. 화면에 첫째로 보이는 열에 text 를 회색으로 쓰되 클립을 표 안쪽
// 전체로 넓혀 열 폭에 잘리지 않게 한다(사용자가 열을 좁히거나 순서를 바꿔 두면
// 한 열 안에서는 문구와 버튼이 잘렸다 - 화면 검증 2026-09-11). action 이 있으면
// 그 옆에 SmallButton 을 붙인다. 버튼이 눌리면 true.
// BeginTable 안, TableHeadersRow 뒤에서 부른다.
bool table_empty_row(const char* text, const char* action = nullptr);

}  // namespace cdtb::render
