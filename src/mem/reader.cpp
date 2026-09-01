#include "mem/reader.h"

#include <windows.h>

#include "mem/module.h"
#include "mem/safe_read.h"

namespace cdtb::mem {

bool LocalReader::read(std::uintptr_t addr, void* out, std::size_t n) const {
    return safe_read_bytes(addr, out, n);
}

std::uintptr_t LocalReader::module_base() const {
    const auto m = find_module(nullptr);
    return m.has_value() ? reinterpret_cast<std::uintptr_t>(m->base) : 0;
}

std::size_t LocalReader::module_size() const {
    const auto m = find_module(nullptr);
    return m.has_value() ? m->size : 0;
}

std::vector<Range> LocalReader::heap_regions() const {
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
        const DWORD w = PAGE_READWRITE | PAGE_WRITECOPY |
                        PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        const bool usable = mbi.State == MEM_COMMIT &&
                            (mbi.Protect & PAGE_GUARD) == 0 &&
                            (mbi.Protect & PAGE_NOACCESS) == 0 &&
                            (mbi.Protect & w) != 0 && mbi.Type != MEM_IMAGE;
        if (usable) {
            out.push_back(
                Range{static_cast<const std::uint8_t*>(mbi.BaseAddress),
                      mbi.RegionSize});
        }
        const auto next =
            reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) break;
        addr = next;
    }
    return out;
}

}  // namespace cdtb::mem
