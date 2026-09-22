// @build 1.0.0.2949  **kCallSiteRva · kLookupRva 를 재도출했다**(2026-09-22) -
//   0x2B8DD46 · 0x2146120. 조회를 부르는 273곳 중 뒤가 `cmp qword [rax+0x28],-1` 인
//   자리는 여전히 한 곳뿐이고, kEmptyRecordRva(0x6CF2F70)는 lea 짝 142곳 만장일치로
//   그대로다. 판정은 2944 때 넓힌 대로 꼬리까지 본다. 자리와 근거는
//   `game/spawnguard_site.h`.

#include "game/guardcave.h"
#include "game/spawnguard.h"
#include "game/spawnguard_site.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <vector>

#include "core/log.h"

namespace cdtb::game {
namespace {

// 자리·조회·빈 레코드의 RVA 와 자리 판정은 `game/spawnguard_site.h` 에 있다
// (시험이 봐야 해서 헤더로 뺐다). 빌드별 이동은 거기 주석에 있다.
constexpr std::uint64_t kCallSiteRva = kSpawnCallSiteRva;
constexpr std::uint64_t kLookupRva = kSpawnLookupRva;
constexpr std::uint64_t kEmptyRecordRva = kSpawnEmptyRecordRva;

// 빈 레코드의 표식. 게임이 "없음"을 이 두 값으로 나타낸다.
constexpr std::size_t kRecNoOff = 0x28;    // u64, 없으면 -1
constexpr std::size_t kRecRowOff = 0x20;   // u16, 없으면 0xFFFF

std::atomic<bool> g_installed{false};
std::atomic<bool> g_unsupported{false};
// 렌더 루프가 매 프레임 부르지만 다른 스레드가 겹쳐 부를 수도 있다 - 한 번에
// 하나만 들어간다(Codex 지적 2026-09-11).
std::atomic<bool> g_busy{false};
struct BusyGuard {
    ~BusyGuard() { g_busy.store(false, std::memory_order_release); }
};
// 빈 레코드 표식을 이만큼(매 프레임 호출, 60fps 에서 10초) 기다려도 안 보이면
// 한 번 값을 남긴다 - 주소가 낡았을 때 유일한 단서다(2차 리뷰 관찰 2026-09-11).
constexpr int kEmptyWaitLogAt = 600;
std::atomic<int> g_empty_waits{0};

// **썽크가 실제로 막은 횟수.** 케이브가 널 갈래에서만 `lock inc` 로 올린다
// (`guardcave.h`). 0 이면 이번 판에 한 번도 안 막았다는 뜻이고, 그것도
// 정보다 - 자리가 낡아 아무것도 안 막고 있어도 크래시 조건이 안 걸린 판에서는
// 똑같이 "정상" 으로 보이기 때문이다(2026-09-20 확인 때 이 구멍이 남았다).
std::atomic<std::uint32_t> g_hits{0};
std::atomic<bool> g_hits_told{false};

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

bool spawnguard_install(const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    if (g_unsupported.load(std::memory_order_acquire)) return false;
    if (g_busy.exchange(true, std::memory_order_acq_rel)) return false;
    BusyGuard busy;

    const std::uintptr_t base = reader.module_base();
    if (base == 0) return false;
    const std::uintptr_t site = base + kCallSiteRva;
    const std::uintptr_t lookup = base + kLookupRva;
    const std::uintptr_t empty = base + kEmptyRecordRva;

    // 1) 그 자리가 정말 "조회를 부르고 **곧장** [rax+0x28] 을 견주는" 그 자리인가.
    //    `E8` + 대상만 보면 게임이 이미 막아 둔 266곳도 전부 통과한다 - 자리가
    //    낡아 그중 하나에 떨어지면 멀쩡한 자리를 건드리게 되므로 꼬리까지 본다.
    std::uint8_t o[10]{};
    if (!reader.read(site, o, sizeof(o))) return false;
    switch (spawnguard_check_site(o, sizeof(o), site, lookup)) {
        case SpawnSite::kOk:
            break;
        case SpawnSite::kShort:
            return false;   // 읽기 실패와 같이 본다 - 다음 프레임에 다시
        case SpawnSite::kNotCall:
            log::warnf("소환 가드: 0x{:X} 가 call 이 아니다 (0x{:02X}) - 설치 안 함",
                       kCallSiteRva, o[0]);
            g_unsupported.store(true, std::memory_order_release);
            return false;
        case SpawnSite::kWrongTarget: {
            std::int32_t rel = 0;
            std::memcpy(&rel, o + 1, 4);
            const std::uintptr_t target =
                site + 5 +
                static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
            log::warnf("소환 가드: call 대상이 0x{:X} 가 아니라 0x{:X} - 설치 안 함",
                       kLookupRva, target - base);
            g_unsupported.store(true, std::memory_order_release);
            return false;
        }
        case SpawnSite::kGuarded:
            log::warnf("소환 가드: 0x{:X} 의 조회 뒤가 `cmp [rax+0x28],-1` 이 아니다"
                       " ({:02X} {:02X} {:02X} {:02X} {:02X}) - 게임이 이미 막은"
                       " 자리일 수 있다, 설치 안 함",
                       kCallSiteRva, o[5], o[6], o[7], o[8], o[9]);
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
    void* cave = alloc_near(site, 96);
    if (cave == nullptr) {
        log::warnf("소환 가드: 케이브 확보 실패");
        g_unsupported.store(true, std::memory_order_release);
        return false;
    }
    // 바이트는 `guardcave.h` 의 순수 함수가 만든다 - rel8 을 손으로 세면
    // 조각 길이가 바뀔 때 조용히 남의 자리로 뛴다. 시험이 그 산술을 못박는다.
    const std::vector<std::uint8_t> b = build_spawn_thunk(
        lookup, empty, reinterpret_cast<std::uint64_t>(&g_hits));
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

std::uint32_t spawnguard_hits() {
    return g_hits.load(std::memory_order_relaxed);
}

void spawnguard_tick_report() {
    const std::uint32_t n = g_hits.load(std::memory_order_relaxed);
    if (n == 0) return;
    // **한 번만.** 되풀이되는 줄은 로그를 묻는다(TROUBLESHOOTING 6.19).
    bool expected = false;
    if (!g_hits_told.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return;
    }
    log::infof("소환 가드가 **실제로 막았다** - 조회가 널을 낸 것을 {}회 빈 "
               "레코드로 대신했다 (이 줄은 한 번만 나온다)", n);
}

}  // namespace cdtb::game
