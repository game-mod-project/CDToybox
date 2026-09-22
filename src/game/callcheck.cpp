// @build 1.0.0.2949  휠 UI 0x10BE8C0(재도출) · 검증기 0x9DD420 · 앞단 0x9E0FC0 ·
//   관리자·관문 함수 셋 · 오류 슬롯 일곱(여기까지 그대로임을 대조, 2026-09-22).
//   근거는 `callcheck.h`.

#include "game/callcheck.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <format>
#include <initializer_list>
#include <string>

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

// 등록된 오류 값을 슬롯에서 읽어 온다(값은 런타임에 정해진다).
void read_reason_values(std::uint32_t (&values)[kCallCheckReasonCount]) {
    for (std::size_t i = 0; i < kCallCheckReasonCount; ++i) {
        std::uint32_t v = 0;
        if (mem::safe_read_bytes(g_base + kCallCheckReasons[i].slot_rva, &v,
                                 sizeof(v))) {
            values[i] = v;
        } else {
            values[i] = 0;
        }
    }
}

// 오류 코드를 사람이 읽을 글로. **이름을 지어내지 않는다** - 모르면 슬롯값을
// 전부 같이 찍어 밖에서 대조한다.
std::string reason_text(std::uint32_t err) {
    std::uint32_t values[kCallCheckReasonCount]{};
    read_reason_values(values);
    const char* name = callcheck_reason_name(err, values, kCallCheckReasonCount);
    if (name != nullptr) {
        return std::string(name) + " (코드 0x" + std::format("{:X}", err) + ")";
    }
    std::string s = std::format("코드 0x{:X} (아는 {}개 중 없음; 슬롯값", err,
                                kCallCheckReasonCount);
    for (std::size_t i = 0; i < kCallCheckReasonCount; ++i) {
        s += std::format(" {:X}", values[i]);
    }
    return s + ")";
}

void say(std::uint32_t err, std::uint64_t merc_no) {
    if (err == 0) return;
    g_last.store(err, std::memory_order_relaxed);
    if (g_said.exchange(err, std::memory_order_relaxed) == err) return;
    if (g_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxLines) return;
    log::infof("탈것 호출 거부: 번호 {} -> {}", merc_no, reason_text(err));
}

// ------------------------------------------------------- 휠 UI · 앞단

// 인자 수는 `callcheck.h` 의 "휠의 앞 두 단" (호출자 쪽에서 셌다, §1.14).
// 휠 UI 는 this 하나지만 네 레지스터를 그대로 넘긴다 - 콜리가 rdx/r8/r9 를
// 읽기 전에 먼저 쓰므로 무엇이 들어 있든 상관없다.
using WheelUiFn = void* (*)(void*, void*, void*, void*);
using WheelPreFn = std::uint32_t* (*)(void*, std::uint32_t*, std::uint64_t,
                                      void*);
WheelUiFn g_orig_ui = nullptr;
WheelPreFn g_orig_pre = nullptr;
std::atomic<bool> g_ui_on{false};
std::atomic<bool> g_pre_on{false};

// 휠 UI 한 번 **안에서** 앞단이 불렸는지를 짝짓는다. 앞단은 펫 UI 도 부르므로
// (0x10A0A49) 전역 하나로는 섞인다 - 같은 스레드의 같은 호출 안인지로 가른다.
thread_local bool t_in_wheel = false;
thread_local bool t_pre_entered = false;
thread_local std::uint32_t t_pre_err = 0;
thread_local std::uint64_t t_pre_merc = 0;

std::atomic<std::uint32_t> g_ui_calls{0};
std::atomic<std::uint32_t> g_verdicts[kWheelVerdictCount]{};
std::atomic<std::uint64_t> g_ui_last_key{~0ull};
std::atomic<int> g_ui_lines{0};
constexpr int kMaxUiLines = 30;
std::atomic<std::uint32_t> g_ui_reported{0};
std::atomic<std::uint64_t> g_ui_reported_at{0};
std::atomic<int> g_ui_report_lines{0};

// 관리자 칸 읽기(`callcheck.h` 의 kFocus*). 설치 때 바이트 대조가 통과해야 켠다.
std::atomic<bool> g_slots_ok{false};

