// @build 1.0.0.2944  검증기 0x9DD420 · 오류 슬롯 다섯. 근거는 `callcheck.h`.

#include "game/callcheck.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "core/log.h"
#include "game/spawnguard_site.h"
#include "mem/hook.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

// 콜리가 `[rbp+0x50]`(스택 첫 인자)까지 읽으므로 **다섯**이다. 넷으로 걸면
// 다섯째가 우리 스택의 쓰레기가 된다(TROUBLESHOOTING 1.14).
using CallCheckFn = std::uint32_t* (*)(void*, std::uint32_t*, void*, void*,
                                       std::uint16_t);

CallCheckFn g_orig = nullptr;
std::atomic<bool> g_on{false};
std::atomic<std::uint32_t> g_last{0};
// 되풀이되는 줄은 로그를 묻는다(TROUBLESHOOTING 6.19). 거부만 찍고, 같은
// 코드가 이어지면 넘긴다. 서로 다른 사유는 몇 번이든 본다.
constexpr int kMaxLines = 24;
std::atomic<int> g_lines{0};
std::atomic<std::uint32_t> g_said{0};
std::uintptr_t g_base = 0;

// **들어온 횟수**(`callcheck.h` 의 (가)/(나) 설명). 거부와 달리 통과도 센다.
std::atomic<std::uint32_t> g_calls{0};
std::atomic<std::uint32_t> g_pass{0};
std::atomic<std::uint32_t> g_reject{0};
std::atomic<bool> g_told_first{false};
std::atomic<std::uint32_t> g_reported{0};
std::atomic<std::uint64_t> g_reported_at{0};
std::atomic<int> g_report_lines{0};
// 요약은 1초에 한 번까지, 그리고 다 합쳐 40줄까지. 검증기가 매 프레임 불리는
// 종류였더라도 로그를 묻지 못하게 한다(§6.19).
constexpr std::uint64_t kReportGapMs = 1000;
constexpr int kMaxReportLines = 40;

void say(std::uint32_t err, std::uint64_t merc_no) {
    if (err == 0) return;
    g_last.store(err, std::memory_order_relaxed);
    if (g_said.exchange(err, std::memory_order_relaxed) == err) return;
    if (g_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxLines) return;

    std::uint32_t values[kCallCheckReasonCount]{};
    for (std::size_t i = 0; i < kCallCheckReasonCount; ++i) {
        std::uint32_t v = 0;
        if (mem::safe_read_bytes(g_base + kCallCheckReasons[i].slot_rva, &v,
                                 sizeof(v))) {
            values[i] = v;
        }
    }
    const char* name = callcheck_reason_name(err, values, kCallCheckReasonCount);
    if (name != nullptr) {
        log::infof("탈것 호출 거부: 번호 {} -> {} (코드 0x{:X})", merc_no, name,
                   err);
    } else {
        // **이름을 지어내지 않는다.** 슬롯 다섯을 같이 찍어 밖에서 대조한다.
        log::infof("탈것 호출 거부: 번호 {} -> 코드 0x{:X} (아는 다섯 중 없음; "
                   "슬롯값 {:X} {:X} {:X} {:X} {:X})",
                   merc_no, err, values[0], values[1], values[2], values[3],
                   values[4]);
    }
}

std::uint32_t* detour(void* a1, std::uint32_t* err, void* a3, void* a4,
                      std::uint16_t a5) {
    std::uint32_t* r = g_orig(a1, err, a3, a4, a5);
    // 호출자 둘은 각각 `*err` 와 `*ret` 을 읽는다 - 둘은 같은 자리다.
    std::uint32_t* p = (err != nullptr) ? err : r;
    std::uint32_t v = 0;
    if (p != nullptr) {
        // 못 읽으면 0 으로 둔다 - **통과로 세지 않으려고** 따로 가른다.
        if (!mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(p), &v,
                                  sizeof(v))) {
            v = 0;
        }
    }
    // **오류값과 무관하게 무조건 센다.** 여기 조건을 달면 "안 거쳤다" 와
    // "거쳤는데 통과했다" 를 다시 못 가른다(`callcheck.h` 의 (가)/(나)).
    g_calls.fetch_add(1, std::memory_order_relaxed);
    if (v == 0) {
        g_pass.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_reject.fetch_add(1, std::memory_order_relaxed);
        say(v, reinterpret_cast<std::uint64_t>(a3));
    }
    return r;
}

}  // namespace

