#include "mem/watchpoint.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "core/log.h"

namespace cdtb::mem {
namespace {

// 예외 핸들러가 프로세스 전역이라 어쩔 수 없이 전역이어야 하는
// 부분만 여기 둔다. 감시 대상 목록 자체는 인스턴스가 들고 있다.
std::mutex g_mutex;

std::uintptr_t g_addr[WriteWatch::kMaxSlots]{};
int g_size[WriteWatch::kMaxSlots]{};
std::unordered_map<std::uintptr_t, std::size_t> g_hits[WriteWatch::kMaxSlots];

// 우리가 한 번이라도 건 주소 전부. 절대 비우지 않는다.
//
// 해제 뒤에 도착하는 예외가 우리 것인지 게임 것인지 가르는 단서다.
// 슬롯 배열이 아니라 집합이어야 한다 - 낙오 스레드는 여러 창 전의
// 주소를 들고 있을 수 있는데, 슬롯 배열은 마지막 창 것만 남는다.
std::unordered_set<std::uintptr_t> g_ever;

PVOID g_handler = nullptr;   // 한 번 걸면 shutdown() 까지 유지한다
bool g_watching = false;
std::unordered_set<DWORD> g_programmed;

int g_enumerated = 0;
int g_open_failed = 0;
int g_set_failed = 0;
int g_self_healed = 0;
int g_rearmed = 0;    // 보호 코드가 지운 것을 다시 건 횟수

// DR7 의 LEN 필드 인코딩. 크기 순서가 아니라는 점을 조심할 것.
DWORD64 len_bits(int size) {
    switch (size) {
        case 1:  return 0;
        case 2:  return 1;
        case 8:  return 2;
        default: return 3;   // 4바이트
    }
}

// 설치된 슬롯 전부를 담은 DR7.
//   bit 2n         Ln    슬롯 n 로컬 활성
//   bit 16+4n..    RWn   01 = 쓰기
//   bit 18+4n..    LENn
DWORD64 make_dr7() {
    DWORD64 dr7 = 0;
    for (int i = 0; i < WriteWatch::kMaxSlots; ++i) {
        if (g_addr[i] == 0) continue;
        dr7 |= (1ull << (i * 2));
        dr7 |= (1ull << (16 + i * 4));
        dr7 |= (len_bits(g_size[i]) << (18 + i * 4));
    }
    return dr7;
}

void apply_slots(CONTEXT* c) {
    c->Dr0 = g_addr[0];
    c->Dr1 = g_addr[1];
    c->Dr2 = g_addr[2];
    c->Dr3 = g_addr[3];
    c->Dr7 = make_dr7();
}

void clear_slots(CONTEXT* c) {
    c->Dr0 = 0;
    c->Dr1 = 0;
    c->Dr2 = 0;
    c->Dr3 = 0;
    c->Dr7 = 0;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* c = ep->ContextRecord;
    const DWORD64 fired = c->Dr6 & 0xF;
    if (fired == 0) return EXCEPTION_CONTINUE_SEARCH;

    const DWORD64 dr[WriteWatch::kMaxSlots] = {c->Dr0, c->Dr1, c->Dr2, c->Dr3};

    std::lock_guard lock(g_mutex);

    // 우리가 아무것도 건 적이 없으면 우리 것일 수 없다.
    if (g_ever.empty()) return EXCEPTION_CONTINUE_SEARCH;

    bool mine = false;
    bool stale = false;
    for (int i = 0; i < WriteWatch::kMaxSlots; ++i) {
        if ((fired & (1ull << i)) == 0) continue;

        if (g_watching && g_addr[i] != 0 &&
            dr[i] == static_cast<DWORD64>(g_addr[i])) {
            ++g_hits[i][static_cast<std::uintptr_t>(c->Rip)];
            mine = true;
            continue;
        }

        // 여기부터는 낙오분이다. 두 경우가 있다.
        //
        // 1. 레지스터가 이미 0 - 히트가 나서 예외가 전달되는 도중에
        //    우리가 그 스레드의 디버그 레지스터를 지운 것이다.
        //    Dr6 의 비트만 남고 Dr0~3 은 비어 있다. 이 경우를 남의
        //    것으로 넘기면 처리되지 않은 단일 스텝 예외가 되어
        //    프로세스가 죽는다. 실측에서 게임도 테스트도 이렇게 죽었다.
        // 2. 우리가 예전에 건 주소를 아직 들고 있다 - SetThreadContext
        //    로 닿지 못한 스레드다.
        if (dr[i] == 0 ||
            g_ever.count(static_cast<std::uintptr_t>(dr[i])) != 0) {
            mine = true;
            stale = true;
        }
    }

    // 우리 것이 아니면 손대지 않는다. 게임(보호 코드)도 디버그
    // 레지스터를 쓸 수 있고, 그 예외를 삼키면 게임이 깨진다.
    if (!mine) return EXCEPTION_CONTINUE_SEARCH;

    if (stale || !g_watching) {
        // 현장에서 고친다. SetThreadContext 로 닿지 못한 스레드를
        // 되찾는 유일한 경로다. 감시 중이면 현재 설정으로 맞추고,
        // 아니면 완전히 무장 해제한다.
        if (g_watching) {
            apply_slots(c);
        } else {
            clear_slots(c);
        }
        ++g_self_healed;
    }

    c->Dr6 = 0;   // 지우지 않으면 다음 감지가 흐려진다
    return EXCEPTION_CONTINUE_EXECUTION;
}

// 스레드의 디버그 레지스터가 우리 설정과 같은지.
bool matches(const CONTEXT& c, bool enable) {
    const DWORD64 want7 = enable ? make_dr7() : 0;
    if (c.Dr7 != want7) return false;
    if (!enable) return true;
    return c.Dr0 == g_addr[0] && c.Dr1 == g_addr[1] && c.Dr2 == g_addr[2] &&
           c.Dr3 == g_addr[3];
}

enum class Programmed { kAlreadyOk, kChanged, kFailed };

// 스레드 하나에 현재 슬롯 구성을 쓰고, 되읽어 확인한다.
// 이미 맞게 걸려 있으면 건드리지 않는다.
// g_mutex 를 쥔 채 부른다.
Programmed program_thread(DWORD tid, bool enable) {
    const HANDLE th = ::OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
        FALSE, tid);
    if (th == nullptr) {
        ++g_open_failed;
        return Programmed::kFailed;
    }

