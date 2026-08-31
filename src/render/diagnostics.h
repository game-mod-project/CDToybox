#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::render {

struct ExecRange {
    std::uintptr_t begin = 0;
    std::size_t size = 0;
};

struct Diagnostics {
    // 게임 모듈
    std::uintptr_t game_base = 0;
    std::size_t game_size = 0;
    std::vector<ExecRange> game_exec;

    // 자기 모듈 마커 탐색 (스캐너 정확성)
    bool self_marker_found = false;
    std::uintptr_t self_marker_expected = 0;
    std::uintptr_t self_marker_found_at = 0;

    // 게임 모듈 프롤로그 탐색 (규모·속도)
    std::size_t prologue_hits = 0;
    double prologue_ms = 0.0;

    std::string error;
};

// 최초 호출 시 1회 계산하고 이후 캐시를 반환한다.
const Diagnostics& diagnostics();

}  // namespace cdtb::render
