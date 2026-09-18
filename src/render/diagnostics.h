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

    std::string error;
};

// 최초 호출 시 1회 계산하고 이후 캐시를 반환한다. **값싼 것만 잰다** - 모듈
// 정보(섹션 헤더)와 우리 DLL(5MB) 안의 마커 찾기다.
const Diagnostics& diagnostics();

// --- 게임 프롤로그 세기 (실행 섹션 383MB 전수)
//
// **여기가 오버레이 첫 오픈을 3.4초 멈추던 자리다.** 실측 2026-09-18:
//
//     21:11:20.742  오버레이 표시
//           +3.50s  진단: 실행 섹션 2개, 프롤로그 8635회, 3376.6ms, 마커 발견
//
// `show_diagnostics` 가 기본 켜짐이라 창을 처음 그릴 때 무조건 돌았다. 접어 둔
// 헤더 안의 숫자인데도 헤더보다 먼저 계산했기 때문이다. 스캐너가 규모에서도
// 도는지 보는 **자체 시험**일 뿐이라, 이제 눌러야 돈다.
struct PrologueProbe {
    bool done = false;
    std::size_t hits = 0;
    double ms = 0.0;
};
PrologueProbe prologue_probe();

// 지금 잰다. **부르는 스레드를 3초쯤 잡는다** - 화면에서 누를 때만 부를 것.
void prologue_probe_run();

}  // namespace cdtb::render
