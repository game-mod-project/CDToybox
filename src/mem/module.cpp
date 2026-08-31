#include "mem/module.h"

#include <windows.h>

namespace cdtb::mem {
namespace {

const IMAGE_NT_HEADERS64* nt_headers(const std::uint8_t* base) {
    if (base == nullptr) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

}  // namespace

std::optional<ModuleInfo> find_module(const wchar_t* name) {
    const HMODULE h = ::GetModuleHandleW(name);
    if (h == nullptr) return std::nullopt;

    const auto* base = reinterpret_cast<const std::uint8_t*>(h);
    const IMAGE_NT_HEADERS64* nt = nt_headers(base);
    if (nt == nullptr) return std::nullopt;

    ModuleInfo info;
    info.base = base;
    info.size = nt->OptionalHeader.SizeOfImage;
    return info;
}

std::vector<Range> executable_ranges(ModuleInfo mod) {
    std::vector<Range> out;

    const IMAGE_NT_HEADERS64* nt = nt_headers(mod.base);
    if (nt == nullptr) return out;

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    const WORD count = nt->FileHeader.NumberOfSections;

    for (WORD i = 0; i < count; ++i) {
        const auto& s = sec[i];
        if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;

        const DWORD size =
            s.Misc.VirtualSize != 0 ? s.Misc.VirtualSize : s.SizeOfRawData;
        if (size == 0) continue;
        if (static_cast<std::size_t>(s.VirtualAddress) + size > mod.size) {
            continue;
        }

        out.push_back(Range{mod.base + s.VirtualAddress, size});
    }
    return out;
}

}  // namespace cdtb::mem
