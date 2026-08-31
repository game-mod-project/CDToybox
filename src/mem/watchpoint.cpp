#include "mem/watchpoint.h"

#include <windows.h>
#include <tlhelp32.h>

#include <mutex>
#include <unordered_map>

#include "core/log.h"

namespace cdtb::mem {
namespace {

std::mutex g_mutex;
std::unordered_map<std::uintptr_t, std::size_t> g_hits;   // rip -> 횟수
PVOID g_handler = nullptr;
std::uintptr_t g_watched = 0;

// DR7 구성.
//   bit0     L0   DR0 로컬 활성
//   bit16-17 RW0  01 = 쓰기, 11 = 읽기/쓰기
//   bit18-19 LEN0 00=1, 01=2, 11=4, 10=8 바이트
DWORD64 make_dr7(int size) {
    DWORD64 len = 0;
    switch (size) {
        case 1: len = 0; break;
        case 2: len = 1; break;
        case 8: len = 2; break;
        default: len = 3; break;   // 4바이트
    }
    return (1ull << 0) | (1ull << 16) | (len << 18);
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    CONTEXT* c = ep->ContextRecord;
    // DR6 의 하위 4비트가 어느 브레이크포인트인지 알려준다.
    if ((c->Dr6 & 0xF) == 0) return EXCEPTION_CONTINUE_SEARCH;

    {
        std::lock_guard lock(g_mutex);
        ++g_hits[static_cast<std::uintptr_t>(c->Rip)];
    }

    c->Dr6 = 0;   // 지우지 않으면 다음 감지가 흐려진다
    return EXCEPTION_CONTINUE_EXECUTION;
}

// 현재 프로세스의 다른 스레드 전부에 디버그 레지스터를 설정한다.
int apply_to_threads(std::uintptr_t addr, int size, bool enable) {
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

            const HANDLE th = ::OpenThread(
                THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                FALSE, te.th32ThreadID);
            if (th == nullptr) continue;

            ::SuspendThread(th);
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (::GetThreadContext(th, &c)) {
                if (enable) {
                    c.Dr0 = addr;
                    c.Dr7 = make_dr7(size);
                } else {
                    c.Dr0 = 0;
                    c.Dr7 = 0;
                }
                c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                if (::SetThreadContext(th, &c)) ++applied;
            }
            ::ResumeThread(th);
            ::CloseHandle(th);
        } while (::Thread32Next(snap, &te));
    }
    ::CloseHandle(snap);
    return applied;
}

}  // namespace

WriteWatch::~WriteWatch() { remove(); }

bool WriteWatch::install(std::uintptr_t addr, int size) {
    if (active_) remove();
    if (addr == 0) return false;
    if (size != 1 && size != 2 && size != 4 && size != 8) return false;
    if (addr % static_cast<std::uintptr_t>(size) != 0) {
        log::errorf("감시 주소가 {}바이트 정렬이 아니다: 0x{:X}", size, addr);
        return false;
    }

    {
        std::lock_guard lock(g_mutex);
        g_hits.clear();
        g_watched = addr;
    }

    // 예외 핸들러를 먼저 건다. 순서가 반대면 첫 히트를 놓친다.
    g_handler = ::AddVectoredExceptionHandler(1, on_exception);
    if (g_handler == nullptr) {
        log::errorf("AddVectoredExceptionHandler 실패");
        return false;
    }

    const int n = apply_to_threads(addr, size, true);
    log::infof("쓰기 감시 설치: 0x{:X} ({}바이트), 스레드 {}개", addr, size, n);

    if (n == 0) {
        ::RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
        return false;
    }
    active_ = true;
    return true;
}

void WriteWatch::remove() {
    if (!active_) return;
    apply_to_threads(0, 4, false);
    if (g_handler != nullptr) {
        ::RemoveVectoredExceptionHandler(g_handler);
        g_handler = nullptr;
    }
    active_ = false;
    log::infof("쓰기 감시 해제");
}

std::vector<std::uintptr_t> WriteWatch::hits() const {
    std::lock_guard lock(g_mutex);
    std::vector<std::uintptr_t> out;
    out.reserve(g_hits.size());
    for (const auto& [rip, count] : g_hits) out.push_back(rip);
    return out;
}

std::size_t WriteWatch::hit_count() const {
    std::lock_guard lock(g_mutex);
    std::size_t n = 0;
    for (const auto& [rip, count] : g_hits) n += count;
    return n;
}

}  // namespace cdtb::mem
