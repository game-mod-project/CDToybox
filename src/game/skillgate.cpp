#include "game/skillgate.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>

#include "core/log.h"
#include "core/write_log.h"

namespace cdtb::game {
namespace {

// 한 관문의 정의. **있어야 하는 원본 바이트**를 같이 들고 있는 것이 핵심이다 -
// 게임이 갱신되면 그 자리의 코드가 달라지고, 확인 없이 쓰면 남의 명령을 부순다.
// 이 저장소는 고정 RVA 가 갱신마다 영역별로 다르게 밀린다는 것을 실측했다
// (game-update-rva-drift: +0x2040/+0x1630/+0x1740/+0x4150 - 단일 델타가 아니다).
struct GateDef {
    const char* name;
    const char* what;
    std::uintptr_t rva;
    std::uint8_t want[8];   // 원본(len 바이트만 본다)
    std::uint8_t with[8];   // 쓸 값
    std::size_t len;
};

constexpr GateDef kGates[kGateCount] = {
    {"습득 경로 무시",
     "\"특정 조건을 통해 배울 수 있습니다\" 를 넘긴다",
     0x0E0AB860,
     {0x48, 0x89, 0x5C},              // mov [rsp+0x18], rbx  (프롤로그 머리)
     {0xB0, 0x01, 0xC3},              // mov al,1 ; ret
     3},
    {"결속 비용 무시",
     "어비스 결속이 모자라도 통과시킨다(결속 수는 안 줄인다)",
     0x0208B639,
     {0x0F, 0x9D, 0x44, 0x24, 0x40},  // setge byte [rsp+0x40]
     {0xC6, 0x44, 0x24, 0x40, 0x01},  // mov byte [rsp+0x40], 1
     5},
};

struct GateState {
    std::uintptr_t site = 0;
    std::uint64_t orig = 0;   // 창 8바이트의 원본(되돌리기용)
    bool on = false;
    bool unsupported = false;
};
std::mutex g_mtx;
GateState g_state[kGateCount];

// 정렬된 8바이트를 한 번에 갈아 끼운다. 창은 부르는 쪽이 이미 확인했다.
bool write_window(std::uintptr_t base, std::uint64_t value) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(base), 8,
                        PAGE_EXECUTE_READWRITE, &old)) {
        log::warnf("스킬 관문: VirtualProtect 실패(0x{:X})",
                   static_cast<unsigned long>(GetLastError()));
        return false;
    }
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(base),
                           static_cast<long long>(value));
    VirtualProtect(reinterpret_cast<void*>(base), 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base), 8);
    return true;
}

}  // namespace

bool patch_window(std::uintptr_t addr, std::size_t len, std::uintptr_t* base,
                  std::size_t* off) {
    if (base == nullptr || off == nullptr) return false;
    if (len == 0 || len > 8) return false;
    const std::uintptr_t b = addr & ~static_cast<std::uintptr_t>(7);
    const std::size_t o = static_cast<std::size_t>(addr - b);
    if (o + len > 8) return false;   // 창 두 개에 걸친다 - 원자로 못 쓴다
    *base = b;
    *off = o;
    return true;
}

std::uint64_t patch_splice(std::uint64_t orig, std::size_t off,
                           const std::uint8_t* with, std::size_t len) {
    if (with == nullptr || len == 0 || off + len > 8) return orig;
    std::uint8_t b[8];
    std::memcpy(b, &orig, 8);
    std::memcpy(b + off, with, len);
    std::uint64_t out = 0;
    std::memcpy(&out, b, 8);
    return out;
}

SkillGateInfo skillgate_info(int gate) {
    SkillGateInfo i;
    if (gate < 0 || gate >= kGateCount) return i;
    i.name = kGates[gate].name;
    i.what = kGates[gate].what;
    std::lock_guard<std::mutex> lk(g_mtx);
    i.on = g_state[gate].on;
    i.unsupported = g_state[gate].unsupported;
    i.site = g_state[gate].site;
    return i;
}