// 객체 포인터와 그 vtable. 모듈 안 vtable 은 RVA 로 적는다 - 밖에서
// `tools/rtti/vtname.py` 로 클래스 이름을 푼다(여기서 RTTI 를 풀지 않는다).
std::string obj_text(std::uintptr_t obj) {
    if (obj == 0) return "0";
    std::uintptr_t vt = 0;
    if (!mem::safe_read_bytes(obj, &vt, sizeof(vt))) {
        return std::format("0x{:X}(못 읽음)", obj);
    }
    if (vt >= g_base && vt - g_base < 0x20000000) {
        return std::format("0x{:X}(vt RVA 0x{:X})", obj, vt - g_base);
    }
    return std::format("0x{:X}(vt 0x{:X} 모듈 밖)", obj, vt);
}

// 관문①·②가 묻는 칸을 **읽기만** 한다. 게임 함수는 부르지 않는다.
std::string focus_slots_text() {
    std::uintptr_t root = 0;
    if (!mem::safe_read_bytes(g_base + kFocusMgrGlobalRva, &root, sizeof(root)) ||
        root == 0) {
        return "관리자 전역이 비었다";
    }
    std::uintptr_t mgr = 0;
    if (!mem::safe_read_bytes(root + kFocusMgrOff, &mgr, sizeof(mgr)) || mgr == 0) {
        return std::format("관리자 [+0x30] 이 비었다 (전역 0x{:X})", root);
    }
    std::uintptr_t main = 0, ctl = 0, focus = 0;
    mem::safe_read_bytes(mgr + kMainPlayerOff, &main, sizeof(main));
    mem::safe_read_bytes(mgr + kFocusCtlOff, &ctl, sizeof(ctl));
    if (ctl != 0) {
        mem::safe_read_bytes(ctl + kFocusActorOff, &focus, sizeof(focus));
    }
    return std::format("관리자 0x{:X} · 주 플레이어[+0x50] {} · 포커스[+0x58] {}"
                       " -> [+0xD8] {}",
                       mgr, obj_text(main), obj_text(ctl), obj_text(focus));
}

void report_wheel(WheelVerdict v, std::int32_t slot, std::uint8_t kind,
                  std::uint32_t err, std::uint64_t merc) {
    // 같은 결말이 이어지면 넘긴다. 결말이 바뀌면(벌판 -> 보스룸) 다시 찍는다.
    const std::uint64_t key =
        (static_cast<std::uint64_t>(static_cast<int>(v)) << 32) | err;
    if (g_ui_last_key.exchange(key, std::memory_order_relaxed) == key) return;
    if (g_ui_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxUiLines) return;

    std::string tail;
    if (v == WheelVerdict::PreRejected) {
        std::uint32_t silent = 0;
        const bool have_silent = mem::safe_read_bytes(
            g_base + kSilentErrorRva, &silent, sizeof(silent));
        tail = " -> " + reason_text(err);
        if (have_silent && silent == err) {
            tail += " [문구 없음 값과 같다 - 화면에 안 뜬다]";
        }
    }
    // 결말마다 관리자 칸을 같이 남긴다 - 벌판 줄과 나란히 놓고 비교하려고.
    if (g_slots_ok.load(std::memory_order_acquire)) {
        tail += " | " + focus_slots_text();
    }
    log::infof("휠 UI: 칸 {} 종류 {} 번호 {} -> {}{}", slot,
               static_cast<unsigned>(kind), merc,
               wheel_verdict_text(v), tail);
}

