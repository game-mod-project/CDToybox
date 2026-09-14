#include "render/log_filter.h"

#include <algorithm>
#include <cctype>

namespace cdtb::render {
namespace {

// 할당 없이 대소문자 무시 부분 일치. 예전 방식대로 양쪽을 소문자 문자열로 복사하면
// 줄마다 두 번 할당하는데, 이 판정은 거르개가 바뀔 때 수천 줄에 한꺼번에 돈다.
bool contains_ci(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > hay.size()) return false;
    const auto eq = [](char a, char b) {
        // unsigned char 로 넘긴다 - char 가 음수면 std::tolower 는 정의되지 않는다.
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                       eq) != hay.end();
}

}  // namespace

bool log_line_matches(const LogFilter& f, log::Level level,
                      std::string_view text) {
    switch (level) {
        case log::Level::Warn:
            if (!f.warn) return false;
            break;
        case log::Level::Error:
            if (!f.error) return false;
            break;
        default:
            if (!f.info) return false;
            break;
    }
    return contains_ci(text, f.query);
}

}  // namespace cdtb::render
