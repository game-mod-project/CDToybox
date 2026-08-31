#pragma once

#include "mem/scanner.h"

namespace cdtb::mem {

// VirtualQuery로 커밋되고 읽기·쓰기가 가능한 영역을 모은다.
// PAGE_GUARD / PAGE_NOACCESS / 실행 전용 페이지는 제외한다.
//
// 모듈 이미지의 데이터 섹션은 제외하지 않는다. 카메라 구조체를
// 가리키는 전역 포인터는 실행 파일의 .data 계열에 있을 가능성이
// 높고, 그것을 빼면 정작 필요한 것을 놓친다.
std::vector<Range> writable_regions();

// 열거된 영역들의 총 바이트. 진행률 표시에 쓴다.
std::size_t total_bytes(const std::vector<Range>& regions);

}  // namespace cdtb::mem
