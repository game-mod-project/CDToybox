#include "game/nofall.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <optional>
#include <vector>

#include "core/log.h"
#include "core/write_log.h"
#include "game/player.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 낙하 누적값 쓰기 사이트. 변위(??)는 런타임에 원본에서 복사한다.
constexpr const char* kSite =
    "48 89 5F ?? 48 8B 5C 24 ?? 48 89 77 ?? 66 89 6F";

std::atomic<bool> g_installed{false};
std::atomic<bool> g_unsupported{false};
std::atomic<bool> g_enabled{false};
std::uintptr_t g_site = 0;
std::uintptr_t g_cave = 0;
std::uintptr_t g_vars = 0;    // [0]=objAddr(케이브가 기록한 faller), [8]=playerAddr
std::uint8_t g_orig[9]{};

bool vp(std::uintptr_t p) {
    return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
}
std::uint64_t rq(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
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

// 플레이어 액터 체인 블록에서 faller 포인터를 찾는다(참고 모드 refIn 과 동일).
bool ref_in(const mem::Reader& r, std::uintptr_t base, std::size_t range,
            std::uint64_t target) {
    if (!vp(base)) return false;
    std::vector<std::uint8_t> buf(range);
    if (!r.read(base, buf.data(), buf.size())) return false;
    for (std::size_t k = 0; k + 8 <= buf.size(); k += 8) {
        std::uint64_t v = 0;
        std::memcpy(&v, buf.data() + k, 8);
        if (v == target) return true;
    }
    return false;
}

}  // namespace

bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;
    // 한 번 "이 빌드 미지원" 으로 갈렸으면 다시 보지 않는다. 이미지는
    // 세션 중에 바뀌지 않으므로 결과가 달라질 수 없다. 이것이 없어
    // 2.4초마다 같은 WARN 을 썯고, 로그 728줄 중 289줄(40%)이 그것만으로
    // 채워져 조사 로그가 묻혔다(실측 2026-09-09).
    if (g_unsupported.load(std::memory_order_acquire)) return false;
    if (!rtti.loaded()) return false;

    const auto parsed = mem::parse_pattern(kSite);
    if (!parsed) return false;
    const auto& img = rtti.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) {   // 유일하지 않으면 패치 금지(안전)
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 사이트 시그니처가 유일하지 않다({}) - 설치 안 함",
                   hits.size());
        return false;
    }
    const std::uint64_t rva = static_cast<std::uint64_t>(hits[0] - img.data());
    const std::uintptr_t site = reader.module_base() + rva;

    // 원본 9바이트(2개 명령: mov[rdi+d],rbx / mov rbx,[rsp+d])를 라이브에서 복사.
    std::uint8_t orig[9]{};
    if (!reader.read(site, orig, sizeof(orig))) return false;

    void* vars = VirtualAlloc(nullptr, 32, MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);
    if (vars == nullptr) return false;
    std::memset(vars, 0, 32);
    const std::uintptr_t objAddr = reinterpret_cast<std::uintptr_t>(vars);
    const std::uintptr_t playerAddr = objAddr + 8;

    void* cave = alloc_near(site, 128);
    if (cave == nullptr) {
        VirtualFree(vars, 0, MEM_RELEASE);
        return false;
    }

    // 케이브 조립(참고 모드와 동일).
    std::vector<std::uint8_t> b;
    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    auto addq = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back((v >> (i * 8)) & 0xFF);
    };
    add({0x50, 0x9C});                       // push rax / pushfq
    add({0x48, 0xB8}); addq(objAddr);        // mov rax, objAddr
    add({0x48, 0x89, 0x38});                 // mov [rax], rdi (누가 떨어지나 기록)
    add({0x48, 0xB8}); addq(playerAddr);     // mov rax, playerAddr
    add({0x48, 0x8B, 0x00});                 // mov rax, [rax] (학습된 플레이어)
    add({0x48, 0x39, 0xF8});                 // cmp rax, rdi
    add({0x74, 0x08});                       // je skip
    add({0x9D, 0x58});                       // popfq / pop rax
    add({orig[0], orig[1], orig[2], orig[3]});      // 원본 누적 쓰기
    add({0xEB, 0x02});                       // jmp after
    add({0x9D, 0x58});                       // skip: popfq / pop rax
    add({orig[4], orig[5], orig[6], orig[7], orig[8]});  // after: mov rbx,[rsp+d]
    add({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});      // jmp [rip+0]
    addq(static_cast<std::uint64_t>(site + 9));

    std::memcpy(cave, b.data(), b.size());
    FlushInstructionCache(GetCurrentProcess(), cave, b.size());

    // 사이트 패치: E9 rel32(케이브로) + 4 NOP = 9바이트.
    const std::int32_t rel = static_cast<std::int32_t>(
        reinterpret_cast<std::uintptr_t>(cave) - (site + 5));
    std::uint8_t patch[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
    std::memcpy(patch + 1, &rel, 4);

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 9,
                        PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(cave, 0, MEM_RELEASE);
        VirtualFree(vars, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(site), patch, 9);
    VirtualProtect(reinterpret_cast<void*>(site), 9, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 9);

    g_site = site;
    g_cave = reinterpret_cast<std::uintptr_t>(cave);
    g_vars = objAddr;
    std::memcpy(g_orig, orig, 9);
    g_installed.store(true, std::memory_order_release);
    log::infof("낙사 훅 설치: site=0x{:X} cave=0x{:X}", site,
               reinterpret_cast<std::uintptr_t>(cave));
    return true;
}