void* detour_wheel_ui(void* self, void* a2, void* a3, void* a4) {
    const auto obj = reinterpret_cast<std::uintptr_t>(self);
    std::int32_t slot = -1;
    std::uint8_t kind = 0xFF;
    const bool readable =
        mem::safe_read_bytes(obj + kWheelUiSlotOff, &slot, sizeof(slot)) &&
        mem::safe_read_bytes(obj + kWheelUiKindOff, &kind, sizeof(kind));

    // 재진입해도 바깥 호출의 값을 잃지 않게 통째로 보관했다가 되돌린다.
    const bool s_in = t_in_wheel;
    const bool s_entered = t_pre_entered;
    const std::uint32_t s_err = t_pre_err;
    const std::uint64_t s_merc = t_pre_merc;
    t_in_wheel = true;
    t_pre_entered = false;
    t_pre_err = 0;
    t_pre_merc = 0;

    void* r = g_orig_ui(self, a2, a3, a4);

    const bool entered = t_pre_entered;
    const std::uint32_t err = t_pre_err;
    const std::uint64_t merc = t_pre_merc;
    t_in_wheel = s_in;
    t_pre_entered = s_entered;
    t_pre_err = s_err;
    t_pre_merc = s_merc;

    g_ui_calls.fetch_add(1, std::memory_order_relaxed);
    if (!readable) {
        // 칸을 못 읽었으면 결말을 짓지 않는다 - 추측으로 채우지 않는다.
        if (g_ui_lines.fetch_add(1, std::memory_order_relaxed) < kMaxUiLines) {
            log::warnf("휠 UI: 객체 0x{:X} 의 칸/종류를 못 읽었다 - 결말 생략",
                       obj);
        }
        return r;
    }
    const WheelVerdict v = wheel_verdict(slot, kind, entered, err);
    g_verdicts[static_cast<int>(v)].fetch_add(1, std::memory_order_relaxed);
    report_wheel(v, slot, kind, err, merc);
    return r;
}

std::uint32_t* detour_wheel_pre(void* self, std::uint32_t* err,
                                std::uint64_t merc_no, void* a4) {
    std::uint32_t* r = g_orig_pre(self, err, merc_no, a4);
    if (t_in_wheel) {
        std::uint32_t v = 0;
        if (err != nullptr &&
            !mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(err), &v,
                                  sizeof(v))) {
            v = 0;
        }
        t_pre_entered = true;
        t_pre_err = v;
        t_pre_merc = merc_no;
    }
    return r;
}

// `site` 의 명령이 `call rel32` 이고 그 대상이 `want` 인가. 흔한 프롤로그 대신
// **이 함수만의 의미**로 확인한다(§1.5).
bool call_goes_to(const mem::Reader& reader, std::uintptr_t site,
                  std::uintptr_t want, std::uintptr_t* got) {
    std::uint8_t c[5]{};
    if (!reader.read(site, c, sizeof(c)) || c[0] != 0xE8) {
        *got = 0;
        return false;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, c + 1, 4);
    *got = site + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
    return *got == want;
}

