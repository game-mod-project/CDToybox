#pragma once

#include <string_view>

#include "core/log.h"

namespace cdtb::render {

// 로그 창의 거르개. **ImGui 를 안 쓴다** - 판정을 순수하게 떼어 두어야 시험이
// 덮는다(그리는 코드는 시험이 못 부른다).
struct LogFilter {
    bool info = true;
    bool warn = true;
    bool error = true;
    char query[128] = "";   // 비면 글자 거르기 없음
};

// 이 줄을 보일 것인가. 글자 비교는 ASCII 대소문자를 무시한다 - 한글은 대소문자가
// 없어 바이트 그대로 맞는다(UTF-8 이어도 부분 일치가 성립한다).
bool log_line_matches(const LogFilter& f, log::Level level,
                      std::string_view text);

}  // namespace cdtb::render
