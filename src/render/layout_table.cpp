#include "render/layout_table.h"

#include <cstddef>

namespace cdtb::render {
namespace {

// 1920x1080 기준. 기본 열림 4창 + 본창은 서로 안 겹치고, 필요할 때 여는 4창은
// 본창을 안 덮고 서로 포개지지 않는다(layout_tests). 아이템 목록 x 는 400 이었는데
// 본창과 겹쳐 밀어냈다. 본창 폭 420 은 2열 체크박스 라벨(가장 긴 것이
// "장비 소켓·연마·염색")이 SameLine(200) 자리에서 안 잘리는 값이고, 그 오른쪽
// 끝 60+420=480 이 아이템 목록의 x 다. 맞닿는 건 겹침이 아니다(specs_overlap
// 은 엄격 비교).
constexpr WindowSpec kSpecs[] = {
    {Win::Main, "CDToybox", "", true, 60, 60, 420, 260, 420, 200},
    {Win::Items, "아이템 목록", "아이템 목록", true, 480, 60, 760, 520, 430, 240},
    {Win::Grant, "아이템 지급", "아이템 지급", true, 1240, 60, 440, 260, 420, 260},
    {Win::Stash, "보관함", "보관함", true, 1240, 340, 420, 400, 420, 300},
    {Win::Inventory, "인벤토리", "인벤토리", true, 480, 600, 760, 420, 720, 300},
    {Win::Roster, "탈것 · 용병 · 캐릭터", "탈것·용병·캐릭터", false, 60, 340, 560,
     520, 560, 360},
    {Win::Equip, "장비 소켓 · 연마 · 염색", "장비 소켓·연마·염색", false, 640, 340,
     560, 420, 560, 300},
    {Win::Player, "플레이어 치트", "플레이어 치트", false, 1240, 760, 320, 220, 320,
     220},
    // 드래곤·A.T.A.G. 는 플레이어 치트에서 떼어 낸 창이다(2026-09-16). 토글이
    // 여덟까지 늘어 그 창에서 가장 큰 덩어리였고, 성격도 다르다 - 플레이어 치트는
    // 내 캐릭터, 이쪽은 동반자와 탈것이다. 자리는 지급·보관함(기본 열림)의
    // **위**를 쓰되 플레이어 치트(y 760~)와 안 겹치게 740 에서 끊는다.
    {Win::Vehicle, "드래곤 · A.T.A.G.", "드래곤·A.T.A.G.", false, 1240, 60, 420,
     680, 400, 400},
    {Win::Camera, "카메라 분석", "카메라 분석 (진단)", false, 60, 870, 560, 200, 400,
     160},
    // 장비(640,340~760)의 **아래**, 플레이어 치트(x 1240~)의 **왼쪽** 빈 자리다.
    // 로스터·카메라는 x 620 에서 끝나므로 640 부터는 비어 있다. 본창과도 안 겹친다
    // (layout_tests 가 이 넷을 전부 검사한다).
    {Win::Log, "로그", "로그 (실시간)", false, 640, 780, 560, 280, 400, 160},
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