    auto result = Programmed::kFailed;
    ::SuspendThread(th);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (::GetThreadContext(th, &c)) {
        if (matches(c, enable)) {
            result = Programmed::kAlreadyOk;
        } else {
            if (enable) {
                apply_slots(&c);
            } else {
                clear_slots(&c);
            }
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (::SetThreadContext(th, &c)) {
                // 되읽어 확인한다. 성공을 보고하고도 값이 안 들어가는
                // 경우가 있다. 확인하지 않으면 그 스레드를 걸었다고
                // 착각하고, 히트 0 을 "안 쓴다"로 잘못 읽게 된다.
                CONTEXT v{};
                v.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (::GetThreadContext(th, &v) && matches(v, enable)) {
                    result = Programmed::kChanged;
                }
            }
        }
    }
    ::ResumeThread(th);
    ::CloseHandle(th);
    if (result == Programmed::kFailed) ++g_set_failed;
    return result;
}

// 프로세스의 모든 스레드를 훑어 설정을 맞춘다.
//
// 예전에는 "아직 안 건 스레드만" 다시 걸었다. 그것은 사각지대를
// 만든다 - 보호 코드가 이미 건 스레드의 디버그 레지스터를 지우면,
// 그 스레드는 히트가 나지 않고(레지스터가 비었으니), 히트가 없으니
// 핸들러의 현장 복구도 돌지 않고, 새 스레드가 아니니 여기서도
// 건너뛴다. 영원히 눈이 먼다.
//
// 그래서 매번 전부 확인한다. 이미 맞게 걸린 스레드는 건드리지 않으므로
// 비용은 GetThreadContext 한 번뿐이다.
//
// g_mutex 를 쥔 채 부른다.
struct WalkResult {
    int fresh = 0;      // 처음 건 스레드
    int rearmed = 0;    // 설정이 지워져 있어 다시 건 스레드
};

WalkResult walk_threads(bool enable) {
    const DWORD self_pid = ::GetCurrentProcessId();
    const DWORD self_tid = ::GetCurrentThreadId();

    WalkResult r;
    g_enumerated = 0;   // 한 바퀴 기준으로 센다
    const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return r;

    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (::Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != self_pid) continue;
            if (te.th32ThreadID == self_tid) continue;
            ++g_enumerated;

            const bool known = g_programmed.count(te.th32ThreadID) != 0;
            const auto p = program_thread(te.th32ThreadID, enable);
            if (p == Programmed::kFailed) continue;

            if (enable) {
                g_programmed.insert(te.th32ThreadID);
                if (p == Programmed::kChanged) {
                    if (known) {
                        ++r.rearmed;
                        ++g_rearmed;
                    } else {
                        ++r.fresh;
                    }
                }
            }
        } while (::Thread32Next(snap, &te));
    }
    ::CloseHandle(snap);
    return r;
}

