#include "mem/regions.h"

#include <windows.h>

namespace cdtb::mem {
namespace {

bool is_scannable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

}  // namespace

std::vector<Range> writable_regions() {
    std::vector<Range> out;

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    auto addr =
        reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress);
    const auto limit =
        reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < limit) {
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi,
                           sizeof(mbi)) != sizeof(mbi)) {
            break;
        }
        if (is_scannable(mbi)) {
            out.push_back(
                Range{static_cast<const std::uint8_t*>(mbi.BaseAddress),
                      mbi.RegionSize});
        }
        const auto next =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;   // 전진하지 못하면 무한 루프다
        addr = next;
    }
    return out;
}

std::size_t total_bytes(const std::vector<Range>& regions) {
    std::size_t n = 0;
    for (const auto& r : regions) n += r.size;
    return n;
}

}  // namespace cdtb::mem
