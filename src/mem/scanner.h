#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cdtb::mem {

// nullopt 원소는 와일드카드(??)를 뜻한다.
using PatternBytes = std::vector<std::optional<std::uint8_t>>;

struct Range {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
};

// "48 8B 05 ?? ?? ?? ??" 형식을 파싱한다.
// 공백 구분, 대소문자 무관, 와일드카드는 ? 또는 ??.
// 토큰이 하나도 없거나 잘못된 토큰이 있으면 nullopt.
std::optional<PatternBytes> parse_pattern(std::string_view text);

// 첫 일치 위치. 없으면 nullptr.
const std::uint8_t* find_first(Range range, const PatternBytes& pattern);

// 최대 max개의 일치 위치를 앞에서부터 모은다.
std::vector<const std::uint8_t*> find_all(Range range,
                                          const PatternBytes& pattern,
                                          std::size_t max);

}  // namespace cdtb::mem