void ensure_handler() {
    if (g_handler != nullptr) return;
    // 한 번 걸면 shutdown() 까지 유지한다. 디버그 레지스터가 남은
    // 스레드가 하나라도 있는 채로 핸들러를 내리면, 그 스레드가
    // 처리되지 않은 단일 스텝 예외로 프로세스를 죽인다.
    g_handler = ::AddVectoredExceptionHandler(1, on_exception);
    if (g_handler == nullptr) log::errorf("AddVectoredExceptionHandler 실패");
}

}  // namespace

WriteWatch::~WriteWatch() { remove(); }

bool WriteWatch::add(std::uintptr_t addr, int size, std::string label) {
    if (active_) return false;
    if (addr == 0) return false;
    if (size != 1 && size != 2 && size != 4 && size != 8) return false;
    if (addr % static_cast<std::uintptr_t>(size) != 0) {
        log::errorf("감시 주소가 {}바이트 정렬이 아니다: 0x{:X}", size, addr);
        return false;
    }
    if (static_cast<int>(cfg_.size()) >= kMaxSlots) {
        log::errorf("감시 슬롯이 꽉 찼다(최대 {}개)", kMaxSlots);
        return false;
    }
    cfg_.push_back({addr, size, std::move(label)});
    return true;
}

bool WriteWatch::install() {
    if (active_) return false;
    if (cfg_.empty()) return false;

    {
        std::lock_guard lock(g_mutex);
        if (g_watching) {
            log::errorf("이미 다른 감시가 설치돼 있다");
            return false;
        }
        for (int i = 0; i < kMaxSlots; ++i) {
            g_addr[i] = 0;
            g_size[i] = 0;
            g_hits[i].clear();
        }
        for (std::size_t i = 0; i < cfg_.size(); ++i) {
            g_addr[i] = cfg_[i].addr;
            g_size[i] = cfg_[i].size;
            g_ever.insert(cfg_[i].addr);   // 낙오 판별용. 절대 비우지 않는다
        }
        g_programmed.clear();
        g_enumerated = 0;
        g_rearmed = 0;
        g_open_failed = 0;
        g_set_failed = 0;
        g_watching = true;

        ensure_handler();
        if (g_handler == nullptr) {
            g_watching = false;
            return false;
        }
    }

    WalkResult w;
    {
        std::lock_guard lock(g_mutex);
        w = walk_threads(true);
    }
    const int n = w.fresh + w.rearmed;
    if (n == 0) {
        std::lock_guard lock(g_mutex);
        g_watching = false;
        log::errorf("어떤 스레드에도 걸지 못했다");
        return false;
    }

    for (std::size_t i = 0; i < cfg_.size(); ++i) {
        log::infof("  DR{} 0x{:X} ({}바이트)  {}", i, cfg_[i].addr, cfg_[i].size,
                   cfg_[i].label);
    }
    const auto s = stats();
    log::infof("쓰기 감시 설치: 슬롯 {}개 | 스레드 {}개 중 {}개 성공, "
               "OpenThread 실패 {}, 설정 실패 {}",
               cfg_.size(), s.enumerated, n, s.open_failed, s.set_failed);
    if (s.open_failed + s.set_failed > 0) {
        log::warnf("  못 건 스레드가 있다. 그 스레드의 쓰기는 잡히지 않는다");
    }
    active_ = true;
    return true;
}