bool nofall_installed() { return g_installed.load(std::memory_order_acquire); }
bool nofall_unsupported() {
    return g_unsupported.load(std::memory_order_acquire);
}

void nofall_set(bool on) {
    log_write("낙사 방지", g_vars, g_enabled.load() ? "on" : "off", on ? "on" : "off");
    g_enabled.store(on, std::memory_order_release);
    if (!on && g_vars != 0) {
        // 학습된 플레이어를 지운다 -> 케이브의 cmp 가 절대 안 맞아 정상 낙사.
        __try {
            *reinterpret_cast<volatile std::uint64_t*>(g_vars + 8) = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
}

bool nofall_enabled() { return g_enabled.load(std::memory_order_acquire); }

void nofall_identify(const mem::Reader& reader) {
    if (!g_installed.load(std::memory_order_acquire)) return;
    if (!g_enabled.load(std::memory_order_acquire)) return;
    if (g_vars == 0) return;
    // 이미 학습됐으면 그대로.
    if (rq(reader, g_vars + 8) != 0) return;
    // 케이브가 기록한 마지막 faller.
    const std::uint64_t faller = rq(reader, g_vars);
    if (!vp(faller)) return;
    // 플레이어 액터 체인에 그 faller 가 참조돼 있으면 = 내가 떨어진 것.
    // player_char() = 스티키로 고정된 플레이어 char(게이지 체인의 actor 역할).
    const std::uintptr_t actor = player_char();
    if (!vp(actor)) return;
    const std::uintptr_t p1 = rq(reader, actor + 0x68);
    const std::uintptr_t p2 = vp(p1) ? rq(reader, p1 + 0x20) : 0;
    const std::uintptr_t p3 = vp(p2) ? rq(reader, p2 + 0x18) : 0;
    const struct {
        std::uintptr_t base;
        std::size_t range;
    } blocks[] = {{actor, 0x8000}, {p1, 0x2000}, {p2, 0x2000}, {p3, 0x2000}};
    for (const auto& e : blocks) {
        if (ref_in(reader, e.base, e.range, faller)) {
            __try {
                *reinterpret_cast<volatile std::uint64_t*>(g_vars + 8) = faller;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return;
            }
            log::infof("낙사: 플레이어 faller 학습 0x{:X}", faller);
            return;
        }
    }
}

}  // namespace cdtb::game