bool skillgate_set(const mem::Reader& reader, int gate, bool on) {
    if (gate < 0 || gate >= kGateCount) return false;
    const GateDef& d = kGates[gate];
    std::lock_guard<std::mutex> lk(g_mtx);
    GateState& s = g_state[gate];
    if (s.unsupported) return false;
    if (s.on == on) return true;

    const std::uintptr_t site = reader.module_base() + d.rva;
    std::uintptr_t base = 0;
    std::size_t off = 0;
    if (!patch_window(site, d.len, &base, &off)) {
        // 창 두 개에 걸치면 원자로 못 쓴다. 그런 자리는 애초에 고르지 않았지만,
        // 모듈 기준이 8바이트 정렬이 아닌 판이 오면 여기서 걸린다.
        log::warnf("스킬 관문 '{}': 0x{:X} 가 8바이트 창에 안 들어간다", d.name,
                   site);
        s.unsupported = true;
        return false;
    }

    if (on) {
        std::uint64_t cur = 0;
        std::memcpy(&cur, reinterpret_cast<const void*>(base), 8);
        std::uint8_t b[8];
        std::memcpy(b, &cur, 8);
        // **원본이 우리가 아는 바이트인지 먼저 본다.** 게임이 갱신되면 그 자리의
        // 코드가 달라지고, 확인 없이 쓰면 남의 명령을 부순다. 고정 RVA 는 갱신마다
        // 영역별로 다르게 밀린다(game-update-rva-drift) - 단일 델타로 못 민다.
        if (std::memcmp(b + off, d.want, d.len) != 0) {
            log::warnf("스킬 관문 '{}': 0x{:X} 의 원본이 다르다 - 설치 안 함"
                       " (게임이 갱신됐을 수 있다)", d.name, site);
            s.unsupported = true;
            s.site = site;
            return false;
        }
        const std::uint64_t next = patch_splice(cur, off, d.with, d.len);
        log_write(d.name, site, "원본", "패치");
        if (!write_window(base, next)) return false;
        s.site = site;
        s.orig = cur;
        s.on = true;
        log::infof("스킬 관문 '{}' 켬: 0x{:X} ({}바이트)", d.name, site, d.len);
        return true;
    }

    // 끄기: 켤 때 떠 둔 창 8바이트를 그대로 되돌린다. 그 사이 누가 같은 창을
    // 바꿨다면 되돌리는 것이 오히려 해가 되므로, 지금 값이 우리가 쓴 것인지 본다.
    std::uint64_t cur = 0;
    std::memcpy(&cur, reinterpret_cast<const void*>(base), 8);
    const std::uint64_t mine = patch_splice(s.orig, off, d.with, d.len);
    if (cur != mine) {
        log::warnf("스킬 관문 '{}': 지금 값이 우리가 쓴 것과 다르다 - 안 되돌린다",
                   d.name);
        return false;
    }
    log_write(d.name, site, "패치", "원본");
    if (!write_window(base, s.orig)) return false;
    s.on = false;
    log::infof("스킬 관문 '{}' 끔: 0x{:X}", d.name, site);
    return true;
}

void skillgate_remove_all() {
    std::lock_guard<std::mutex> lk(g_mtx);
    for (int g = 0; g < kGateCount; ++g) {
        GateState& s = g_state[g];
        if (!s.on || s.site == 0) continue;
        std::uintptr_t base = 0;
        std::size_t off = 0;
        if (!patch_window(s.site, kGates[g].len, &base, &off)) continue;
        // 내려갈 때는 지금 값을 따지지 않는다 - 우리 코드가 사라지는 마당에
        // 우리 패치를 남겨 두는 것이 더 나쁘다.
        write_window(base, s.orig);
        s.on = false;
        log::infof("스킬 관문 '{}' 되돌림(모듈 해제)", kGates[g].name);
    }
}

}  // namespace cdtb::game