int WriteWatch::refresh_threads() {
    if (!active_) return 0;
    std::lock_guard lock(g_mutex);
    const auto w = walk_threads(true);
    if (w.rearmed > 0) {
        log::warnf("  스레드 {}개의 디버그 레지스터가 지워져 있어 다시 걸었다",
                   w.rearmed);
    }
    return w.fresh + w.rearmed;
}

void WriteWatch::remove() {
    if (!active_) return;

    snapshot_ = results();   // 해제 뒤에도 결과를 읽을 수 있게 남긴다

    {
        std::lock_guard lock(g_mutex);
        g_watching = false;   // 먼저 내려야 핸들러가 낙오분을 무장 해제한다

        // 감시 중 새로 생긴 스레드까지 전부 돌아야 한다. 두 바퀴를
        // 도는 것은 첫 바퀴에서 놓친 스레드가 두 번째에 잡히기 때문이다.
        const int before = g_open_failed + g_set_failed;
        walk_threads(false);
        walk_threads(false);
        const int failed = (g_open_failed + g_set_failed) - before;

        g_programmed.clear();
        for (int i = 0; i < kMaxSlots; ++i) {
            g_addr[i] = 0;
            g_size[i] = 0;
        }
        // g_ever 는 남긴다. 낙오 스레드가 예외를 일으키면 핸들러가
        // 그것을 보고 우리 것인 줄 알아채 스스로 무장 해제한다.

        log::infof("쓰기 감시 해제 (지우지 못한 스레드 {}개, 핸들러는 유지)",
                   failed);
    }
    // 예외 핸들러는 떼지 않는다. 남은 스레드가 우리 브레이크포인트를
    // 때렸을 때 받아 줄 곳이 없으면 프로세스가 죽는다. 2026-08-31
    // 실측에서 정확히 그렇게 죽었다.
    active_ = false;
}

void WriteWatch::shutdown() {
    std::lock_guard lock(g_mutex);
    g_watching = false;
    for (int i = 0; i < kMaxSlots; ++i) {
        g_addr[i] = 0;
        g_size[i] = 0;
    }
    walk_threads(false);
    walk_threads(false);
    g_programmed.clear();
    // g_ever 는 비우지 않는다. 프로세스가 살아 있는 동안 낙오
    // 스레드가 나타날 수 있고, 그때 판별할 근거가 사라지면 안 된다.

    if (g_handler != nullptr) {
        ::RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
    log::infof("쓰기 감시 종료: 핸들러 해제, 현장 복구 {}회", g_self_healed);
}

WriteWatch::ThreadStats WriteWatch::stats() const {
    std::lock_guard lock(g_mutex);
    ThreadStats s;
    s.enumerated = g_enumerated;
    s.programmed = static_cast<int>(g_programmed.size());
    s.open_failed = g_open_failed;
    s.set_failed = g_set_failed;
    s.self_healed = g_self_healed;
    s.rearmed = g_rearmed;
    return s;
}

std::vector<WriteWatch::Slot> WriteWatch::results() const {
    if (!active_) return snapshot_;

    std::lock_guard lock(g_mutex);
    std::vector<Slot> out;
    out.reserve(cfg_.size());
    for (std::size_t i = 0; i < cfg_.size(); ++i) {
        Slot slot;
        slot.label = cfg_[i].label;
        slot.addr = cfg_[i].addr;
        slot.size = cfg_[i].size;
        for (const auto& [rip, count] : g_hits[i]) {
            slot.total += count;
            slot.rips.push_back({rip, count});
        }
        std::sort(slot.rips.begin(), slot.rips.end(),
                  [](const Hit& a, const Hit& b) { return a.count > b.count; });
        out.push_back(std::move(slot));
    }
    return out;
}

}  // namespace cdtb::mem
