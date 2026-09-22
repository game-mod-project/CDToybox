// @build 1.0.0.2949  div 사이트 다섯(kDivMem[3] · kDivReg 넷)을 재도출했다(2026-09-22) -
//   `div + 꼬리` 바이트열이 실행 섹션에서 각각 1곳, 가족 표식(전역 0x06D69458 ·
//   호출 0x01416C10 · gs:[0x58]+0x1EC)도 그대로다.
// @build 1.0.0.2949  **가방 렌더 셋(kDivMem[0..2])은 2944 에 이어 2949 에도 없다** -
//   바이트열 셋을 파일 385MB 전량에서 찾아 각각 0곳임을 다시 확인했다. 표에는 남겨
//   둔다, opcode 검사가 걸러 5/8 설치로 끝나고 경고가 뜬다.
//   (2944 에서 더한 쌍둥이 0xF83F6B4 는 2949 에서 0xF2FD6C3 이다 - 0xF83F65B(→0xF2FD66B)
//   와 같은 함수 안이고, 앞의 것만 걸면 크래시가 뒤로 옮겨갈 뿐이다.)
//   자리 표와 근거는 `game/specguard_sites.h`.

#include "game/guardcave.h"
#include "game/specguard.h"
#include "game/specguard_sites.h"
#include "game/specguard_tail.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
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
// 자리 표는 `game/specguard_sites.h` 에 있다(시험이 봐야 해서 헤더로 뺐다).
// 거기에 각 RVA 의 근거·빌드별 이동·왜 낡은 셋을 안 지우는지가 적혀 있다.

// 0 안 함, 1 설치 중(다른 스레드는 손대지 않는다), 2 끝. 렌더 루프와 분석 루프가
// 동시에 부를 수 있어 CAS 로 한 스레드만 들어간다(Codex 지적 2026-09-11).
std::atomic<int> g_state{0};
std::atomic<int> g_count{0};
// **가드가 실제로 막은 횟수.** 케이브가 0 갈래에서만 `lock inc` 로 올린다
// (`guardcave.h`). 0 이면 이번 판에 한 번도 안 막았다는 뜻이고, 그것도 정보다 -
// 자리가 낡아 아무것도 안 막고 있어도 크래시 조건이 안 걸린 판에서는 똑같이
// "정상" 으로 보이기 때문이다(2026-09-20 게임 확인 때 이 구멍이 남았다).
std::atomic<std::uint32_t> g_hits{0};
std::atomic<bool> g_hits_told{false};
constexpr int kSiteTotal = kSpecguardSiteTotal;

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

// div qword ptr [mem] 가드. div 의 mem 피연산자를 그대로 써서 cmp [mem],0
// 를 만들고, 분모 0이면 나눗셈을 건너뛴다. div 가 5바이트 미만이면 patch_len
// 만큼 뒤 명령(위치 독립)을 함께 케이브로 옮긴다.
bool install_divguard(const mem::Reader& reader, std::uintptr_t site,
                      int patch_len) {
    std::uint8_t o[24]{};
    if (patch_len < 5 || patch_len > 20) return false;
    if (!reader.read(site, o, sizeof(o))) return false;
    const int dl = div_mem_len(o);
    if (dl == 0) {
        log::warnf("특수아이템 가드: div 아님 @0x{:X} (0x{:02X}{:02X}{:02X})",
                   site, o[0], o[1], o[2]);
        return false;
    }
    const int tail_len = patch_len - dl;
    if (tail_len < 0) {
        log::warnf("특수아이템 가드: patch_len<divlen @0x{:X}", site);
        return false;
    }
    // div 자신이 [rip+disp32] 면 케이브로 옮기는 순간 딴 곳을 나눈다.
    if ((o[2] >> 6) == 0 && (o[2] & 7) == 5) {
        log::warnf("특수아이템 가드: div 가 RIP 상대라 옮길 수 없다 @0x{:X} - 건너뜀",
                   site);
        return false;
    }
    if (!specguard_tail_is_safe(o + dl, static_cast<std::size_t>(tail_len))) {
        log::warnf("특수아이템 가드: 꼬리가 위치 독립이 아니다 @0x{:X} ({:02X} {:02X} "
                   "{:02X}) - 건너뜀, patch_len 을 다시 잡을 것",
                   site, o[dl], o[dl + 1], o[dl + 2]);
        return false;
    }
    void* cave = alloc_near(site, 128);
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
    // **막은 갈래에서만** 센다. `xor` **앞**이어야 한다 - 뒤에 두면 게임이
    // 읽는 플래그가 달라진다(`guardcave.h`).
    emit_hit_bump(b, reinterpret_cast<std::uint64_t>(&g_hits));
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
    log::infof("특수아이템 가드[div] 설치: site=0x{:X} divlen={} patch={} cave=0x{:X}",
               site, dl, patch_len, ca);
    return true;
}

