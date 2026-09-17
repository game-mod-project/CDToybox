#pragma once

#include <span>

// 창 11개의 제목·본창 라벨·기본 위치·크기·하한을 한 표에 둔다.
//
// 예전엔 창마다 SetNextWindowPos/Size 를 제각각 불렀고 셋(로스터·장비·
// 플레이어)은 위치를 안 정해 ImGui 기본값 (60,60) - 본창 자리 - 에 포개졌다.
// 본창 체크박스 글자도 창 제목과 어긋났다. 표 하나면 둘 다 구조로 맞는다.
// ImGui 를 안 쓰므로 테스트가 불변식(겹침·화면 안)을 검사한다.
namespace cdtb::render {

enum class Win {
    Main, Items, Grant, Stash, Inventory, Roster, Equip, Player, Vehicle, Camera,
    Log, Count
};
inline constexpr int kWinCount = static_cast<int>(Win::Count);

struct WindowSpec {
    Win id;
    const char* title;   // ImGui 창 제목(= ImGui ID)
    const char* label;   // 본창 체크박스 글자. 본창 자신은 ""
    // 기본 배치 묶음. 함께 쓰일 법한 창들이라 서로 겹치지 않게 자리를
    // 잡는다(layout_tests 가 이 묶음과 나머지 묶음을 각각 검사한다).
    // **시작 시 열리는가와는 무관하다** - 최초 실행에는 본창만 열리고,
    // 그 뒤로는 사용자가 마지막에 둔 상태를 기억한다(ui_persist).
    // 열 창을 전부 안 겹치게 놓을 자리는 1920 폭에 없어서 묶음이 둘이다.
    bool primary;
    float x, y, w, h;
    float min_w, min_h;
};

const WindowSpec& window_spec(Win w);
std::span<const WindowSpec> window_specs();

// 두 창의 기본 사각형이 겹치는가. 맞닿기만 하면 안 겹친 것.
bool specs_overlap(const WindowSpec& a, const WindowSpec& b);

}  // namespace cdtb::render
