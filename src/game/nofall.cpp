#include "game/nofall.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <intrin.h>

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
// probe `nofall` 이 설치 없이 이 유일성을 찍어 준다.
constexpr const char* kSite =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 "
    "49 8B C1 49 8B E8 0F B7 DA 48 8B F1 4D 85 C9";

// 0 안 함, 1 설치 중(다른 스레드는 손대지 않는다), 2 끝. 지금 호출자는 분석 루프
// 하나뿐이지만, specguard 가 같은 자리에서 물린 적이 있어(Codex 지적 2026-09-11)
// 처음부터 CAS 로 막는다 - "백업 호출" 한 줄이 추가되는 순간 이중 패치가 된다.
std::atomic<int> g_state{0};
std::atomic<bool> g_unsupported{false};
std::atomic<bool> g_enabled{false};
std::atomic<std::uintptr_t> g_deref{0};  // 폴트를 지켜볼 명령 주소(VEH)
std::atomic<std::uintptr_t> g_done{0};   // 폴트 시 떨어뜨릴 자리(VEH)
std::atomic<std::uint64_t> g_faults{0};
// 마지막으로 owner 에 써 넣은 값. 같은 값을 매 프레임 다시 쓰지 않으려는 것이다.
// nofall_set 이 owner 를 직접 건드리므로 그쪽에서 반드시 무효화해야 한다 -
// 안 하면 껐다 켰을 때 "root 가 그대로" 라 다시 안 써져 보호가 안 켜진다.
constexpr std::uint64_t kOwnerUnknown = ~0ULL;
std::atomic<std::uint64_t> g_last_owner{kOwnerUnknown};
std::uintptr_t g_site = 0;
std::uintptr_t g_cave = 0;
std::uintptr_t g_vars = 0;  // [0]=내 root, [8]=취소함, [16]=통과시킴
// 원본 5바이트. 지금은 아무도 읽지 않는다 - 언인스톨을 넣게 되면 그때 복원용으로
// 쓴다(순서: owner=0 -> 원본 복원(원자 쓰기) -> 케이브·vars 는 해제하지 않는다).
std::uint8_t g_orig[kNofallOrigSize]{};

bool vp(std::uintptr_t p) {
    return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
}
std::uintptr_t rq(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? static_cast<std::uintptr_t>(v) : 0;
}

// vars 는 우리가 잡은 메모리라 항상 유효하지만, 해제 경로와 엇갈릴 수 있으니
// 읽기·쓰기 모두 SEH 로 감싼다. 오프셋 0/8/16 이라 전부 8바이트 정렬이고,
// x86-64 에서 정렬된 qword 적재·저장은 단일 복사 원자성을 가진다(찢기지 않는다).
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

// 케이브의 가해자 역참조가 매핑 안 된 주소를 물었을 때만 끼어든다. 그 한 명령의
// 주소와 정확히 같을 때가 아니면 손대지 않으므로, 진짜 크래시를 가리지 않는다.
// 폴트 = "가해자를 판정할 수 없다" 이므로 **그대로 통과**(done)로 떨군다 - 피해를
// 지우는 쪽이 아니라 남기는 쪽이 안전하다.
LONG CALLBACK on_cave_fault(EXCEPTION_POINTERS* ep) {
    if (ep == nullptr || ep->ExceptionRecord == nullptr ||
        ep->ContextRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto deref = g_deref.load(std::memory_order_acquire);
    const auto done = g_done.load(std::memory_order_acquire);
    if (deref == 0 || done == 0) return EXCEPTION_CONTINUE_SEARCH;
    const auto at =
        reinterpret_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
    if (at != deref) return EXCEPTION_CONTINUE_SEARCH;
    ep->ContextRecord->Rip = static_cast<DWORD64>(done);
    g_faults.fetch_add(1, std::memory_order_relaxed);
    return EXCEPTION_CONTINUE_EXECUTION;
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

// 사이트 앞 8바이트를 **한 번의 정렬된 원자 쓰기**로 바꾼다. `memcpy` 로 5바이트를
// 쓰면 보통 dword + byte 두 번으로 나가는데, 그 틈에 다른 스레드가 진입하면
// `E9` + 옛 5번째 바이트라는 없는 목적지로 뛴다. 이 사이트는 전투 코드 39곳이
// 부르는 공용 디스패처라 "그 순간 누가 거기 있을" 확률이 다른 훅보다 훨씬 크다.
bool patch_site_atomically(std::uintptr_t site, const std::uint8_t* patch) {
    std::uint64_t q = 0;
    std::memcpy(&q, reinterpret_cast<const void*>(site), 8);  // 뒤 3바이트 보존
    std::memcpy(&q, patch, kNofallOrigSize);
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(site), 8, PAGE_EXECUTE_READWRITE,
                        &old)) {
        log::warnf("낙사: VirtualProtect 실패(0x{:X}) - 설치 안 함",
                   static_cast<unsigned long>(GetLastError()));
        return false;
    }
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(site),
                           static_cast<long long>(q));
    VirtualProtect(reinterpret_cast<void*>(site), 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 8);
    return true;
}

}  // namespace

bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_state.load(std::memory_order_acquire) == 2) return true;
    // 한 번 "이 빌드 미지원" 으로 갈렸으면 다시 보지 않는다. 이미지는
    // 세션 중에 바뀌지 않으므로 결과가 달라질 수 없다. 이것이 없어
    // 2.4초마다 같은 WARN 을 썼고, 로그 728줄 중 289줄(40%)이 그것만으로
    // 채워져 조사 로그가 묻혔다(실측 2026-09-09).
    if (g_unsupported.load(std::memory_order_acquire)) return false;
    if (!rtti.loaded()) return false;

    int expect = 0;
    if (!g_state.compare_exchange_strong(expect, 1, std::memory_order_acq_rel)) {
        return g_state.load(std::memory_order_acquire) == 2;
    }
    // 여기부터는 이 스레드 혼자다. 어느 갈래로 나가든 g_state 를 되돌려 놓는다.
    auto give_up = [&]() { g_state.store(0, std::memory_order_release); };

    const auto parsed = mem::parse_pattern(kSite);
    if (!parsed) {
        // kSite 는 컴파일 타임 상수라 이 실패는 세션 내내 절대 안 바뀐다.
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: AOB 문자열을 해석하지 못했다 - 설치 안 함");
        give_up();
        return false;
    }
    const auto& img = rtti.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) {  // 유일하지 않으면 패치 금지(안전)
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 디스패처 시그니처가 유일하지 않다({}) - 설치 안 함",
                   hits.size());
        give_up();
        return false;
    }
    const std::uint64_t rva = static_cast<std::uint64_t>(hits[0] - img.data());
    const std::uintptr_t site = reader.module_base() + rva;
    if ((site & 7) != 0) {
        // 원자 패치가 8바이트 정렬을 요구한다. 안 맞으면 찢긴 명령을 무릅쓰느니
        // 설치하지 않는다(실측 2850 에서는 site % 16 == 0 이라 걸리지 않는다).
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 사이트 0x{:X} 가 8바이트 정렬이 아니다 - 설치 안 함",
                   site);
        give_up();
        return false;
    }

    // 사이트 첫 명령 `mov [rsp+8], rbx` 가 정확히 5바이트다. 라이브에서 복사한다.
    std::uint8_t orig[kNofallOrigSize]{};
    if (!reader.read(site, orig, sizeof(orig))) {
        give_up();
        return false;
    }

    void* vars = VirtualAlloc(nullptr, kNofallVarsSize,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (vars == nullptr) {
        give_up();
        return false;
    }
    std::memset(vars, 0, kNofallVarsSize);
    const std::uintptr_t vars_at = reinterpret_cast<std::uintptr_t>(vars);

    void* cave = alloc_near(site, kNofallCaveSize);
    if (cave == nullptr) {
        VirtualFree(vars, 0, MEM_RELEASE);
        give_up();
        return false;
    }

    // 케이브의 꼬리 점프는 원본 명령을 실행한 뒤 site+5 로 돌아간다.
    const NofallCave code = nofall_build_cave(orig, vars_at, site);
    if (!code.ok) {
        g_unsupported.store(true, std::memory_order_release);
        log::warnf("낙사: 케이브 조립 실패({}) - 설치 안 함", code.why);
        VirtualFree(cave, 0, MEM_RELEASE);
        VirtualFree(vars, 0, MEM_RELEASE);
        give_up();
        return false;
    }

    std::memcpy(cave, code.code.data(), code.code.size());
    FlushInstructionCache(GetCurrentProcess(), cave, code.code.size());

    // 폴트 가드를 **패치보다 먼저** 건다 - 패치가 끝나는 순간부터 케이브가 돈다.
    const auto cave_at = reinterpret_cast<std::uintptr_t>(cave);
    g_deref.store(cave_at + code.deref_at, std::memory_order_release);
    g_done.store(cave_at + code.done_at, std::memory_order_release);
    static std::atomic<bool> veh_added{false};
    bool expect_veh = false;
    if (veh_added.compare_exchange_strong(expect_veh, true,
                                          std::memory_order_acq_rel)) {
        if (::AddVectoredExceptionHandler(1, on_cave_fault) == nullptr) {
            log::warnf("낙사: 폴트 가드 등록 실패 - 설치 안 함");
            g_deref.store(0, std::memory_order_release);
            g_done.store(0, std::memory_order_release);
            veh_added.store(false, std::memory_order_release);
            VirtualFree(cave, 0, MEM_RELEASE);
            VirtualFree(vars, 0, MEM_RELEASE);
            give_up();
            return false;
        }
    }

    // 사이트 패치: E9 rel32(케이브로) 5바이트. 첫 명령이 정확히 5바이트라 NOP
    // 패딩이 필요 없고, 뒤 3바이트는 그대로 두고 8바이트를 원자적으로 바꾼다.
    const std::int32_t rel =
        static_cast<std::int32_t>(cave_at - (site + kNofallOrigSize));
    std::uint8_t patch[kNofallOrigSize] = {0xE9, 0, 0, 0, 0};
    std::memcpy(patch + 1, &rel, 4);
    if (!patch_site_atomically(site, patch)) {
        g_deref.store(0, std::memory_order_release);
        g_done.store(0, std::memory_order_release);
        VirtualFree(cave, 0, MEM_RELEASE);
        VirtualFree(vars, 0, MEM_RELEASE);
        give_up();
        return false;
    }

    g_site = site;
    g_cave = cave_at;
    g_vars = vars_at;
    std::memcpy(g_orig, orig, kNofallOrigSize);
    g_state.store(2, std::memory_order_release);
    log::infof("낙사 훅 설치: site=0x{:X} cave=0x{:X} 케이브 {}바이트", site,
               g_cave, code.code.size());
    return true;
}

