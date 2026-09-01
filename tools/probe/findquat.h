#pragma once

#include <cstddef>
#include <cstdint>

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

// 주어진 주소에 float3 를 지정 시간 동안 반복해서 쓴다.
//
// 어느 메모리가 렌더 카메라를 구동하는지 가리는 도구다. 게임이 매
// 프레임 덮어쓰는 값이라도, 밖에서 훨씬 빠르게 계속 눌러 쓰면 대부분의
// 프레임을 우리가 이긴다. 화면이 움직이면 그 주소가 답이다.
//
// 이것이 없으면 후보 하나를 시험할 때마다 모드를 고쳐 빌드하고 게임을
// 재시작해야 한다. 사람의 시간을 그렇게 쓰는 것은 옳지 않다.
void cmd_hold(const cdtb::probe::Remote& r, std::uintptr_t addr, float x,
              float y, float z, unsigned ms);

// double 로 저장된 좌표를 찾는다.
//
// 거대 오픈월드는 카메라의 월드 원점을 배정밀도로 들고 있는 경우가
// 많다. float 로만 훑으면 파생 사본만 보이고 원본을 놓친다.
void cmd_findvec3d(const cdtb::probe::Remote& r, double x, double y, double z,
                   double eps, std::size_t max_hits);

// 좌표가 일치하는 자리들을 한꺼번에 눌러 쓴다.
//
// 어느 자리가 렌더를 구동하는지 이분법으로 좁히기 위한 도구다.
// start/count 로 후보의 일부만 골라 쓰면, 화면이 움직이는지 여부만으로
// 범위를 절반씩 줄일 수 있다. 후보가 1000개라도 열 번이면 끝난다.
//
// 하나씩 시험하면 사람이 그만큼 화면을 지켜봐야 한다. 그 시간을
// 줄이는 것이 이 도구의 목적이다.
void cmd_holdmany(const cdtb::probe::Remote& r, float x, float y, float z,
                  float eps, float dy, unsigned ms, std::size_t start,
                  std::size_t count);
