#include "game/nofall.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "core/log.h"
#include "core/write_log.h"
#include "game/nofall_cave.h"
#include "game/player.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 데미지/스테이터스 디스패처의 진입점. 2850 이미지 전량 스캔에서 **1곳**이다
// (RVA 0x01719850). 프롤로그만으로는 32곳이라 본문 네 명령까지 묶어야 유일해진다.
// 다음 게임 갱신에서 깨지면 뒤쪽을 풀어 가며 다시 찾는다 - `48 83 EC ??` 로 프레임
// 크기를 와일드카드하거나, 본문 13바이트
// `49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9` 로 찾아 **-0x14** 하면 사이트다.
constexpr const char* kSite =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 "
    "49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9";

std::atomic<bool> g_installed{false};
std::atomic<bool> g_unsupported{false};
std::atomic<bool> g_enabled{false};
std::uintptr_t g_site = 0;
std::uintptr_t g_cave = 0;
std::uintptr_t g_vars = 0;  // [0]=내 root, [8]=취소함, [16]=통과시킴
std::uint8_t g_orig[kNofallOrigSize]{};

bool vp(std::uintptr_t p) {
    return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
}
std::uintptr_t rq(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? static_cast<std::uintptr_t>(v) : 0;
}

// vars 는 우리가 잡은 메모리라 항상 유효하지만, 해제 경로와 엇갈릴 수 있으니
// 읽기·쓰기 모두 SEH 로 감싼다.
std::uint64_t vars_read(std::size_t off) {
    if (g_vars == 0) return 0;
    __try {
        return *reinterpret_cast<volatile std::uint64_t*>(g_vars + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
void vars_write(std::size_t off, std::uint64_t v) {
    if (g_vars == 0) return;
    __try {
        *reinterpret_cast<volatile std::uint64_t*>(g_vars + off) = v;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

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

bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    // 한 번 "이 빌드 미지원" 으로 갈렸으면 다시 보지 않는다. 이미지는
    // 세션 중에 바뀌지 않으므로 결과가 달라질 수 없다. 이것이 없어
    // 2.4초마다 같은 WARN 을 썼고, 로그 728줄 중 289줄(40%)이 그것만으로
    // 채워져 조사 로그가 묻혔다(실측 2026-09-09).
    if (g_unsupported.load(std::memory_order_acquire)) return false;
    if (!rtti.loaded()) return false;

    const auto parsed = mem::parse_pattern(kSite);
    if (!parsed) return false;
    const auto& img = rtti.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) {  // 유일하지 않으면 패치 금지(안전)
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 디스패처 시그니처가 유일하지 않다({}) - 설치 안 함",
                   hits.size());
        return false;
    }
    const std::uint64_t rva = static_cast<std::uint64_t>(hits[0] - img.data());
    const std::uintptr_t site = reader.module_base() + rva;

    // 사이트 첫 명령 `mov [rsp+8], rbx` 가 정확히 5바이트다. 라이브에서 복사한다.
    std::uint8_t orig[kNofallOrigSize]{};
    if (!reader.read(site, orig, sizeof(orig))) return false;

    void* vars = VirtualAlloc(nullptr, kNofallVarsSize,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (vars == nullptr) return false;
    std::memset(vars, 0, kNofallVarsSize);
    const std::uintptr_t vars_at = reinterpret_cast<std::uintptr_t>(vars);

    void* cave = alloc_near(site, kNofallCaveSize);
    if (cave == nullptr) {
        VirtualFree(vars, 0, MEM_RELEASE);
        return false;
    }

    // 케이브의 꼬리 점프는 원본 명령을 실행한 뒤 site+5 로 돌아간다.
    const NofallCave code = nofall_build_cave(orig, vars_at, site);
    if (!code.ok) {
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 케이브 조립 실패({}) - 설치 안 함", code.why);
        VirtualFree(cave, 0, MEM_RELEASE);
        VirtualFree(vars, 0, MEM_RELEASE);
        return false;
    }

    std::memcpy(cave, code.code.data(), code.code.size());
    FlushInstructionCache(GetCurrentProcess(), cave, code.code.size());

    // 사이트 패치: E9 rel32(케이브로) **5바이트만**. 첫 명령이 정확히 5바이트라
    // NOP 패딩이 필요 없다.
    const std::int32_t rel = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(cave) - (site + 5));
    std::uint8_t patch[kNofallOrigSize] = {0xE9, 0, 0, 0, 0};
    std::memcpy(patch + 1, &rel, 4);

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), kNofallOrigSize,
                        PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        VirtualFree(vars, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(site), patch, kNofallOrigSize);
    VirtualProtect(reinterpret_cast<void*>(site), kNofallOrigSize, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site),
                          kNofallOrigSize);

    g_site = site;
    g_cave = reinterpret_cast<std::uintptr_t>(cave);
    g_vars = vars_at;
    std::memcpy(g_orig, orig, kNofallOrigSize);
    g_installed.store(true, std::memory_order_release);
    log::infof("낙사 훅 설치: site=0x{:X} cave=0x{:X} 케이브 {}바이트", site,
               g_cave, code.code.size());
    return true;
}

bool nofall_installed() { return g_installed.load(std::memory_order_acquire); }
bool nofall_unsupported() {
    return g_unsupported.load(std::memory_order_acquire);
}

void nofall_set(bool on) {
    log_write("낙사 방지", g_vars,
              g_enabled.load(std::memory_order_acquire) ? "on" : "off",
              on ? "on" : "off");
    g_enabled.store(on, std::memory_order_release);
    // 끄면 root 를 지운다 -> 케이브의 `test rax,rax / je done` 이 곧바로 빠진다.
    if (!on) vars_write(kNofallOwner, 0);
}

bool nofall_enabled() { return g_enabled.load(std::memory_order_acquire); }

std::uint64_t nofall_zeroed() { return vars_read(kNofallZeroed); }
std::uint64_t nofall_let_through() { return vars_read(kNofallLetThrough); }

void nofall_refresh(const mem::Reader& reader) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    if (g_vars == 0) return;
    if (!g_enabled.load(std::memory_order_acquire)) {
        vars_write(kNofallOwner, 0);
        return;
    }
    // 디스패처의 rcx 와 비교할 대상. player.h 의 게이지 체인과 같은 자리다.
    // 캐릭터 교체·지역 이동으로 바뀌므로 캐시하지 않고 매번 다시 계산한다.
    // 한 단계라도 끊기면 0 을 써서 **보호를 끈다** - 낡은 root 를 남기면 그 주소를
    // 물려받은 다른 개체의 피해까지 지울 수 있다.
    const std::uintptr_t ch = player_char();
    const std::uintptr_t actor = vp(ch) ? rq(reader, ch + 0x68) : 0;
    const std::uintptr_t mark = vp(actor) ? rq(reader, actor + 0x20) : 0;
    const std::uintptr_t root = vp(mark) ? rq(reader, mark + 0x18) : 0;
    vars_write(kNofallOwner, vp(root) ? static_cast<std::uint64_t>(root) : 0);
}

}  // namespace cdtb::game