bool nofall_installed() { return g_state.load(std::memory_order_acquire) == 2; }
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
    g_last_owner.store(kOwnerUnknown, std::memory_order_release);
}

bool nofall_enabled() { return g_enabled.load(std::memory_order_acquire); }

std::uint64_t nofall_zeroed() { return vars_read(kNofallZeroed); }
std::uint64_t nofall_let_through() { return vars_read(kNofallLetThrough); }
std::uint64_t nofall_faults() {
    return g_faults.load(std::memory_order_relaxed);
}

void nofall_refresh(const mem::Reader& reader) {
    if (g_state.load(std::memory_order_acquire) != 2) return;
    if (g_vars == 0) return;
    std::uint64_t want = 0;
    if (g_enabled.load(std::memory_order_acquire)) {
        // 디스패처의 rcx 와 비교할 대상. player.h 의 게이지 체인과 같은 자리다.
        // 캐릭터 교체·지역 이동으로 바뀌므로 캐시하지 않고 매번 다시 계산한다.
        // 한 단계라도 끊기면 0 을 써서 **보호를 끈다** - 낡은 root 를 남기면 그
        // 주소를 물려받은 다른 개체의 피해까지 지울 수 있다.
        const std::uintptr_t ch = player_char();
        const std::uintptr_t actor = vp(ch) ? rq(reader, ch + 0x68) : 0;
        const std::uintptr_t mark = vp(actor) ? rq(reader, actor + 0x20) : 0;
        const std::uintptr_t root = vp(mark) ? rq(reader, mark + 0x18) : 0;
        if (vp(root)) want = static_cast<std::uint64_t>(root);
    }
    // 값이 그대로면 쓰지 않는다 - owner 와 두 카운터가 같은 캐시 라인이라,
    // 렌더 스레드의 초당 60회 저장이 게임 스레드들의 lock inc 와 부딪힌다.
    if (g_last_owner.load(std::memory_order_acquire) == want) return;
    vars_write(kNofallOwner, want);
    g_last_owner.store(want, std::memory_order_release);
}

}  // namespace cdtb::game
