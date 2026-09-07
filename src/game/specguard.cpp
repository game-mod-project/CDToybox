#include "game/specguard.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 특수기능 아이템을 치트 지급하면 인벤토리 UI 렌더가 특수기능 상태(0)로
// 나눗셈을 하다 정수 0으로 나누기(0xC0000094)로 죽는다. 렌더 경로에 그런
// div 지점이 여러 곳이라, 실측으로 잡힌 지점마다 "분모 0이면 나눗셈을
// 건너뛰고 몫=0" 가드를 인라인 훅으로 건다. 정상 아이템은 분모≠0이라
// 원래대로 나눈다. 각 AOB 는 이미지에서 유일할 때만 패치한다.

std::atomic<bool> g_installed{false};
std::atomic<int> g_count{0};

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

// AOB 를 이미지에서 유일하게 찾아 라이브 사이트 주소를 준다. 실패/비유일이면 0.
std::uintptr_t find_unique(const mem::Rtti& rtti, const mem::Reader& reader,
                           const char* aob, const char* tag) {
    const auto parsed = mem::parse_pattern(aob);
    if (!parsed) return 0;
    const auto& img = rtti.image();
    const auto hits = mem::find_all(mem::Range{img.data(), img.size()}, *parsed, 2);
    if (hits.size() != 1) {
        log::warnf("특수아이템 가드[{}]: 시그니처가 유일하지 않다({}) - 건너뜀",
                   tag, hits.size());
        return 0;
    }
    const std::uint64_t rva =
        static_cast<std::uint64_t>(hits[0] - img.data());
    return reader.module_base() + rva;
}

// site 에 케이브로 가는 E9 rel32 를 쓰고 나머지는 NOP 로 채운다.
bool patch_jmp(std::uintptr_t site, std::size_t len, std::uintptr_t cave) {
    const std::int32_t rel = static_cast<std::int32_t>(cave - (site + 5));
    std::vector<std::uint8_t> patch(len, 0x90);
    patch[0] = 0xE9;
    std::memcpy(patch.data() + 1, &rel, 4);
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), len,
                        PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(site), patch.data(), len);
    VirtualProtect(reinterpret_cast<void*>(site), len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site),
                          len);
    return true;
}

void put_cave(void* cave, const std::vector<std::uint8_t>& b) {
    std::memcpy(cave, b.data(), b.size());
    FlushInstructionCache(GetCurrentProcess(), cave, b.size());
}

// ---- 사이트 1: div qword ptr [rbp+disp32] (RVA 0xEB1BF4) ----
// 분모가 스택 지역([rbp+disp]) 인 형태. 뒤에 lea ecx,[rax+1]; add ecx,edi.
bool install_site1(const mem::Rtti& rtti, const mem::Reader& reader) {
    const std::uintptr_t site =
        find_unique(rtti, reader, "48 F7 B5 ?? ?? ?? ?? 8D 48 01 03 CF", "1");
    if (site == 0) return false;
    std::uint8_t o[7]{};
    if (!reader.read(site, o, 7) || o[0] != 0x48 || o[1] != 0xF7 ||
        o[2] != 0xB5) {
        return false;
    }
    const std::uint8_t d0 = o[3], d1 = o[4], d2 = o[5], d3 = o[6];
    void* cave = alloc_near(site, 64);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + 7;

    std::vector<std::uint8_t> b;
    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    add({0x48, 0x83, 0xBD, d0, d1, d2, d3, 0x00});   // +0x00 cmp [rbp+disp],0
    add({0x74, 0x0C});                                // +0x08 je zero(+0x16)
    add({0x48, 0xF7, 0xB5, d0, d1, d2, d3});          // +0x0A div [rbp+disp]
    add({0xE9, 0, 0, 0, 0});                          // +0x11 jmp back
    add({0x33, 0xC0, 0x33, 0xD2});                    // +0x16 zero: xor eax;xor edx
    add({0xE9, 0, 0, 0, 0});                          // +0x1A jmp back
    std::int32_t r1 = static_cast<std::int32_t>(back - (ca + 0x11 + 5));
    std::memcpy(b.data() + 0x12, &r1, 4);
    std::int32_t r2 = static_cast<std::int32_t>(back - (ca + 0x1A + 5));
    std::memcpy(b.data() + 0x1B, &r2, 4);
    put_cave(cave, b);
    if (!patch_jmp(site, 7, ca)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        return false;
    }
    log::infof("특수아이템 가드[1] 설치: site=0x{:X} cave=0x{:X}", site, ca);
    return true;
}

// ---- 사이트 2: div r8 (RVA 0xF064E5B) ----
// 분모가 R8 레지스터. 뒤에 cmp eax,[rdi+4] 가 붙어 있어 함께 옮긴다.
bool install_site2(const mem::Rtti& rtti, const mem::Reader& reader) {
    const std::uintptr_t site =
        find_unique(rtti, reader, "49 F7 F0 3B 47 04", "2");
    if (site == 0) return false;
    std::uint8_t o[6]{};
    if (!reader.read(site, o, 6) || o[0] != 0x49 || o[1] != 0xF7 ||
        o[2] != 0xF0) {
        return false;
    }
    void* cave = alloc_near(site, 64);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + 6;   // cmp 까지 옮기므로 그 다음(jae)로

    std::vector<std::uint8_t> b;
    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    add({0x4D, 0x85, 0xC0});          // +0x00 test r8,r8
    add({0x74, 0x05});                // +0x03 je zero(+0x0A)
    add({0x49, 0xF7, 0xF0});          // +0x05 div r8
    add({0xEB, 0x04});                // +0x08 jmp docmp(+0x0E)
    add({0x33, 0xC0, 0x33, 0xD2});    // +0x0A zero: xor eax,eax;xor edx,edx
    add({0x3B, 0x47, 0x04});          // +0x0E docmp: cmp eax,[rdi+4]
    add({0xE9, 0, 0, 0, 0});          // +0x11 jmp back
    std::int32_t r1 = static_cast<std::int32_t>(back - (ca + 0x11 + 5));
    std::memcpy(b.data() + 0x12, &r1, 4);
    put_cave(cave, b);
    if (!patch_jmp(site, 6, ca)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        return false;
    }
    log::infof("특수아이템 가드[2] 설치: site=0x{:X} cave=0x{:X}", site, ca);
    return true;
}

}  // namespace

bool specguard_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (!rtti.loaded()) return false;
    int n = 0;
    if (install_site1(rtti, reader)) ++n;
    if (install_site2(rtti, reader)) ++n;
    // 사이트 하나라도 걸면 설치 완료로 본다(다음 주기에 재시도 안 함).
    // AOB 유일성 검증을 통과한 사이트만 패치되므로 안전하다.
    g_count.store(n, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    log::infof("특수아이템 가드: {}개 사이트 설치", n);
    return true;
}

bool specguard_installed() { return g_installed.load(std::memory_order_acquire); }
bool specguard_unsupported() {
    return g_installed.load(std::memory_order_acquire) &&
           g_count.load(std::memory_order_acquire) == 0;
}

}  // namespace cdtb::game
