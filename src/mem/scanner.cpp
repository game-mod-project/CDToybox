#include "mem/scanner.h"

#include <cstring>

namespace cdtb::mem {
namespace {

// 16진 문자 하나를 0~15로. 실패 시 -1.
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr std::size_t kNoAnchor = static_cast<std::size_t>(-1);

// 앵커로 쓸 첫 비-와일드카드 인덱스. 전부 와일드카드면 kNoAnchor.
//
// "첫 바이트"가 아니라 "첫 구체 바이트"를 잡는 것이 중요하다.
// "?? ?? 48 8B" 같은 패턴은 실제 시그니처에서 흔하고, 선두가
// 와일드카드면 memchr로 건너뛸 기준이 없어진다.
std::size_t anchor_index(const PatternBytes& p) {
    for (std::size_t i = 0; i < p.size(); ++i) {
        if (p[i].has_value()) return i;
    }
    return kNoAnchor;
}

bool matches_at(const std::uint8_t* at, const PatternBytes& p) {
    for (std::size_t k = 0; k < p.size(); ++k) {
        const auto& want = p[k];
        if (want.has_value() && at[k] != want.value()) return false;
    }
    return true;
}

}  // namespace

std::optional<PatternBytes> parse_pattern(std::string_view text) {
    PatternBytes out;
    std::size_t i = 0;

    while (i < text.size()) {
        if (is_space(text[i])) {
            ++i;
            continue;
        }

        const std::size_t start = i;
        while (i < text.size() && !is_space(text[i])) ++i;
        const std::string_view tok = text.substr(start, i - start);

        if (tok == "?" || tok == "??") {
            out.push_back(std::nullopt);
            continue;
        }
        if (tok.size() != 2) return std::nullopt;

        const int hi = hex_value(tok[0]);
        const int lo = hex_value(tok[1]);
        if (hi < 0 || lo < 0) return std::nullopt;

        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    if (out.empty()) return std::nullopt;
    return out;
}

// memchr로 앵커 바이트가 나오는 위치만 훑고, 후보에서만 전체를 대조한다.
// memchr는 SIMD로 구현되어 있어 바이트 단위 루프보다 훨씬 빠르다.
const std::uint8_t* find_first(Range range, const PatternBytes& pattern) {
    if (range.begin == nullptr || pattern.empty()) return nullptr;
    if (range.size < pattern.size()) return nullptr;

    const std::size_t last = range.size - pattern.size();
    const std::size_t a = anchor_index(pattern);

    // 전부 와일드카드면 어느 오프셋이든 일치한다.
    if (a == kNoAnchor) return range.begin;

    const std::uint8_t anchor = pattern[a].value();

    // 후보 시작 오프셋은 0..last 이고, 그 앵커 바이트는 begin+o+a 에 있다.
    // 따라서 앵커를 찾을 구간은 begin+a 에서 last+1 바이트다.
    const std::uint8_t* search = range.begin + a;
    std::size_t remaining = last + 1;

    while (remaining > 0) {
        const void* found = std::memchr(search, anchor, remaining);
        if (found == nullptr) return nullptr;

        const auto* hit = static_cast<const std::uint8_t*>(found);
        const std::uint8_t* candidate = hit - a;
        if (matches_at(candidate, pattern)) return candidate;

        const std::size_t consumed =
            static_cast<std::size_t>(hit - search) + 1;
        if (consumed >= remaining) break;
        search += consumed;
        remaining -= consumed;
    }
    return nullptr;
}

std::vector<const std::uint8_t*> find_all(Range range,
                                          const PatternBytes& pattern,
                                          std::size_t max) {
    std::vector<const std::uint8_t*> hits;
    if (range.begin == nullptr || pattern.empty() || max == 0) return hits;
    if (range.size < pattern.size()) return hits;

    const std::size_t last = range.size - pattern.size();
    const std::size_t a = anchor_index(pattern);

    if (a == kNoAnchor) {
        for (std::size_t o = 0; o <= last && hits.size() < max; ++o) {
            hits.push_back(range.begin + o);
        }
        return hits;
    }

    const std::uint8_t anchor = pattern[a].value();
    const std::uint8_t* search = range.begin + a;
    std::size_t remaining = last + 1;

    while (hits.size() < max && remaining > 0) {
        const void* found = std::memchr(search, anchor, remaining);
        if (found == nullptr) break;

        const auto* hit = static_cast<const std::uint8_t*>(found);
        const std::uint8_t* candidate = hit - a;
        if (matches_at(candidate, pattern)) hits.push_back(candidate);

        const std::size_t consumed =
            static_cast<std::size_t>(hit - search) + 1;
        if (consumed >= remaining) break;
        search += consumed;
        remaining -= consumed;
    }
    return hits;
}

}  // namespace cdtb::mem
