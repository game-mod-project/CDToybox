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
    if (p != nullptr) {
        std::uint32_t v = 0;
        if (mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(p), &v,
                                 sizeof(v)) &&
            v != 0) {
            say(v, reinterpret_cast<std::uint64_t>(a3));
        }
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

}  // namespace cdtb::game
