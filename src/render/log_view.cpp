#include "render/log_view.h"

#include <cstddef>

namespace cdtb::render {

void log_view_rebuild(LogView& v, const LogFilter& f) {
    v.view.clear();
    v.view.reserve(v.lines.size());
    for (std::size_t i = 0; i < v.lines.size(); ++i) {
        if (log_line_matches(f, v.lines[i].level, v.lines[i].text)) {
            v.view.push_back(static_cast<int>(i));
        }
    }
}

void log_view_trim(LogView& v) {
    if (v.lines.size() <= kLogViewMax) return;
    const std::size_t drop = v.lines.size() - kLogViewMax + kLogTrimChunk;
    v.lines.erase(v.lines.begin(),
                  v.lines.begin() + static_cast<std::ptrdiff_t>(drop));
    std::vector<int> kept;
    kept.reserve(v.view.size());
    for (const int i : v.view) {
        if (static_cast<std::size_t>(i) >= drop) {
            kept.push_back(i - static_cast<int>(drop));
        }
    }
    v.view.swap(kept);
}

void log_view_absorb(LogView& v, std::size_t before, std::uint64_t missed,
                     const LogFilter& f) {
    // 창을 열기 전의 줄은 놓친 것이 아니다(위 헤더 주석 참고).
    if (v.primed) v.missed += missed;
    v.primed = true;
    if (v.lines.size() <= before) return;
    v.seen = v.lines.back().seq;
    for (std::size_t i = before; i < v.lines.size(); ++i) {
        if (log_line_matches(f, v.lines[i].level, v.lines[i].text)) {
            v.view.push_back(static_cast<int>(i));
        }
    }
    log_view_trim(v);
}

void log_view_clear(LogView& v) {
    v.lines.clear();
    v.view.clear();
    v.missed = 0;
    // seen 은 그대로 둔다. 되돌리면 고리에 남은 줄이 통째로 다시 들어와 "지우기" 가
    // 아무 일도 안 한 것처럼 보인다.
}

}  // namespace cdtb::render
