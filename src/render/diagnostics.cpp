#include "render/diagnostics.h"

#include <chrono>
#include <mutex>

#include "core/log.h"
#include "mem/module.h"
#include "mem/scanner.h"

namespace cdtb::render {
namespace {

// 스캐너 정확성 검증용 고유 마커. 우연히 일치할 확률이 없도록
// 16바이트로 두고, 최적화로 제거되지 않도록 volatile로 둔다.
volatile const std::uint8_t kMarker[16] = {
    0xC0, 0xDE, 0x70, 0x0B, 0x0C, 0xAF, 0xE1, 0x23,
    0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xED,
};

// @aob any - 진단용 표식. 게임 exe 에 있을 이유가 없다
constexpr const char* kMarkerPattern =
    "C0 DE 70 0B 0C AF E1 23 45 67 89 AB CD EF FE ED";

// x64 MSVC의 흔한 함수 프롤로그.
// @aob any - 흔한 MSVC 프롤로그. 함수 시작을 되짚는 용도라 여러 곳이 정상이다
constexpr const char* kProloguePattern = "48 89 5C 24 08 57 48 83 EC 20";

Diagnostics compute() {
    using namespace cdtb::mem;
    Diagnostics d;

    // --- 자기 모듈에서 마커 찾기: 스캐너가 정확한가
    d.self_marker_expected = reinterpret_cast<std::uintptr_t>(
        const_cast<const std::uint8_t*>(kMarker));

    const auto self = find_module(L"xinput1_4.dll");
    const auto marker_pat = parse_pattern(kMarkerPattern);
    if (!self.has_value() || !marker_pat.has_value()) {
        d.error = "자기 모듈 또는 마커 패턴을 준비하지 못했습니다";
        return d;
    }
    if (const std::uint8_t* hit =
            find_first(Range{self->base, self->size}, *marker_pat)) {
        d.self_marker_found = true;
        d.self_marker_found_at = reinterpret_cast<std::uintptr_t>(hit);
    }

    // --- 게임 모듈 실행 섹션에서 프롤로그 세기: 규모에서도 도는가
    const auto game = find_module(nullptr);
    const auto prologue_pat = parse_pattern(kProloguePattern);
    if (!game.has_value() || !prologue_pat.has_value()) {
        d.error = "게임 모듈 또는 프롤로그 패턴을 준비하지 못했습니다";
        return d;
    }
    d.game_base = reinterpret_cast<std::uintptr_t>(game->base);
    d.game_size = game->size;

    // 섹션 헤더만 읽는다 - 값싸다. **여기서 프롤로그를 세지 않는다**
    // (`diagnostics.h` 의 PrologueProbe 주석).
    for (const auto& r : executable_ranges(*game)) {
        d.game_exec.push_back(
            ExecRange{reinterpret_cast<std::uintptr_t>(r.begin), r.size});
    }

    log::infof("진단: 실행 섹션 {}개, 마커 {}", d.game_exec.size(),
               d.self_marker_found ? "발견" : "미발견");
    return d;
}

std::mutex g_pro_mtx;
PrologueProbe g_pro;

}  // namespace

const Diagnostics& diagnostics() {
    static const Diagnostics d = compute();
    return d;
}

PrologueProbe prologue_probe() {
    std::lock_guard<std::mutex> lk(g_pro_mtx);
    return g_pro;
}

void prologue_probe_run() {
    using namespace cdtb::mem;
    const auto game = find_module(nullptr);
    const auto pat = parse_pattern(kProloguePattern);
    if (!game.has_value() || !pat.has_value()) {
        log::warnf("프롤로그 재기: 게임 모듈 또는 패턴을 준비하지 못했다");
        return;
    }
    PrologueProbe p;
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& r : executable_ranges(*game)) {
        p.hits += find_all(r, *pat, 100000).size();
    }
    const auto t1 = std::chrono::steady_clock::now();
    p.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    p.done = true;
    {
        std::lock_guard<std::mutex> lk(g_pro_mtx);
        g_pro = p;
    }
    log::infof("프롤로그 재기: {}회, {:.1f}ms", p.hits, p.ms);
}

}  // namespace cdtb::render
