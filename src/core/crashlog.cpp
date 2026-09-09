#include "core/crashlog.h"

#include <windows.h>

#include <cstdint>

namespace cdtb::crashlog {
namespace {

std::uintptr_t g_base = 0;
std::uintptr_t g_end = 0;
wchar_t g_path[MAX_PATH]{};
volatile LONG g_left = 16;   // 로그 폭주 방지
volatile LONG g_busy = 0;

// --- CRT 없는 문자열 조립 -------------------------------------------
// 힙을 쓰지 않는다. 크래시 시점에 힙 락이 잡혀 있을 수 있고, 거기서
// 멈추면 게임이 죽는 대신 굳는다.
struct Buf {
    char b[8192];
    int n = 0;
    void ch(char c) { if (n < static_cast<int>(sizeof(b)) - 1) b[n++] = c; }
    void str(const char* s) { while (*s) ch(*s++); }
    void hex(std::uint64_t v, int digits) {
        static const char* kHex = "0123456789ABCDEF";
        for (int i = digits - 1; i >= 0; --i) ch(kHex[(v >> (i * 4)) & 0xF]);
    }
    void dec(std::uint32_t v) {
        char t[12];
        int k = 0;
        if (v == 0) { ch('0'); return; }
        while (v && k < 12) { t[k++] = static_cast<char>('0' + v % 10); v /= 10; }
        while (k) ch(t[--k]);
    }
    void dec2(std::uint32_t v) { ch(static_cast<char>('0' + (v / 10) % 10));
                                 ch(static_cast<char>('0' + v % 10)); }
};

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
        default:                              return "기타";
    }
}

// 게임이 정상 동작 중에도 던지는 예외(브레이크포인트·C++ 예외
// 0xE06D7363 등)는 무시하고, 그 자리에서 죽는 종류만 남긴다.
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
            return true;
        default:
            return false;
    }
}

// 스택에서 게임 모듈 안을 가리키는 값을 훑는다. 정식 언와인드가
// 아니라 후보 나열이다 - 죽은 자리를 좁히는 데는 이걸로 충분하고,
// .pdata 를 해석하다 또 죽는 것보다 안전하다.
void scan_stack(Buf& out, std::uintptr_t rsp) {
    int found = 0;
    __try {
        for (std::uintptr_t p = rsp; p < rsp + 0x800 && found < 20; p += 8) {
            const std::uintptr_t v = *reinterpret_cast<std::uintptr_t*>(p);
            if (!in_module(v)) continue;
            out.str("  복귀 후보 모듈+0x");
            out.hex(v - g_base, 8);
            out.str("\r\n");
            ++found;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out.str("  (스택 읽기 실패)\r\n");
    }
    if (found == 0) out.str("  (모듈 안 복귀 주소 없음)\r\n");
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

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord || !info->ContextRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (!fatal(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (::InterlockedDecrement(&g_left) < 0) return EXCEPTION_CONTINUE_SEARCH;
    // 같은 스레드가 우리 안에서 또 죽는 것을 막는다.
    if (::InterlockedExchange(&g_busy, 1) != 0) return EXCEPTION_CONTINUE_SEARCH;

    static Buf buf;   // 스택을 아끼려 정적. g_busy 로 직렬화된다.
    buf.n = 0;

    SYSTEMTIME t{};
    ::GetLocalTime(&t);
    buf.str("\r\n===== 예외 ");
    buf.dec2(t.wHour); buf.ch(':'); buf.dec2(t.wMinute); buf.ch(':');
    buf.dec2(t.wSecond);
    buf.str(" 스레드 ");
    buf.dec(::GetCurrentThreadId());
    buf.str(" =====\r\n");

    const CONTEXT* c = info->ContextRecord;
    const std::uintptr_t rip = static_cast<std::uintptr_t>(c->Rip);
    buf.str("코드 0x"); buf.hex(code, 8);
    buf.ch(' '); buf.str(code_name(code)); buf.str("\r\n");
    buf.str("RIP  0x"); buf.hex(rip, 16);
    if (in_module(rip)) { buf.str("  = 모듈+0x"); buf.hex(rip - g_base, 8); }
    else                { buf.str("  = 모듈 밖"); }
    buf.str("\r\n");

    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_IN_PAGE_ERROR) {
        const ULONG_PTR* p = info->ExceptionRecord->ExceptionInformation;
        buf.str(p[0] == 1 ? "쓰기 주소 0x" : "읽기 주소 0x");
        buf.hex(static_cast<std::uint64_t>(p[1]), 16);
        buf.str("\r\n");
    }

    buf.str("RSP 0x"); buf.hex(c->Rsp, 16);
    buf.str(" RCX 0x"); buf.hex(c->Rcx, 16);
    buf.str(" RDX 0x"); buf.hex(c->Rdx, 16);
    buf.str("\r\nR8  0x"); buf.hex(c->R8, 16);
    buf.str(" R9  0x"); buf.hex(c->R9, 16);
    buf.str(" RAX 0x"); buf.hex(c->Rax, 16);
    buf.str("\r\nRBX 0x"); buf.hex(c->Rbx, 16);
    buf.str(" RSI 0x"); buf.hex(c->Rsi, 16);
    buf.str(" RDI 0x"); buf.hex(c->Rdi, 16);
    buf.str("\r\n스택:\r\n");
    scan_stack(buf, static_cast<std::uintptr_t>(c->Rsp));

    emit(buf);
    ::InterlockedExchange(&g_busy, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void install(const wchar_t* crash_path) {
    const HMODULE game = ::GetModuleHandleW(nullptr);
    if (game) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            reinterpret_cast<const std::uint8_t*>(game) + dos->e_lfanew);
        g_base = reinterpret_cast<std::uintptr_t>(game);
        g_end = g_base + nt->OptionalHeader.SizeOfImage;
    }
    int i = 0;
    for (; crash_path[i] && i < MAX_PATH - 1; ++i) g_path[i] = crash_path[i];
    g_path[i] = 0;
    ::AddVectoredExceptionHandler(0, on_exception);
}

}  // namespace cdtb::crashlog
