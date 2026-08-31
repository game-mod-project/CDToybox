#pragma once

#include <cstdint>

#include "game/camera.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 카메라를 찾은 뒤 자동으로 도는 분석 절차.
//
// 순서가 곧 설계다. 읽기만 하는 덤프를 먼저 하고, 하드웨어
// 브레이크포인트 감시를 하고, 프로세스를 죽일 수 있는 활성화 실험을
// 맨 뒤에 둔다. 로그는 줄마다 flush 하므로 마지막 단계에서 죽어도
// 앞 단계의 결과는 전부 남는다.
void run_analysis(const mem::Rtti& rtti, const CameraSet& set);

// 개별 단계. 테스트에서 따로 부를 수 있도록 노출한다.
void dump_qwords(const char* what, std::uintptr_t base, int bytes,
                 const CameraSet& set);
void dump_camera_diff(std::uintptr_t a, std::uintptr_t b, int bytes);

// 객체의 vtable 함수 주소를 나열한다. 활성화 메서드를 오프라인에서
// 정적 분석하려면 함수 주소 목록이 있어야 한다.
void dump_vtable(const mem::Rtti& rtti, const char* what, std::uintptr_t object,
                 int entries);
void log_protection(const char* what, std::uintptr_t addr);

}  // namespace cdtb::game
