#pragma once

#include <cstdint>

namespace cdtb::game {

// 프리카메라.
//
// 게임은 카메라 월드 좌표(컴포넌트+0x360)를 매 프레임
//
//     현재 = lerp(현재, 목표, t),   t = 컴포넌트+0x35C
//
// 로 갱신한다(함수 0x140A58540, 저장은 0x140A58784/0x140A58790).
// 자기 이전 값을 읽어 계산하므로, 우리가 아무 때나 쓰면 그 값이
// lerp 의 '현재' 로 먹혀 다시 목표 쪽으로 끌려간다. 실측에서 Y 를
// 700 으로 써 두니 688 -> 665 -> 642 로 되돌아왔다.
//
// 그래서 그 갱신 함수를 후킹해 **원본을 부른 뒤** 우리 좌표를
// 덮어쓴다. 순서가 전부다. 원본보다 먼저 쓰면 진다.
//
// 갱신 함수는 바이트 패턴으로 찾는다. RVA 를 박으면 게임이 갱신될
// 때마다 깨진다. 패턴은 실행 섹션 전체에서 한 곳만 일치하는 것을
// 확인했다.

// 후킹만 한다. 활성화와는 별개다. 실패하면 false.
bool freecam_install();

// 켜고 끈다. 켤 때 현재 카메라 위치에서 시작한다.
// 끄면 게임이 알아서 원래 자리로 보간해 돌아간다.
void freecam_toggle();

struct FreeCamState {
    bool deferred = false;   // 보류 중이면 훅을 걸지 않는다
    bool hooked = false;
    bool active = false;
    float pos[3]{};
    float speed = 0.0f;
    std::uintptr_t update_fn = 0;   // 찾아낸 갱신 함수 주소
    std::uintptr_t script = 0;      // 렌더가 읽는 카메라 변환 객체
};
FreeCamState freecam_state();

// 프레임마다 부른다. 입력을 읽어 좌표를 움직인다.
void freecam_tick();

}  // namespace cdtb::game