// div <reg64> 가드. 분모 레지스터가 0이면 나눗셈을 건너뛴다. div 는 3바이트라
// (48/49) F7 (F0|N) 형태. patch_len 만큼 뒤 위치독립 명령을 함께 옮긴다.
bool install_regdiv(const mem::Reader& reader, std::uintptr_t site,
                    int patch_len) {
    std::uint8_t o[24]{};
    if (patch_len < 5 || patch_len > 20) return false;
    if (!reader.read(site, o, sizeof(o))) return false;
    // (48|49) F7, modrm mod==11 reg==6(/6=div)
    if ((o[0] != 0x48 && o[0] != 0x49) || o[1] != 0xF7 ||
        (o[2] & 0xC0) != 0xC0 || ((o[2] >> 3) & 7) != 6) {
        log::warnf("특수아이템 가드[reg]: div 아님 @0x{:X} (0x{:02X}{:02X}{:02X})",
                   site, o[0], o[1], o[2]);
        return false;
    }
    const int N = o[2] & 7;                       // 레지스터 하위 3비트
    const std::uint8_t rex_test = (o[0] == 0x49) ? 0x4D : 0x48;
    const std::uint8_t modrm_test =
        static_cast<std::uint8_t>(0xC0 | (N << 3) | N);
    const int tail_len = patch_len - 3;
    if (tail_len < 0) return false;
    if (!specguard_tail_is_safe(o + 3, static_cast<std::size_t>(tail_len))) {
        log::warnf("특수아이템 가드[reg]: 꼬리가 위치 독립이 아니다 @0x{:X} ({:02X} {:02X} "
                   "{:02X}) - 건너뜀, patch_len 을 다시 잡을 것",
                   site, o[3], o[4], o[5]);
        return false;
    }

    void* cave = alloc_near(site, 128);
    if (cave == nullptr) return false;
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::uintptr_t back = site + patch_len;

    std::vector<std::uint8_t> b;
    b.insert(b.end(), {rex_test, 0x85, modrm_test});   // test rN,rN
    const std::size_t je_pos = b.size();
    b.insert(b.end(), {0x74, 0x00});                   // je zero (rel 나중에)
    b.insert(b.end(), {o[0], o[1], o[2]});             // div rN
    for (int k = 0; k < tail_len; ++k) b.push_back(o[3 + k]);   // 꼬리
    const std::size_t jmp1 = b.size();
    b.insert(b.end(), {0xE9, 0, 0, 0, 0});
    const std::size_t zero_pos = b.size();
    // **막은 갈래에서만** 센다. `xor` **앞**이어야 한다 - 뒤에 두면 게임이
    // 읽는 플래그가 달라진다(`guardcave.h`).
    emit_hit_bump(b, reinterpret_cast<std::uint64_t>(&g_hits));
    b.insert(b.end(), {0x33, 0xC0, 0x33, 0xD2});       // xor eax;xor edx
    for (int k = 0; k < tail_len; ++k) b.push_back(o[3 + k]);   // 꼬리 재실행
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
    log::infof("특수아이템 가드[reg] 설치: site=0x{:X} N={} patch={} cave=0x{:X}",
               site, N, patch_len, ca);
    return true;
}

}  // namespace

bool specguard_install(const mem::Reader& reader) {
    int expected = 0;
    if (!g_state.compare_exchange_strong(expected, 1,
                                         std::memory_order_acq_rel)) {
        return expected == 2;   // 다른 스레드가 설치 중이거나 이미 끝났다
    }
    const std::uintptr_t base = reader.module_base();
    if (base == 0) {
        g_state.store(0, std::memory_order_release);
        return false;
    }

    int n = 0;
    for (const auto& s : kSpecguardDivMem) {
        if (install_divguard(reader, base + s.rva, s.patch_len)) ++n;
    }
    for (const auto& s : kSpecguardDivReg) {
        if (install_regdiv(reader, base + s.rva, s.patch_len)) ++n;
    }
    g_count.store(n, std::memory_order_release);
    g_state.store(2, std::memory_order_release);
    if (n == kSiteTotal) {
        log::infof("특수아이템 가드: {}개 사이트 설치 (base=0x{:X})", n, base);
    } else {
        log::warnf("특수아이템 가드: {}/{}개만 설치 (base=0x{:X}) - 빠진 자리에서는 "
                   "특수기능 아이템이 원래대로 죽을 수 있다, 위 warn 줄을 볼 것",
                   n, kSiteTotal, base);
    }
    return true;
}

bool specguard_installed() {
    return g_state.load(std::memory_order_acquire) == 2;
}
std::uint32_t specguard_hits() {
    return g_hits.load(std::memory_order_relaxed);
}

void specguard_tick_report() {
    const std::uint32_t n = g_hits.load(std::memory_order_relaxed);
    if (n == 0) return;
    // **한 번만.** 되풀이되는 줄은 로그를 묻는다(TROUBLESHOOTING 6.19).
    bool expected = false;
    if (!g_hits_told.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return;
    }
    log::infof("특수아이템 가드가 **실제로 막았다** - 0 으로 나눌 뻔한 것을 {}회 "
               "건너뛰었다 (이 줄은 한 번만 나온다)", n);
}

bool specguard_unsupported() {
    return g_state.load(std::memory_order_acquire) == 2 &&
           g_count.load(std::memory_order_acquire) < kSiteTotal;
}

}  // namespace cdtb::game
