#include "render/layout_table.h"

#include <cstddef>

namespace cdtb::render {
namespace {

// 1920x1080 기준. 기본 열림 4창 + 본창은 서로 안 겹치고, 필요할 때 여는 4창은
// 본창을 안 덮고 서로 포개지지 않는다(layout_tests). 아이템 목록 x 는 400 이었는데
// 본창(60+360=420)과 20px 겹쳐 440 으로 옮겼다.
constexpr WindowSpec kSpecs[] = {
    {Win::Main, "CDToybox", "", true, 60, 60, 360, 260, 320, 200},
    {Win::Items, "아이템 목록", "아이템 목록", true, 440, 60, 760, 520, 430, 240},
    {Win::Grant, "아이템 지급", "아이템 지급", true, 1220, 60, 440, 260, 420, 260},
    {Win::Stash, "보관함", "보관함", true, 1220, 340, 420, 400, 420, 300},
    {Win::Inventory, "인벤토리", "인벤토리", true, 440, 600, 760, 420, 720, 300},
    {Win::Roster, "탈것 · 용병 · 캐릭터", "탈것·용병·캐릭터", false, 60, 340, 560,
     520, 560, 360},
    {Win::Equip, "장비 소켓 · 연마 · 염색", "장비 소켓·연마·염색", false, 640, 340,
     560, 420, 560, 300},
    {Win::Player, "플레이어 치트", "플레이어 치트", false, 1220, 760, 320, 220, 320,
     220},
    {Win::Camera, "카메라 분석", "카메라 분석 (진단)", false, 60, 870, 560, 200, 400,
     160},
};
static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == kWinCount);

}  // namespace

const WindowSpec& window_spec(Win w) {
    return kSpecs[static_cast<std::size_t>(w)];
}

std::span<const WindowSpec> window_specs() { return kSpecs; }

bool specs_overlap(const WindowSpec& a, const WindowSpec& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h &&
           b.y < a.y + a.h;
}

}  // namespace cdtb::render
