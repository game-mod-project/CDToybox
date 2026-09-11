#include "game/spawnguard.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace cdtb::game {
namespace {

// 이 빌드의 확정 RVA. 1.0.0.2850(2026-09-11) 갱신에서 다시 뽑았다 - 조회 함수는
// `call X ; lea reg,[빈 레코드]` 짝 37곳으로, 자리는 옛 자리 +0x2040(형제 자리
// 0x2ABD8EB → 0x2ABF92B 도 같은 폭), 빈 레코드는 데이터 +0x4150. 2760 까지는
// 0x2AD51F8 / 0x208F5C0 / 0x6BB3F70 (실측 2026-09-09). specs/2026-09-11-game-update-2850.md.
constexpr std::uint64_t kCallSiteRva = 0x2AD7238;   // call 0x2090A50
constexpr std::uint64_t kLookupRva = 0x2090A50;     // 번호 -> 레코드 조회
constexpr std::uint64_t kEmptyRecordRva = 0x6BB80C0;

// 빈 레코드의 표식. 게임이 "없음"을 이 두 값으로 나타낸다.
constexpr std::size_t kRecNoOff = 0x28;    // u64, 없으면 -1
constexpr std::size_t kRecRowOff = 0x20;   // u16, 없으면 0xFFFF

std::atomic<bool> g_installed{false};
std::atomic<bool> g_unsupported{false};
// 빈 레코드 표식을 이만큼(매 프레임 호출, 60fps 에서 10초) 기다려도 안 보이면
// 한 번 값을 남긴다 - 주소가 낡았을 때 유일한 단서다(2차 리뷰 관찰 2026-09-11).
constexpr int kEmptyWaitLogAt = 600;
std::atomic<int> g_empty_waits{0};

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

void put8(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
}

}  // namespace

bool spawnguard_install(const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (g_unsupported.load(std::memory_order_acquire)) return false;

    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;
    const std::uintptr_t site = base + kCallSiteRva;
    const std::uintptr_t lookup = base + kLookupRva;
    const std::uintptr_t empty = base + kEmptyRecordRva;

    // 1) 그 자리가 정말 우리가 읽은 그 call 인가.
    std::uint8_t o[5]{};
    if (!reader.read(site, o, sizeof(o))) return false;
    if (o[0] != 0xE8) {
        log::warnf("소환 가드: 0x{:X} 가 call 이 아니다 (0x{:02X}) - 설치 안 함",
                   kCallSiteRva, o[0]);
        g_unsupported.store(true, std::memory_order_release);
        return false;
    }
    std::int32_t rel = 0;
    std::memcpy(&rel, o + 1, 4);
    const std::uintptr_t target = site + 5 + static_cast<std::intptr_t>(rel);
    if (target != lookup) {
        log::warnf("소환 가드: call 대상이 0x{:X} 가 아니라 0x{:X} - 설치 안 함",
                   kLookupRva, target - base);
        g_unsupported.store(true, std::memory_order_release);
        return false;
    }

    // 2) 전역 빈 레코드가 초기화됐는가. 아직이면 다음에 다시 본다 -
    //    여기에 엉뚱한 것을 돌려주면 크래시를 막는 대신 만든다.
    std::uint64_t no = 0;
    std::uint16_t row = 0;
    if (!reader.read_value(empty + kRecNoOff, &no)) return false;
    if (!reader.read_value(empty + kRecRowOff, &row)) return false;
    if (no != ~0ULL || row != 0xFFFF) {
        // 아직 초기화 전이면 조용히 재시도한다. 빈 레코드 주소가 낡아도 여기로
        // 오므로 한참 지나면 값을 한 번 남긴다.
        if (g_empty_waits.fetch_add(1, std::memory_order_relaxed) + 1 ==
            kEmptyWaitLogAt) {
            log::warnf("소환 가드: 빈 레코드 RVA 0x{:X} 가 {}프레임째 표식이 아니다 "
                       "(no=0x{:X} row=0x{:X}) - 주소가 낡았을 수 있다, 계속 기다린다",
                       kEmptyRecordRva, kEmptyWaitLogAt, no, row);
        }
        return false;
    }

    // 3) 썽크: 원래 조회를 부르고, 널이면 빈 레코드를 돌려준다.
    void* cave = alloc_near(site, 64);
    if (cave == nullptr) {
        log::warnf("소환 가드: 케이브 확보 실패");
        g_unsupported.store(true, std::memory_order_release);
        return false;
    }
    std::vector<std::uint8_t> b;
    b.insert(b.end(), {0x48, 0x83, 0xEC, 0x28});         // sub rsp,0x28
    b.insert(b.end(), {0x49, 0xBB});                     // mov r11, imm64
    put8(b, lookup);
    b.insert(b.end(), {0x41, 0xFF, 0xD3});               // call r11
    b.insert(b.end(), {0x48, 0x83, 0xC4, 0x28});         // add rsp,0x28
    b.insert(b.end(), {0x48, 0x85, 0xC0});               // test rax,rax
    b.insert(b.end(), {0x75, 0x0A});                     // jne +10 (ret 로)
    b.insert(b.end(), {0x48, 0xB8});                     // mov rax, imm64
    put8(b, empty);
    b.push_back(0xC3);                                   // ret
    std::memcpy(cave, b.data(), b.size());
    FlushInstructionCache(GetCurrentProcess(), cave, b.size());

    // 4) 그 call 하나만 썽크로 돌린다.
    const std::uintptr_t ca = reinterpret_cast<std::uintptr_t>(cave);
    const std::int32_t nrel = static_cast<std::int32_t>(ca - (site + 5));
    std::uint8_t patch[5]{0xE8};
    std::memcpy(patch + 1, &nrel, 4);
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 5, PAGE_EXECUTE_READWRITE,
                        &old)) {
        log::warnf("소환 가드: VirtualProtect 실패");
        g_unsupported.store(true, std::memory_order_release);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(site), patch, 5);
    VirtualProtect(reinterpret_cast<void*>(site), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 5);

    g_installed.store(true, std::memory_order_release);
    log::infof("소환 가드 설치 - 0x{:X} 의 조회가 널이면 빈 레코드로 대신한다"
               " (썽크 0x{:X})",
               kCallSiteRva, ca);
    return true;
}

bool spawnguard_unsupported() {
    return g_unsupported.load(std::memory_order_acquire);
}

}  // namespace cdtb::game
