#include "game/specguard.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// div qword ptr [rbp+disp32] ; lea ecx,[rax+1] ; add ecx,edi
// disp32 는 빌드마다 다를 수 있어 와일드카드. 이 조합은 이미지에서 유일하다.
constexpr const char* kSite = "48 F7 B5 ?? ?? ?? ?? 8D 48 01 03 CF";

std::atomic<bool> g_installed{false};
std::atomic<bool> g_unsupported{false};
std::uintptr_t g_site = 0;
std::uintptr_t g_cave = 0;

// target 에서 ±2GB 안의 실행 가능 메모리를 잡는다(E9 rel32 점프용).
void* alloc_near(std::uintptr_t target, std::size_t size) {
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const std::uintptr_t gran = si.dwAllocationGranularity;
    const std::uintptr_t start = target & ~(gran - 1);
    for (std::uintptr_t off = gran; off < 0x7F000000ULL; off += gran) {
        for (int dir = 0; dir < 2; ++dir) {
            const std::uintptr_t addr = dir == 0 ? start - off : start + off;
            void* p = VirtualAlloc(reinterpret_cast<void*>(addr), size,
                                   MEM_RESERVE | MEM_COMMIT,
                                   PAGE_EXECUTE_READWRITE);
            if (p != nullptr) return p;
        }
    }
    return nullptr;
}

}  // namespace

bool specguard_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (g_unsupported.load(std::memory_order_acquire)) return false;
    if (!rtti.loaded()) return false;

    const auto parsed = mem::parse_pattern(kSite);
    if (!parsed) return false;
    const auto& img = rtti.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) {   // 유일하지 않으면 패치 금지(안전)
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("특수아이템 가드: 사이트 시그니처가 유일하지 않다({}) - 설치 안 함",
                   hits.size());
        return false;
    }
    const std::uint64_t rva = static_cast<std::uint64_t>(hits[0] - img.data());
    const std::uintptr_t site = reader.module_base() + rva;   // div 명령 위치

    // div 는 7바이트: 48 F7 B5 <disp32>. 라이브에서 disp32 를 복사한다.
    std::uint8_t orig[7]{};
    if (!reader.read(site, orig, sizeof(orig))) return false;
    if (orig[0] != 0x48 || orig[1] != 0xF7 || orig[2] != 0xB5) {
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("특수아이템 가드: div 바이트가 예상과 다르다 - 설치 안 함");
        return false;
    }
    const std::uint8_t d0 = orig[3], d1 = orig[4], d2 = orig[5], d3 = orig[6];

    void* cave = alloc_near(site, 64);
    if (cave == nullptr) return false;
    const std::uintptr_t cave_addr = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + 7;   // 0xEB1BFB (lea ecx,[rax+1])

    // 케이브 조립. 진입 시 RDX:RAX=피제수(RDX=0), RBP 유효, 분모=[rbp+disp].
    std::vector<std::uint8_t> b;
    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    // +0x00: cmp qword ptr [rbp+disp], 0   (48 83 BD <disp32> 00) 8바이트
    add({0x48, 0x83, 0xBD, d0, d1, d2, d3, 0x00});
    // +0x08: je zero  (74 0C) - zero 는 cave+0x16
    add({0x74, 0x0C});
    // +0x0A: div qword ptr [rbp+disp]  (48 F7 B5 <disp32>) 7바이트
    add({0x48, 0xF7, 0xB5, d0, d1, d2, d3});
    // +0x11: jmp back  (E9 rel32) -> back
    add({0xE9, 0, 0, 0, 0});
    // +0x16: zero: xor eax,eax ; xor edx,edx
    add({0x33, 0xC0, 0x33, 0xD2});
    // +0x1A: jmp back  (E9 rel32) -> back
    add({0xE9, 0, 0, 0, 0});

    // rel32 채우기
    const std::int32_t rel1 = static_cast<std::int32_t>(
        back - (cave_addr + 0x11 + 5));
    std::memcpy(b.data() + 0x11 + 1, &rel1, 4);
    const std::int32_t rel2 = static_cast<std::int32_t>(
        back - (cave_addr + 0x1A + 5));
    std::memcpy(b.data() + 0x1A + 1, &rel2, 4);

    std::memcpy(cave, b.data(), b.size());
    FlushInstructionCache(GetCurrentProcess(), cave, b.size());

    // 사이트 패치: div(7바이트) -> E9 rel32(케이브로) + 90 90.
    const std::int32_t rel = static_cast<std::int32_t>(cave_addr - (site + 5));
    std::uint8_t patch[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
    std::memcpy(patch + 1, &rel, 4);

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 7,
                        PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(site), patch, 7);
    VirtualProtect(reinterpret_cast<void*>(site), 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 7);

    g_site = site;
    g_cave = cave_addr;
    g_installed.store(true, std::memory_order_release);
    log::infof("특수아이템 가드 설치: site=0x{:X} cave=0x{:X} disp=0x{:02X}{:02X}{:02X}{:02X}",
               site, cave_addr, d3, d2, d1, d0);
    return true;
}

bool specguard_installed() { return g_installed.load(std::memory_order_acquire); }
bool specguard_unsupported() {
    return g_unsupported.load(std::memory_order_acquire);
}

}  // namespace cdtb::game
