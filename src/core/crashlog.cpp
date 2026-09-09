#include "core/crashlog.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>

namespace cdtb::crashlog {
namespace {

std::uintptr_t g_base = 0;
std::uintptr_t g_end = 0;
wchar_t g_path[MAX_PATH]{};
volatile LONG g_left = 16;   // 예외 로그 폭주 방지
volatile LONG g_busy = 0;

// --- CRT 없는 문자열 조립 -------------------------------------------
struct Buf {
    char b[65536];
    int n = 0;
    void ch(char c) { if (n < static_cast<int>(sizeof(b)) - 1) b[n++] = c; }
    void str(const char* s) { while (*s) ch(*s++); }
    void hex(std::uint64_t v, int digits) {
        static const char* kHex = "0123456789ABCDEF";
        for (int i = digits - 1; i >= 0; --i) ch(kHex[(v >> (i * 4)) & 0xF]);
    }
    void dec(std::uint64_t v) {
        char t[24];
        int k = 0;
        if (v == 0) { ch('0'); return; }
        while (v && k < 24) { t[k++] = static_cast<char>('0' + v % 10); v /= 10; }
        while (k) ch(t[--k]);
    }
    void dec2(std::uint32_t v) { ch(static_cast<char>('0' + (v / 10) % 10));
                                 ch(static_cast<char>('0' + v % 10)); }
    void nl() { ch('\r'); ch('\n'); }
    void stamp() {
        SYSTEMTIME t{};
        ::GetLocalTime(&t);
        dec2(t.wHour); ch(':'); dec2(t.wMinute); ch(':'); dec2(t.wSecond);
    }
};

Buf g_buf;   // 정적. g_busy 로 직렬화한다.

bool in_module(std::uintptr_t p) { return p >= g_base && p < g_end; }

const char* code_name(DWORD c) {
    switch (c) {
        case EXCEPTION_ACCESS_VIOLATION:      return "접근 위반";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "배열 범위 초과";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "정렬 위반";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "잘못된 명령";
        case EXCEPTION_IN_PAGE_ERROR:         return "페이지 오류";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "0 나누기";
        case EXCEPTION_PRIV_INSTRUCTION:      return "특권 명령";
        case EXCEPTION_STACK_OVERFLOW:        return "스택 넘침";
        case 0xE06D7363:                      return "C++ 예외";
        case 0xC0000409:                      return "보안 검사 실패";
        default:                              return "기타";
    }
}

bool fatal(DWORD c) {
    switch (c) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case 0xC0000409:
            return true;
        default:
            return false;
    }
}

// 스택에서 게임 모듈 안을 가리키는 값을 훑는다. 정식 언와인드가
// 아니라 후보 나열이다 - .pdata 를 해석하다 또 죽는 것보다 안전하다.
//
// 읽을 수 있는 범위를 VirtualQuery 로 미리 자른다. 앞서 루프 전체를
// __try 하나로 감쌌더니 한 번 실패하는 순간 루프가 끝나 후보가 하나만
// 남았다(2026-09-09 19:09 기록).
void scan_stack(Buf& out, std::uintptr_t rsp, int limit) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(rsp), &mbi, sizeof(mbi)) == 0 ||
        mbi.State != MEM_COMMIT) {
        out.str("    (스택 영역 없음)");
        out.nl();
        return;
    }
    const std::uintptr_t region_end =
        reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    std::uintptr_t stop = rsp + 0x1000;
    if (stop > region_end) stop = region_end;

    int found = 0;
    for (std::uintptr_t p = rsp; p + 8 <= stop && found < limit; p += 8) {
        std::uintptr_t v = 0;
        __try {
            v = *reinterpret_cast<std::uintptr_t*>(p);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }
        if (!in_module(v)) continue;
        out.str("    복귀 후보 모듈+0x");
        out.hex(v - g_base, 8);
        out.nl();
        ++found;
    }
    if (found == 0) {
        out.str("    (모듈 안 복귀 주소 없음)");
        out.nl();
    }
}

