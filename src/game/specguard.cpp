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
// div 지점이 여러 곳이라, 지점마다 "분모 0이면 나눗셈을 건너뛰고 몫=0"
// 가드를 인라인 훅으로 건다. 정상 아이템은 분모≠0이라 원래대로 나눈다.
//
// rtti(이미지 스캔)에 의존하지 않는다 - 367MB 이미지 로드가 늦은 세션엔
// rtti.loaded()==false 라 설치가 조용히 건너뛰어졌다(실측 2026-09-07). 모듈
// 베이스 + 이 빌드의 확정 RVA 로 바로 가서 예상 opcode 를 확인한 뒤 패치한다.
//
// 빌드 2.00.01(2658) 실측:
//  - 계열 A: `div qword ptr [mem] ; lea ecx,[rax+1]` (디스크 전체 3곳).
//  - 계열 B: `div r8 ; cmp eax,[rdi+4]` (1곳).
// 게임 패치로 코드가 옮겨가면 바이트 불일치로 안전하게 스킵된다.
constexpr std::uint64_t kFamA[] = {0xEB1BF4, 0xEA874F, 0x21DA368};
constexpr std::uint64_t kFamB = 0xF064E5B;

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

// `48 F7 /6 [mem]` (div qword ptr [mem]) 의 바이트 길이. mem 이 아니거나
// div 가 아니면 0.
int div_mem_len(const std::uint8_t* b) {
    if (b[0] != 0x48 || b[1] != 0xF7) return 0;
    const std::uint8_t modrm = b[2];
    const int mod = modrm >> 6, rm = modrm & 7, reg = (modrm >> 3) & 7;
    if (reg != 6 || mod == 3) return 0;   // /6=div, mod==3 은 레지스터
    int len = 3;
    const bool sib = (rm == 4);
    if (sib) ++len;
    if (mod == 0) {
        if (rm == 5) len += 4;
        else if (sib && (b[3] & 7) == 5) len += 4;
    } else if (mod == 1) {
        len += 1;
    } else if (mod == 2) {
        len += 4;
    }
    return len;
}

// 계열 A: div qword ptr [mem] ; lea ecx,[rax+1]. div 의 mem 피연산자를 그대로
// 써서 cmp [mem],0 가드를 만든다. div 가 5바이트 미만이면 뒤의 lea 까지 옮긴다.
bool install_famA(const mem::Reader& reader, std::uintptr_t site) {
    std::uint8_t o[16]{};
    if (!reader.read(site, o, sizeof(o))) return false;
    const int dl = div_mem_len(o);
    if (dl == 0) {
        log::warnf("특수아이템 가드[A]: div 아님 @0x{:X} (0x{:02X}{:02X}{:02X})",
                   site, o[0], o[1], o[2]);
        return false;
    }
    // patch 길이는 최소 5(E9 rel32). div 가 짧으면 뒤 lea(8D 48 01)까지 옮긴다.
    int patch_len, tail_len;
    if (dl >= 5) {
        patch_len = dl;
        tail_len = 0;
    } else {
        if (!(o[dl] == 0x8D && o[dl + 1] == 0x48 && o[dl + 2] == 0x01)) {
            log::warnf("특수아이템 가드[A]: 짧은 div 뒤 lea 불일치 @0x{:X}", site);
            return false;
        }
        patch_len = dl + 3;
        tail_len = 3;
    }
    void* cave = alloc_near(site, 96);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + patch_len;

    std::vector<std::uint8_t> b;
    // cmp qword ptr [mem], 0  =  48 83 (modrm|0x08) [sib/disp...] 00
    b.push_back(0x48);
    b.push_back(0x83);
    b.push_back(static_cast<std::uint8_t>(o[2] | 0x08));
    for (int k = 3; k < dl; ++k) b.push_back(o[k]);   // sib/disp 그대로
    b.push_back(0x00);
    const std::size_t je_pos = b.size();
    b.push_back(0x74);
    b.push_back(0x00);   // rel8 나중에
    // div (원본) [+ tail lea]
    for (int k = 0; k < dl + tail_len; ++k) b.push_back(o[k]);
    const std::size_t jmp1 = b.size();
    b.insert(b.end(), {0xE9, 0, 0, 0, 0});
    const std::size_t zero_pos = b.size();
    b.insert(b.end(), {0x33, 0xC0, 0x33, 0xD2});   // xor eax;xor edx
    for (int k = 0; k < tail_len; ++k) b.push_back(o[dl + k]);   // tail lea 재실행
    const std::size_t jmp2 = b.size();
    b.insert(b.end(), {0xE9, 0, 0, 0, 0});

    b[je_pos + 1] = static_cast<std::uint8_t>(zero_pos - (je_pos + 2));
    std::int32_t r1 = static_cast<std::int32_t>(back - (ca + jmp1 + 5));
    std::memcpy(b.data() + jmp1 + 1, &r1, 4);
    std::int32_t r2 = static_cast<std::int32_t>(back - (ca + jmp2 + 5));
    std::memcpy(b.data() + jmp2 + 1, &r2, 4);

    put_cave(cave, b);
    if (!patch_jmp(site, patch_len, ca)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        return false;
    }
    log::infof("특수아이템 가드[A] 설치: site=0x{:X} divlen={} patch={} cave=0x{:X}",
               site, dl, patch_len, ca);
    return true;
}

// 계열 B: div r8 ; cmp eax,[rdi+4].
bool install_famB(const mem::Reader& reader, std::uintptr_t site) {
    std::uint8_t o[6]{};
    if (!reader.read(site, o, 6)) return false;
    if (o[0] != 0x49 || o[1] != 0xF7 || o[2] != 0xF0 || o[3] != 0x3B ||
        o[4] != 0x47 || o[5] != 0x04) {
        log::warnf("특수아이템 가드[B]: opcode 불일치 @0x{:X}", site);
        return false;
    }
    void* cave = alloc_near(site, 64);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + 6;

    std::vector<std::uint8_t> b;
    b.insert(b.end(), {0x4D, 0x85, 0xC0});   // test r8,r8
    b.insert(b.end(), {0x74, 0x05});         // je zero(+0x0A)
    b.insert(b.end(), {0x49, 0xF7, 0xF0});   // div r8
    b.insert(b.end(), {0xEB, 0x04});         // jmp docmp(+0x0E)
    b.insert(b.end(), {0x33, 0xC0, 0x33, 0xD2});   // zero
    b.insert(b.end(), {0x3B, 0x47, 0x04});   // docmp: cmp eax,[rdi+4]
    const std::size_t jmp = b.size();
    b.insert(b.end(), {0xE9, 0, 0, 0, 0});
    std::int32_t r = static_cast<std::int32_t>(back - (ca + jmp + 5));
    std::memcpy(b.data() + jmp + 1, &r, 4);

    put_cave(cave, b);
    if (!patch_jmp(site, 6, ca)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        return false;
    }
    log::infof("특수아이템 가드[B] 설치: site=0x{:X} cave=0x{:X}", site, ca);
    return true;
}

}  // namespace

bool specguard_install(const mem::Rtti& /*rtti*/, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;

    int n = 0;
    for (const auto rva : kFamA) {
        if (install_famA(reader, base + rva)) ++n;
    }
    if (install_famB(reader, base + kFamB)) ++n;
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
