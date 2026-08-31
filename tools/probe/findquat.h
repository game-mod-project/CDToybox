#pragma once

#include <cstddef>

namespace cdtb::probe { class Remote; }

// 시점을 돌리는 동안 값이 변하는 단위 쿼터니언을 찾는다.
//
// 카메라 회전이 메모리 어디에 있는지 알아내는 도구다. 위치와 달리
// 회전은 정적 분석으로 못 찾았고, 하드웨어 브레이크포인트는 이
// 게임의 보호 코드가 무력화한다. 남은 길은 값의 모양으로 찾는 것이고,
// 단위 쿼터니언은 서명이 강하다.
//
// 쓰는 법: 캐릭터를 세워 두고 마우스로 시점만 돌리는 동안 부른다.
// 위치가 안 변하면 변하는 쿼터니언은 회전뿐이다.
void cmd_findquat(const cdtb::probe::Remote& r, unsigned wait_ms,
                  std::size_t max_report);
