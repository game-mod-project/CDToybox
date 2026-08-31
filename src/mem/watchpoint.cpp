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

// 설치된 감시의 전역 상태. 예외 핸들러가 프로세스 전역이라 어쩔 수
// 없이 전역이어야 하는 부분만 여기 둔다. 감시 대상 목록 자체는
// WriteWatch 인스턴스가 들고 있다.
std::mutex g_mutex;
std::uintptr_t g_addr[WriteWatch::kMaxSlots]{};
int g_size[WriteWatch::kMaxSlots]{};
std::unordered_map<std::uintptr_t, std::size_t> g_hits[WriteWatch::kMaxSlots];
PVOID g_handler = nullptr;
bool g_busy = false;

// 이미 디버그 레지스터를 건 스레드. 감시 중 새로 생긴 스레드만
// 골라내기 위해 기억한다.
std::unordered_set<DWORD> g_programmed;

// DR7 의 LEN 필드 인코딩. 크기 순서가 아니라는 점을 조심할 것.
DWORD64 len_bits(int size) {
    switch (size) {
        case 1:  return 0;
        case 2:  return 1;
        case 8:  return 2;
        default: return 3;   // 4바이트
    }
}

// 설치된 슬롯 전부를 담은 DR7 을 만든다.
//   bit 2n         Ln    슬롯 n 로컬 활성
//   bit 16+4n..    RWn   01 = 쓰기
//   bit 18+4n..    LENn
DWORD64 make_dr7() {
    DWORD64 dr7 = 0;
    for (int i = 0; i < WriteWatch::kMaxSlots; ++i) {
        if (g_addr[i] == 0) continue;
        dr7 |= (1ull << (i * 2));                           // Ln
        dr7 |= (1ull << (16 + i * 4));                      // RWn = 01 (쓰기)
        dr7 |= (len_bits(g_size[i]) << (18 + i * 4));       // LENn
    }
    return dr7;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* c = ep->ContextRecord;
    const DWORD64 fired = c->Dr6 & 0xF;   // 어느 슬롯이 걸렸는지
    if (fired == 0) return EXCEPTION_CONTINUE_SEARCH;

    {
        std::lock_guard lock(g_mutex);
        for (int i = 0; i < WriteWatch::kMaxSlots; ++i) {
            if ((fired & (1ull << i)) == 0) continue;
            ++g_hits[i][static_cast<std::uintptr_t>(c->Rip)];
        }
    }

    c->Dr6 = 0;   // 지우지 않으면 다음 감지가 흐려진다
    return EXCEPTION_CONTINUE_EXECUTION;
}

// 스레드 하나에 현재 슬롯 구성을 쓴다. g_mutex 를 쥔 채 부른다.
bool program_thread(DWORD tid, bool enable) {
    const HANDLE th = ::OpenThread(
        THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
        FALSE, tid);
    if (th == nullptr) return false;

    bool ok = false;
    ::SuspendThread(th);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (::GetThreadContext(th, &c)) {
        if (enable) {
            c.Dr0 = g_addr[0];
            c.Dr1 = g_addr[1];
            c.Dr2 = g_addr[2];
            c.Dr3 = g_addr[3];
            c.Dr7 = make_dr7();
        } else {
            c.Dr0 = c.Dr1 = c.Dr2 = c.Dr3 = 0;
            c.Dr7 = 0;
        }
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        ok = ::SetThreadContext(th, &c) != FALSE;
    }
    ::ResumeThread(th);
    ::CloseHandle(th);
    return ok;
}

// 프로세스의 스레드를 훑는다. only_new 면 아직 안 건 것만 건다.
// g_mutex 를 쥔 채 부른다.
int walk_threads(bool enable, bool only_new) {
    const DWORD self_pid = ::GetCurrentProcessId();
    const DWORD self_tid = ::GetCurrentThreadId();

    const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    int applied = 0;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (::Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != self_pid) continue;
            if (te.th32ThreadID == self_tid) continue;
            if (only_new && g_programmed.count(te.th32ThreadID) != 0) continue;

            if (!program_thread(te.th32ThreadID, enable)) continue;
            ++applied;
            if (enable) g_programmed.insert(te.th32ThreadID);
        } while (::Thread32Next(snap, &te));
    }
    ::CloseHandle(snap);
    return applied;
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
        if (g_busy) {
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
        }
        g_programmed.clear();
        g_busy = true;
    }

    // 예외 핸들러를 먼저 건다. 순서가 반대면 첫 히트를 놓친다.
    g_handler = ::AddVectoredExceptionHandler(1, on_exception);
    if (g_handler == nullptr) {
        log::errorf("AddVectoredExceptionHandler 실패");
        std::lock_guard lock(g_mutex);
        g_busy = false;
        return false;
    }

    int n = 0;
    {
        std::lock_guard lock(g_mutex);
        n = walk_threads(true, false);
    }
    if (n == 0) {
        ::RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
        std::lock_guard lock(g_mutex);
        g_busy = false;
        return false;
    }

    for (std::size_t i = 0; i < cfg_.size(); ++i) {
        log::infof("  DR{} 0x{:X} ({}바이트)  {}", i, cfg_[i].addr, cfg_[i].size,
                   cfg_[i].label);
    }
    log::infof("쓰기 감시 설치: 슬롯 {}개, 스레드 {}개", cfg_.size(), n);
    active_ = true;
    return true;
}

int WriteWatch::refresh_threads() {
    if (!active_) return 0;
    std::lock_guard lock(g_mutex);
    return walk_threads(true, true);
}

void WriteWatch::remove() {
    if (!active_) return;

    snapshot_ = results();   // 해제 뒤에도 결과를 읽을 수 있게 남긴다

    {
        std::lock_guard lock(g_mutex);
        // 해제는 감시 중 새로 생긴 스레드까지 전부 돌아야 한다.
        walk_threads(false, false);
        g_programmed.clear();
        for (int i = 0; i < kMaxSlots; ++i) {
            g_addr[i] = 0;
            g_size[i] = 0;
        }
        g_busy = false;
    }
    if (g_handler != nullptr) {
        ::RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
    active_ = false;
    log::infof("쓰기 감시 해제");
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
