#pragma once

#include <cstdint>
#include <string_view>

#include "core/log.h"

namespace cdtb {

// 게임 메모리를 바꾸는 함수는 예외 없이 이 한 줄을 남긴다. 창이 아니라
// game:: 안에서 불러 명령 파일로 조작해도 남는다. 크래시 뒤 "무엇을 썼는가" 를
// 로그로 재구성하는 유일한 길이다(bin64/CDToybox.crash.txt 와 짝).
// before 를 모르면 "-" 를 넘긴다.
inline void log_write(std::string_view what, std::uintptr_t target,
                      std::string_view before, std::string_view after) {
    log::infof("쓰기 {}: 0x{:X} {} -> {}", what,
               static_cast<unsigned long long>(target), before, after);
}

}  // namespace cdtb