void install_wheel_hooks(const mem::Reader& reader, std::uintptr_t base) {
    std::uintptr_t got = 0;
    const std::uintptr_t pre = base + kWheelPreFnRva;
    if (!call_goes_to(reader, pre + kWheelPreValidatorCallOff,
                      base + kCallCheckFnRva, &got)) {
        log::warnf("휠 앞단 진단: 0x{:X}+0x{:X} 가 검증기를 부르지 않는다"
                   " (대상 0x{:X}) - 안 건다",
                   kWheelPreFnRva, kWheelPreValidatorCallOff,
                   got ? got - base : 0);
        return;
    }
    const std::uintptr_t ui = base + kWheelUiFnRva;
    if (!call_goes_to(reader, ui + kWheelUiPreCallOff, pre, &got)) {
        log::warnf("휠 UI 진단: 0x{:X}+0x{:X} 가 앞단을 부르지 않는다"
                   " (대상 0x{:X}) - 안 건다",
                   kWheelUiFnRva, kWheelUiPreCallOff, got ? got - base : 0);
        return;
    }
    // 앞단을 먼저 건다 - 휠 UI 가 먼저 걸리면 짝이 빈 채로 결말을 지을 수 있다.
    if (!mem::hook_install(reinterpret_cast<void*>(pre),
                           reinterpret_cast<void*>(&detour_wheel_pre),
                           reinterpret_cast<void**>(&g_orig_pre))) {
        log::warnf("휠 앞단 진단: 후킹 실패 (RVA 0x{:X})", kWheelPreFnRva);
        return;
    }
    g_pre_on.store(true, std::memory_order_release);
    if (!mem::hook_install(reinterpret_cast<void*>(ui),
                           reinterpret_cast<void*>(&detour_wheel_ui),
                           reinterpret_cast<void**>(&g_orig_ui))) {
        log::warnf("휠 UI 진단: 후킹 실패 (RVA 0x{:X})", kWheelUiFnRva);
        return;
    }
    g_ui_on.store(true, std::memory_order_release);

    // 관리자 칸 오프셋을 **바이트로** 대조한다. 통과해야 결말 줄에 칸을 붙인다.
    auto same = [&](std::uintptr_t at, std::initializer_list<std::uint8_t> want) {
        std::uint8_t got_b[8]{};
        if (want.size() > sizeof(got_b) || !reader.read(at, got_b, want.size())) {
            return false;
        }
        return std::memcmp(got_b, want.begin(), want.size()) == 0;
    };
    const std::uintptr_t q = base + kFocusQueryFnRva;
    std::uint8_t mov[7]{};
    bool ok = reader.read(q + 0x15, mov, sizeof(mov)) && mov[0] == 0x48 &&
              mov[1] == 0x8B && mov[2] == 0x0D;
    if (ok) {
        std::int32_t rel = 0;
        std::memcpy(&rel, mov + 3, 4);
        ok = q + 0x15 + 7 +
                 static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel)) ==
             base + kFocusMgrGlobalRva;
    }
    ok = ok && same(q + 0x1C, {0x48, 0x8B, 0x49, 0x30}) &&
         call_goes_to(reader, q + 0x20, base + kMainPlayerCheckFnRva, &got) &&
         same(base + kMainPlayerCheckFnRva + 0x1A, {0x48, 0x8B, 0x79, 0x50}) &&
         same(base + kFocusActorCheckFnRva + 0x23, {0x48, 0x8B, 0x79, 0x58}) &&
         same(base + kFocusActorCheckFnRva + 0xEF,
              {0x4C, 0x8B, 0xB0, 0xD8, 0x00, 0x00, 0x00});
    g_slots_ok.store(ok, std::memory_order_release);
    if (!ok) {
        log::warnf("휠 UI 진단: 관리자 칸 오프셋이 2944 와 다르다 - 칸 읽기는 끄고"
                   " 결말만 찍는다");
    }
    log::infof("휠 UI 진단 설치 (UI 0x{:X} · 앞단 0x{:X}) - 휠을 누를 때마다 "
               "어느 관문에서 끝났는지 찍는다 (관리자 칸 읽기 {})",
               kWheelUiFnRva, kWheelPreFnRva, ok ? "켬" : "끔");
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
    // 검증기 **위**를 보는 둘. 실패해도 검증기 진단은 그대로 둔다.
    install_wheel_hooks(reader, base);
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

namespace {

// 휠 UI 요약. 수가 바뀔 때만, 1초에 한 번·40줄까지(검증기 요약과 같은 규칙).
void wheel_tick_report() {
    const std::uint32_t n = g_ui_calls.load(std::memory_order_relaxed);
    if (n == 0 || n == g_ui_reported.load(std::memory_order_relaxed)) return;
    const std::uint64_t now = GetTickCount64();
    if (now - g_ui_reported_at.load(std::memory_order_relaxed) < kReportGapMs) {
        return;
    }
    if (g_ui_report_lines.load(std::memory_order_relaxed) >= kMaxReportLines) {
        return;
    }
    g_ui_report_lines.fetch_add(1, std::memory_order_relaxed);
    g_ui_reported.store(n, std::memory_order_relaxed);
    g_ui_reported_at.store(now, std::memory_order_relaxed);
    auto c = [](WheelVerdict v) {
        return g_verdicts[static_cast<int>(v)].load(std::memory_order_relaxed);
    };
    log::infof("휠 UI: 총 {}회 (칸 없음 {} / 탈것 아님 {} / 관문① {} / 앞단 거부 {}"
               " / 앞단 통과 {})",
               n, c(WheelVerdict::NoSlot), c(WheelVerdict::NotVehicle),
               c(WheelVerdict::BlockedAtGate1), c(WheelVerdict::PreRejected),
               c(WheelVerdict::PrePassed));
}

}  // namespace

void callcheck_tick_report() {
    wheel_tick_report();
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
