#pragma once

#include <imgui.h>

// 창 8개가 같은 뜻에 같은 색을 쓰게 한다. 예전에는 초록이 셋, 노랑이 둘이었고
// 주황이 성공과 거부에 함께 쓰였다. 등급 색은 item_style 의 grade_color 다.
namespace cdtb::render::col {

inline constexpr ImVec4 kOk{0.40f, 0.85f, 0.40f, 1.0f};    // 성공 · 활성 · 걸려 있음
inline constexpr ImVec4 kWarn{0.90f, 0.60f, 0.30f, 1.0f};  // 막힘 · 경고 · 미저장 · 한쪽만
inline constexpr ImVec4 kBad{0.95f, 0.35f, 0.35f, 1.0f};   // 실패 · 치명 · 죽은 세션
inline constexpr ImVec4 kBusy{1.00f, 0.90f, 0.40f, 1.0f};  // 로딩 · 진행 중

}  // namespace cdtb::render::col
