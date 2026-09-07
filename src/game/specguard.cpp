#include "game/specguard.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace cdtb::game {
namespace {

// 특수기능 아이템을 치트 지급하면 인벤토리 UI 렌더가 특수기능 상태(0)로
// 나눗셈을 하다 정수 0으로 나누기(0xC0000094)로 죽는다. 렌더 경로에 그런
// div 지점이 여러 곳이라, 실측으로 잡힌 지점마다 "분모 0이면 나눗셈을
// 건너뛰고 몫=0" 가드를 인라인 훅으로 건다. 정상 아이템은 분모≠0이라
// 원래대로 나눈다.
//
// **rtti(이미지 스캔)에 의존하지 않는다.** 예전엔 AOB 를 rtti.image() 에서
// 찾았는데, 367MB 이미지 로드가 늦은 세션에선 rtti.loaded()==false 라 설치가
// 조용히 건너뛰어졌다(실측 2026-09-07: nofall·specguard 로그 0, site1 크래시
// 재발). 그래서 모듈 베이스 + 이 빌드의 확정 RVA 로 바로 가서, 예상 opcode
// 바이트를 확인한 뒤에만 패치한다(다른 빌드면 바이트 불일치로 안전하게 스킵).
//
// 빌드 2.00.01(2658) 실측 RVA. 게임 패치로 코드가 옮겨가면 바이트가 달라
// 자동으로 스킵된다(오작동 대신 무효).
constexpr std::uint64_t kSite1Rva = 0xEB1BF4;   // div qword ptr [rbp+disp32]
constexpr std::uint64_t kSite2Rva = 0xF064E5B;  // div r8 ; cmp eax,[rdi+4]

std::atomic<bool> g_installed{false};
std::atomic<int> g_count{0};

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

std::uint8_t rd8(const mem::Reader& r, std::uintptr_t a) {
    std::uint8_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

// ---- 사이트 1: div qword ptr [rbp+disp32] ----
bool install_site1(const mem::Reader& reader, std::uintptr_t site) {
    std::uint8_t o[7]{};
    if (!reader.read(site, o, 7)) return false;
    if (o[0] != 0x48 || o[1] != 0xF7 || o[2] != 0xB5) {
        log::warnf("특수아이템 가드[1]: opcode 불일치(0x{:02X}{:02X}{:02X}) - 스킵",
                   o[0], o[1], o[2]);
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
    add({0x33, 0xC0, 0x33, 0xD2});                    // +0x16 zero
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

// ---- 사이트 2: div r8 ; cmp eax,[rdi+4] ----
bool install_site2(const mem::Reader& reader, std::uintptr_t site) {
    std::uint8_t o[6]{};
    if (!reader.read(site, o, 6)) return false;
    if (o[0] != 0x49 || o[1] != 0xF7 || o[2] != 0xF0 || o[3] != 0x3B ||
        o[4] != 0x47 || o[5] != 0x04) {
        log::warnf("특수아이템 가드[2]: opcode 불일치 - 스킵");
        return false;
    }
    void* cave = alloc_near(site, 64);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + 6;

    std::vector<std::uint8_t> b;
    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    add({0x4D, 0x85, 0xC0});          // +0x00 test r8,r8
    add({0x74, 0x05});                // +0x03 je zero(+0x0A)
    add({0x49, 0xF7, 0xF0});          // +0x05 div r8
    add({0xEB, 0x04});                // +0x08 jmp docmp(+0x0E)
    add({0x33, 0xC0, 0x33, 0xD2});    // +0x0A zero: xor eax;xor edx
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

bool specguard_install(const mem::Rtti& /*rtti*/, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;   // 아직 모듈 베이스 모름 - 다음 주기에

    int n = 0;
    if (install_site1(reader, base + kSite1Rva)) ++n;
    if (install_site2(reader, base + kSite2Rva)) ++n;
    g_count.store(n, std::memory_order_release);
    g_installed.store(true, std::memory_order_release);
    log::infof("특수아이템 가드: {}개 사이트 설치 (base=0x{:X})", n, base);
    return true;
}

bool specguard_installed() { return g_installed.load(std::memory_order_acquire); }
bool specguard_unsupported() {
    return g_installed.load(std::memory_order_acquire) &&
           g_count.load(std::memory_order_acquire) == 0;
}

}  // namespace cdtb::game