void emit(const Buf& buf) {
    const HANDLE h = ::CreateFileW(g_path, FILE_APPEND_DATA,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(h, buf.b, static_cast<DWORD>(buf.n), &written, nullptr);
    ::FlushFileBuffers(h);
    ::CloseHandle(h);
}

void write_context(Buf& buf, const CONTEXT* c, const EXCEPTION_RECORD* rec) {
    const std::uintptr_t rip = static_cast<std::uintptr_t>(c->Rip);
    if (rec != nullptr) {
        buf.str("코드 0x"); buf.hex(rec->ExceptionCode, 8);
        buf.ch(' '); buf.str(code_name(rec->ExceptionCode)); buf.nl();
    }
    buf.str("  RIP 0x"); buf.hex(rip, 16);
    if (in_module(rip)) { buf.str("  = 모듈+0x"); buf.hex(rip - g_base, 8); }
    else                { buf.str("  = 모듈 밖"); }
    buf.nl();
    if (rec != nullptr && (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                           rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)) {
        const ULONG_PTR* p = rec->ExceptionInformation;
        buf.str(p[0] == 1 ? "  쓰기 주소 0x" : "  읽기 주소 0x");
        buf.hex(static_cast<std::uint64_t>(p[1]), 16);
        buf.nl();
    }
    buf.str("  RSP 0x"); buf.hex(c->Rsp, 16);
    buf.str(" RCX 0x"); buf.hex(c->Rcx, 16);
    buf.str(" RDX 0x"); buf.hex(c->Rdx, 16);
    buf.nl();
    buf.str("  RBX 0x"); buf.hex(c->Rbx, 16);
    buf.str(" RSI 0x"); buf.hex(c->Rsi, 16);
    buf.str(" RDI 0x"); buf.hex(c->Rdi, 16);
    buf.nl();
    scan_stack(buf, static_cast<std::uintptr_t>(c->Rsp), 20);
}

void report(const char* why, EXCEPTION_POINTERS* info) {
    if (::InterlockedExchange(&g_busy, 1) != 0) return;
    g_buf.n = 0;
    g_buf.nl();
    g_buf.str("===== "); g_buf.str(why); g_buf.ch(' '); g_buf.stamp();
    g_buf.str(" 스레드 "); g_buf.dec(::GetCurrentThreadId());
    g_buf.str(" ====="); g_buf.nl();
    write_context(g_buf, info->ContextRecord, info->ExceptionRecord);
    emit(g_buf);
    ::InterlockedExchange(&g_busy, 0);
}

LONG CALLBACK on_vectored(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr ||
        info->ContextRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (!fatal(info->ExceptionRecord->ExceptionCode))
        return EXCEPTION_CONTINUE_SEARCH;
    // 게임 모듈 밖에서 난 것은 버린다. 우리 스캐너가 매핑 안 된
    // 페이지를 훑을 때 나는 양성 예외가 전부 그쪽이었다.
    if (!in_module(static_cast<std::uintptr_t>(info->ContextRecord->Rip)))
        return EXCEPTION_CONTINUE_SEARCH;
    if (::InterlockedDecrement(&g_left) < 0) return EXCEPTION_CONTINUE_SEARCH;
    report("예외", info);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI on_unhandled(EXCEPTION_POINTERS* info) {
    if (info != nullptr && info->ExceptionRecord != nullptr &&
        info->ContextRecord != nullptr) {
        report("처리되지 않은 예외", info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// --- 감시(멈춤 잡기) -------------------------------------------------

volatile LONG g_watch_on = 0;
volatile LONG g_watch_dumped = 0;
volatile LONG64 g_watch_at = 0;
unsigned long long g_watch_id = 0;
char g_watch_what[16]{};
constexpr LONG64 kWatchLimitMs = 12000;

// 모든 스레드를 세워 문맥을 뜨고, **되살린 뒤에** 파일을 쓴다.
// 세워 둔 채로 CreateFileW 를 부르면 이번엔 우리가 로더 락에서 멈춘다.
void dump_all_threads() {
    if (::InterlockedExchange(&g_busy, 1) != 0) return;
    const DWORD pid = ::GetCurrentProcessId();
    const DWORD self = ::GetCurrentThreadId();
    g_buf.n = 0;
    g_buf.nl();
    g_buf.str("===== 멈춤 감지 "); g_buf.stamp(); g_buf.str(" =====");
    g_buf.nl();
    g_buf.str("돌아오지 않는 호출: "); g_buf.str(g_watch_what);
    g_buf.str(" 번호 "); g_buf.dec(g_watch_id);
    g_buf.str(" (");
    g_buf.dec(::GetTickCount64() - static_cast<ULONGLONG>(g_watch_at));
    g_buf.str("ms 경과)");
    g_buf.nl();

    const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        int shown = 0;
        if (::Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;
                if (te.th32ThreadID == self) continue;
                if (shown >= 48) break;
                const HANDLE th = ::OpenThread(
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                        THREAD_QUERY_INFORMATION,
                    FALSE, te.th32ThreadID);
                if (th == nullptr) continue;
                if (::SuspendThread(th) != static_cast<DWORD>(-1)) {
                    alignas(16) CONTEXT ctx{};
                    ctx.ContextFlags = CONTEXT_FULL;
                    if (::GetThreadContext(th, &ctx)) {
                        g_buf.nl();
                        g_buf.str("  [스레드 ");
                        g_buf.dec(te.th32ThreadID);
                        g_buf.ch(']');
                        g_buf.nl();
                        write_context(g_buf, &ctx, nullptr);
                        ++shown;
                    }
                    ::ResumeThread(th);
                }
                ::CloseHandle(th);
            } while (::Thread32Next(snap, &te));
        }
        ::CloseHandle(snap);
    }
    emit(g_buf);
    ::InterlockedExchange(&g_busy, 0);
}

DWORD WINAPI watchdog(LPVOID) {
    for (;;) {
        ::Sleep(1000);
        if (::InterlockedCompareExchange(&g_watch_on, 1, 1) != 1) continue;
        if (::InterlockedCompareExchange(&g_watch_dumped, 1, 1) == 1) continue;
        const LONG64 began = g_watch_at;
        if (began == 0) continue;
        if (static_cast<LONG64>(::GetTickCount64()) - began < kWatchLimitMs)
            continue;
        ::InterlockedExchange(&g_watch_dumped, 1);
        dump_all_threads();
    }
}

}  // namespace

void watch_begin(const char* what, unsigned long long id) {
    if (::InterlockedCompareExchange(&g_watch_on, 1, 0) != 0) return;
    int i = 0;
    for (; what[i] != 0 && i < 15; ++i) g_watch_what[i] = what[i];
    g_watch_what[i] = 0;
    g_watch_id = id;
    g_watch_at = static_cast<LONG64>(::GetTickCount64());
}

void watch_end() {
    g_watch_at = 0;
    ::InterlockedExchange(&g_watch_dumped, 0);
    ::InterlockedExchange(&g_watch_on, 0);
}

void install(const wchar_t* crash_path) {
    const HMODULE game = ::GetModuleHandleW(nullptr);
    if (game != nullptr) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const std::uint8_t*>(game) + dos->e_lfanew);
        g_base = reinterpret_cast<std::uintptr_t>(game);
        g_end = g_base + nt->OptionalHeader.SizeOfImage;
    }
    int i = 0;
    for (; crash_path[i] != 0 && i < MAX_PATH - 1; ++i) g_path[i] = crash_path[i];
    g_path[i] = 0;
    ::AddVectoredExceptionHandler(0, on_vectored);
    ::SetUnhandledExceptionFilter(on_unhandled);
    const HANDLE t = ::CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr);
    if (t != nullptr) ::CloseHandle(t);
}

}  // namespace cdtb::crashlog