bool callcheck_diag_install(const mem::Reader& reader) {
    if (g_on.load(std::memory_order_acquire)) return true;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;
    const std::uintptr_t fn = base + kCallCheckFnRva;

    // **의미로 확인한다.** 프롤로그는 이 실행 파일에 4,977곳이라 확인이 아니다
    // (§1.5). `+0x5E` 의 call 이 번호->레코드 조회를 향해야 그 함수다.
    std::uint8_t c[5]{};
    if (!reader.read(fn + kCallCheckLookupCallOff, c, sizeof(c))) return false;
    if (c[0] != 0xE8) {
        log::warnf("호출 검증기 진단: 0x{:X}+0x{:X} 가 call 이 아니다 (0x{:02X})"
                   " - 안 건다",
                   kCallCheckFnRva, kCallCheckLookupCallOff, c[0]);
        return false;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, c + 1, 4);
    const std::uintptr_t target =
        fn + kCallCheckLookupCallOff + 5 +
        static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
    if (target != base + kSpawnLookupRva) {
        log::warnf("호출 검증기 진단: call 대상이 조회(0x{:X})가 아니라 0x{:X}"
                   " - 안 건다",
                   kSpawnLookupRva, target - base);
        return false;
    }

    if (!mem::hook_install(reinterpret_cast<void*>(fn),
                           reinterpret_cast<void*>(&detour),
                           reinterpret_cast<void**>(&g_orig))) {
        log::warnf("호출 검증기 진단: 후킹 실패 (RVA 0x{:X})", kCallCheckFnRva);
        return false;
    }
    g_base = base;
    g_on.store(true, std::memory_order_release);
    log::infof("호출 검증기 진단 설치 (RVA 0x{:X}) - 탈것 호출이 거부되면 그 "
               "사유를 찍는다",
               kCallCheckFnRva);
    return true;
}

bool callcheck_diag_installed() {
    return g_on.load(std::memory_order_acquire);
}

std::uint32_t callcheck_last_error() {
    return g_last.load(std::memory_order_relaxed);
}

CallCheckCounts callcheck_counts() {
    CallCheckCounts c{};
    c.calls = g_calls.load(std::memory_order_relaxed);
    c.pass = g_pass.load(std::memory_order_relaxed);
    c.reject = g_reject.load(std::memory_order_relaxed);
    return c;
}

void callcheck_tick_report() {
    const std::uint32_t n = g_calls.load(std::memory_order_relaxed);
    if (n == 0) return;

    // 첫 호출은 **한 번만** 크게 알린다. 이 줄이 있느냐 없느냐가 차단이
    // 검증기 위인지 아래인지를 가른다 - 이번 조사의 갈림길이다.
    bool expected = false;
    if (g_told_first.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        log::infof("호출 검증기가 **실제로 불렸다** (RVA 0x{:X}) - 탈것 호출이 "
                   "여기까지는 온다 (이 줄은 한 번만 나온다)",
                   kCallCheckFnRva);
    }

    // 그 뒤로는 **수가 달라졌을 때만**, 그것도 1초에 한 번까지.
    if (n == g_reported.load(std::memory_order_relaxed)) return;
    const std::uint64_t now = GetTickCount64();
    if (now - g_reported_at.load(std::memory_order_relaxed) < kReportGapMs) {
        return;
    }
    if (g_report_lines.load(std::memory_order_relaxed) >= kMaxReportLines) {
        return;
    }
    g_report_lines.fetch_add(1, std::memory_order_relaxed);
    g_reported.store(n, std::memory_order_relaxed);
    g_reported_at.store(now, std::memory_order_relaxed);
    log::infof("호출 검증기: 총 {}회 (통과 {} / 거부 {})", n,
               g_pass.load(std::memory_order_relaxed),
               g_reject.load(std::memory_order_relaxed));
}

}  // namespace cdtb::game
