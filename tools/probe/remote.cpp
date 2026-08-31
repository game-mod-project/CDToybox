#include "remote.h"

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <format>

namespace cdtb::probe {
namespace {

std::string win_error(const char* what) {
    const DWORD e = ::GetLastError();
    char* msg = nullptr;
    ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                         FORMAT_MESSAGE_FROM_SYSTEM |
                         FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, e, 0, reinterpret_cast<char*>(&msg), 0, nullptr);
    std::string text = msg != nullptr ? msg : "(메시지 없음)";
    if (msg != nullptr) ::LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    return std::format("{} 실패 (오류 {}): {}", what, e, text);
}

}  // namespace

Remote::~Remote() {
    if (handle_ != nullptr) ::CloseHandle(handle_);
}

bool Remote::attach(const wchar_t* exe_name) {
    const HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        err_ = win_error("CreateToolhelp32Snapshot");
        return false;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD found = 0;
    if (::Process32FirstW(snap, &pe)) {
        do {
            if (::_wcsicmp(pe.szExeFile, exe_name) == 0) {
                found = pe.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snap, &pe));
    }
    ::CloseHandle(snap);

    if (found == 0) {
        err_ = "프로세스를 찾지 못했습니다. 게임이 실행 중인지 확인하세요.";
        return false;
    }
    pid_ = found;

    handle_ = ::OpenProcess(
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, found);
    if (handle_ == nullptr) {
        err_ = win_error("OpenProcess");
        return false;
    }

    // 주 실행 모듈의 베이스와 크기.
    HMODULE mods[1024];
    DWORD needed = 0;
    if (!::EnumProcessModulesEx(handle_, mods, sizeof(mods), &needed,
                                LIST_MODULES_ALL) ||
        needed < sizeof(HMODULE)) {
        err_ = win_error("EnumProcessModulesEx");
        return false;
    }
    MODULEINFO mi{};
    if (!::GetModuleInformation(handle_, mods[0], &mi, sizeof(mi))) {
        err_ = win_error("GetModuleInformation");
        return false;
    }
    base_ = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
    size_ = mi.SizeOfImage;
    return true;
}

bool Remote::read(std::uintptr_t addr, void* out, std::size_t n) const {
    if (handle_ == nullptr || out == nullptr || n == 0) return false;
    SIZE_T got = 0;
    if (!::ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr), out, n,
                             &got)) {
        return false;
    }
    return got == n;
}

std::vector<Remote::Region> Remote::regions() const {
    std::vector<Region> out;
    if (handle_ == nullptr) return out;

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);
    auto addr =
        reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress);
    const auto limit =
        reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < limit) {
        if (::VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(addr), &mbi,
                             sizeof(mbi)) != sizeof(mbi)) {
            break;
        }
        const bool readable =
            mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_GUARD) == 0 &&
            (mbi.Protect & PAGE_NOACCESS) == 0 && mbi.Protect != 0;
        if (readable) {
            const DWORD w = PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            const DWORD x = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            Region r;
            r.base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
            r.size = mbi.RegionSize;
            r.writable = (mbi.Protect & w) != 0;
            r.executable = (mbi.Protect & x) != 0;
            r.is_image = mbi.Type == MEM_IMAGE;
            out.push_back(r);
        }
        const auto next =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return out;
}

}  // namespace cdtb::probe
