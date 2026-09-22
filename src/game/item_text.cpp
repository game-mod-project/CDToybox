#include "game/item_text.h"

namespace cdtb::game {
namespace {

constexpr std::string_view kLineBreak = "<br/>";
constexpr std::string_view kPlaceholder = "{Staticinfo:";

// 줄마다 앞뒤 ASCII 공백을 지운다.
std::string trim_lines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t start = 0;
    for (;;) {
        std::size_t end = s.find('\n', start);
        if (end == std::string::npos) end = s.size();
        std::size_t a = start;
        std::size_t b = end;
        while (a < b && s[a] == ' ') ++a;
        while (b > a && s[b - 1] == ' ') --b;
        out.append(s, a, b - a);
        if (end == s.size()) break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

}  // namespace

std::string clean_item_desc(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw.compare(i, kLineBreak.size(), kLineBreak) == 0) {
            out += '\n';
            i += kLineBreak.size();
            continue;
        }
        if (raw.compare(i, kPlaceholder.size(), kPlaceholder) == 0) {
            const std::size_t close = raw.find('}', i);
            if (close != std::string_view::npos) {
                // 여는 `{` 와 첫 `}` 사이. 전수 꼴(`#` 가 정확히 하나, 안에 `{` 가 또
                // 없음, 표시 문구가 비지 않음)일 때만 푼다. 아니면 한 글자씩 그대로 간다.
                const std::string_view body = raw.substr(i + 1, close - i - 1);
                const std::size_t hash = body.find('#');
                if (hash != std::string_view::npos &&
                    body.find('#', hash + 1) == std::string_view::npos &&
                    body.find('{') == std::string_view::npos &&
                    hash + 1 < body.size()) {
                    out.append(body.substr(hash + 1));
                    i = close + 1;
                    continue;
                }
            }
        }
        out += raw[i];
        ++i;
    }
    return trim_lines(out);
}

std::string_view desc_first_line(std::string_view cleaned) {
    std::size_t start = 0;
    while (start < cleaned.size()) {
        std::size_t end = cleaned.find('\n', start);
        if (end == std::string_view::npos) end = cleaned.size();
        if (end > start) return cleaned.substr(start, end - start);
        start = end + 1;
    }
    return {};
}

}  // namespace cdtb::game
