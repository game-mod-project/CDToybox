#include "mem/scanner.h"

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

const std::uint8_t* find_first(Range range, const PatternBytes& pattern) {
    if (range.begin == nullptr || pattern.empty()) return nullptr;
    if (range.size < pattern.size()) return nullptr;

    const std::size_t last = range.size - pattern.size();
    for (std::size_t off = 0; off <= last; ++off) {
        const std::uint8_t* at = range.begin + off;
        bool matched = true;
        for (std::size_t k = 0; k < pattern.size(); ++k) {
            const auto& want = pattern[k];
            if (want.has_value() && at[k] != want.value()) {
                matched = false;
                break;
            }
        }
        if (matched) return at;
    }
    return nullptr;
}

std::vector<const std::uint8_t*> find_all(Range range,
                                          const PatternBytes& pattern,
                                          std::size_t max) {
    std::vector<const std::uint8_t*> hits;
    if (range.begin == nullptr || pattern.empty() || max == 0) return hits;
    if (range.size < pattern.size()) return hits;

    Range cursor = range;
    while (hits.size() < max) {
        const std::uint8_t* hit = find_first(cursor, pattern);
        if (hit == nullptr) break;
        hits.push_back(hit);

        const std::size_t consumed =
            static_cast<std::size_t>(hit - cursor.begin) + 1;
        if (consumed >= cursor.size) break;
        cursor.begin += consumed;
        cursor.size -= consumed;
    }
    return hits;
}

}  // namespace cdtb::mem
