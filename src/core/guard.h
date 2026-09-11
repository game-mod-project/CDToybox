#pragma once

namespace cdtb::guard {

// 게임 상태를 수정해도 안전한지 판정한다.
//
// 지금은 항상 true 다 - 이 게임은 순수 싱글플레이다(guard.cpp). 관문은 남긴다:
// 멀티플레이가 오면 이 함수 하나로 모든 쓰기를 막는다. 쓰기 버튼은
// render/confirm 이 이 값을 보고 비활성이 되고, 본창은 "잠겨 있습니다" 를 낸다.
bool is_safe_to_modify();

}  // namespace cdtb::guard
