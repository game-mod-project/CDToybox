#include "game/dragondiag.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "mem/hook.h"
#include "mem/reader.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

// 1.0.0.2850 확정 RVA (2026-09-14~15 재확인, disasm.py).
//
// **반증(2026-09-15 라이브)**: 실제 휠 클릭(사자 소환됨)은 게이트 0x2ACA250 을
// 전혀 안 밟았다 - 게이트는 2962 서버 메시지(무효 경로)에서만 불린다. 따라서
// 진짜 소환 경로를 찾으려면 실제 스폰 프리미티브 0x2A22DE0 을 직접 본다.
// 이 함수는 게이트(0x2ACA441) 포함 5곳에서 불린다; 어느 호출자가 휠 클릭의
// 스폰인지 _ReturnAddress 로 가린다.
constexpr std::uint64_t kGateRva = 0x2ACA250;   // (대조) 소환 게이트
constexpr std::uint64_t kSpawnRva = 0x2A22DE0;  // 실제 스폰 프리미티브
// 휠 소환 사슬의 두 지점 (2026-09-15 실측으로 자리를 고쳤다).
//
// v4 에서 `0x2ADBA00`(게이트를 부르는 함수)에 걸었는데 **A.T.A.G. 도 드래곤도
// 안 들어왔다.** 그 함수는 목록에서 부를 때 같은 다른 경로였다. 실제 휠 경로는
// 스폰 로그가 찍어 준 호출자 `+0x2B78493` 쪽이고, 거기서 뒤로 훑어 함수 시작을
// 찾았다 - `0x2B78330`(프롤로그 `48 89 5c 24 08 48 89 74 24 18 55 57 41 54 41 56`).
// 한 겹 위는 앞선 세션이 기록해 둔 `0x2941350` 이다.
//
// 실측된 갈림: **A.T.A.G. 클릭은 스폰 프리미티브까지 가고(r8=1000019),
// 드래곤 클릭은 아무 줄도 안 남긴다.** 이 둘 중 어디까지 오는지가 다음을 정한다.
constexpr std::uint64_t kCallFnRva = 0x2B78330;    // 휠 소환 루틴
constexpr std::uint64_t kWheelFnRva = 0x2941350;   // 그 한 겹 위
// **거부 코드를 직접 찍는다(2026-09-15).** 드래곤이 "호출할 수 없는 장소입니다" 로
// 막히는데 후보가 여럿이다(`eErrNoCallVehicleInvalidPosition` · `...InvalidAir` ·
// `...MercenaryIndoor` · `...MercenaryRegion` · `...BlockedSpawnPositionByObstacle` ·
// `eErrNoFailToFindSummonMercenaryPosition` …). 하나씩 찍어 보는 대신 **게임이
// 실제로 내는 코드를 읽는다.**
//
// 근거: 휠 소환 헬퍼가 스폰 결과를 보고 0 이 아니면 그 코드를 알림으로 실어 보낸다.
//
//   0x2B7849E  ebx = [rbp+0xA8]        스폰이 쓴 결과 코드
//   0x2B784A4  test ebx,ebx / je       0 이면 조용히 지나간다
//   0x2B784AC  esi = 0x3F5             알림 메시지 id
//   0x2B784BB  [rsp+0x43] = ebx        ★ 그 코드가 본문에 실린다
//
// 그래서 **알림 송신(AsyncRequestSend)** 에 걸고 본문 머리가 0x3F5 인 것만 찍는다.
// 다른 알림은 건드리지 않는다. 읽기만 하고 원본을 그대로 부른다.
constexpr std::uint64_t kNotifyRva = 0xFB7F060;
constexpr std::uint16_t kNotifyMsgId = 0x3F5;   // 클라 알림
constexpr std::size_t kNotifyCodeOff = 3;       // 본문 +3 에 u32 코드

// 게이트: void* gate(rcx, rdx=out, r8, r9). r9 하위16=검색 키. 4 레지스터뿐.
using GateFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t);
// 스폰: 관찰된 5곳 모두 rcx, rdx=out, r8d, r9, [rsp+0x20]=out5 로 부른다.
// 결과는 *rdx 에 쓰인다(호출자들이 그 자리를 읽는다). 반환값 rax 는 안 쓰임.
using SpawnFn = void*(__fastcall*)(void*, void*, std::uint32_t, void*, void*);

// 알림 송신: 진입부가 `rdi = r8`(본문) · `esi = r9w` · `r12d = dx` 로 받는다.
// 레지스터 넷을 그대로 흘려보내면 되므로 4인자로 선언한다(스택 인자를 안 쓴다).
using NotifyFn = void*(__fastcall*)(void*, std::uint64_t, void*, std::uint64_t);

GateFn g_orig_gate = nullptr;
// 바깥 함수도 레지스터 넷만 흘려보낸다(스택 인자는 호출자 프레임에 그대로 있다).
using CallFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t);
CallFn g_orig_callfn = nullptr;
CallFn g_orig_wheelfn = nullptr;
void* g_callfn_target = nullptr;
void* g_wheelfn_target = nullptr;
std::atomic<int> g_callfn_budget{300};
std::atomic<int> g_wheelfn_budget{300};
SpawnFn g_orig_spawn = nullptr;
NotifyFn g_orig_notify = nullptr;
void* g_gate_target = nullptr;
void* g_spawn_target = nullptr;
void* g_notify_target = nullptr;
std::uintptr_t g_base = 0;
std::atomic<bool> g_installed{false};

std::atomic<int> g_gate_budget{1000};
std::atomic<int> g_spawn_budget{2000};
// 알림은 거부가 날 때만 찍히지만, 예산을 둬서 로그가 묻히지 않게 한다
// (TROUBLESHOOTING 6.19 - 한 줄이 로그를 묻는다).
std::atomic<int> g_notify_budget{200};
std::atomic<long> g_spawn_seq{0};

std::uintptr_t caller_rva(const void* ret) {
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(ret);
    return (g_base != 0 && raw > g_base) ? raw - g_base : raw;
}

// 알림 본문 머리가 0x3F5 인 것만 찍는다. **읽기만** 하고 그대로 흘려보낸다.
void* __fastcall det_notify(void* a1, std::uint64_t a2, void* body,
                            std::uint64_t a4) {
    if (body != nullptr) {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(body);
        std::uint8_t head[16] = {};
        // **SEH 로 감싼 읽기를 쓴다** - 본문이 늘 16바이트 이상이라는 보장이 없다.
        if (mem::safe_read_bytes(at, head, sizeof head)) {
            std::uint16_t id = 0;
            std::memcpy(&id, head, sizeof id);
            if (id == kNotifyMsgId &&
                g_notify_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
                std::uint32_t code = 0;
                std::memcpy(&code, head + kNotifyCodeOff, sizeof code);
                log::infof("클라 알림 0x3F5: 코드 0x{:08X} ({}) · 본문 "
                           "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
                           "{:02X} {:02X} {:02X} {:02X} {:02X}",
                           code, code, head[0], head[1], head[2], head[3],
                           head[4], head[5], head[6], head[7], head[8], head[9],
                           head[10], head[11]);
            }
        }
    }
    return g_orig_notify(a1, a2, body, a4);
}

// 게이트를 부르는 바깥 함수. **들어오는지**를 본다 - 들어오는데 게이트 줄이
// 안 따라오면 거부는 이 안이고, 아예 안 들어오면 더 앞(UI)이다.
void* __fastcall det_callfn(void* a1, void* a2, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    if (g_callfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("휠소환 진입(0x2B78330): 호출자=+0x{:X} rcx=0x{:X} rdx=0x{:X}"
                   " r8=0x{:X} r9=0x{:X}",
                   caller_rva(ret), reinterpret_cast<std::uintptr_t>(a1),
                   reinterpret_cast<std::uintptr_t>(a2),
                   reinterpret_cast<std::uintptr_t>(a3), a4);
    }
    return g_orig_callfn(a1, a2, a3, a4);
}

// 휠 소환 사슬의 한 겹 위. 여기까지도 안 오면 거부는 UI 안이다.
void* __fastcall det_wheelfn(void* a1, void* a2, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    if (g_wheelfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("휠함수 진입(0x2941350): 호출자=+0x{:X} rcx=0x{:X} rdx=0x{:X}"
                   " r8=0x{:X} r9=0x{:X}",
                   caller_rva(ret), reinterpret_cast<std::uintptr_t>(a1),
                   reinterpret_cast<std::uintptr_t>(a2),
                   reinterpret_cast<std::uintptr_t>(a3), a4);
    }
    return g_orig_wheelfn(a1, a2, a3, a4);
}

void* __fastcall det_gate(void* a1, void* out, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    const std::uint16_t key = static_cast<std::uint16_t>(a4);
    void* r = g_orig_gate(a1, out, a3, a4);
    std::uint32_t result = 0xFFFFFFFFu;
    if (out != nullptr) std::memcpy(&result, out, sizeof(result));
    if (g_gate_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("소환게이트: 키=0x{:X} 호출자=+0x{:X} 오류코드={}", key,
                   caller_rva(ret), result);
    }
    return r;
}

void* __fastcall det_spawn(void* rcx, void* out, std::uint32_t r8, void* r9,
                           void* a5) {
    const void* ret = _ReturnAddress();
    void* r = g_orig_spawn(rcx, out, r8, r9, a5);
    std::uint32_t result = 0xFFFFFFFFu;
    if (out != nullptr) std::memcpy(&result, out, sizeof(result));
    const long seq = g_spawn_seq.fetch_add(1, std::memory_order_relaxed);
    if (g_spawn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        // **게이트의 "결과" 와 뜻이 다르다.** 게이트는 오류 코드(0 = 오류 없음)
        // 이고, 여기 out 자리는 만들어진 객체다 - 큰 값이면 만든 것, 0 이면 못
        // 만든 것이다. 같은 이름으로 찍다가 거꾸로 읽었다(2026-09-15).
        log::infof("스폰0x2A22DE0[{}]: 호출자=+0x{:X} r8={} 결과물=0x{:X} ({})",
                   seq, caller_rva(ret), r8, result,
                   result != 0 ? "만듦" : "없음");
    }
    return r;
}

}  // namespace

bool dragondiag_install(const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;

    g_base = reader.module_base();
    if (g_base == 0) return false;
    if (!mem::hook_init()) return false;

    g_wheelfn_target = reinterpret_cast<void*>(g_base + kWheelFnRva);
    const bool wheelfn_ok = mem::hook_install(
        g_wheelfn_target, &det_wheelfn,
        reinterpret_cast<void**>(&g_orig_wheelfn));
    if (!wheelfn_ok) {
        log::errorf("휠함수 후킹 실패 (RVA 0x{:X})", kWheelFnRva);
        g_wheelfn_target = nullptr;
    }

    g_callfn_target = reinterpret_cast<void*>(g_base + kCallFnRva);
    const bool callfn_ok = mem::hook_install(
        g_callfn_target, &det_callfn,
        reinterpret_cast<void**>(&g_orig_callfn));
    if (!callfn_ok) {
        log::errorf("호출함수 후킹 실패 (RVA 0x{:X})", kCallFnRva);
        g_callfn_target = nullptr;
    }

    g_gate_target = reinterpret_cast<void*>(g_base + kGateRva);
    const bool gate_ok = mem::hook_install(
        g_gate_target, &det_gate, reinterpret_cast<void**>(&g_orig_gate));
    if (!gate_ok) {
        log::errorf("소환게이트 후킹 실패 (RVA 0x{:X})", kGateRva);
        g_gate_target = nullptr;
    }

    g_spawn_target = reinterpret_cast<void*>(g_base + kSpawnRva);
    const bool spawn_ok = mem::hook_install(
        g_spawn_target, &det_spawn, reinterpret_cast<void**>(&g_orig_spawn));
    if (!spawn_ok) {
        log::errorf("스폰 후킹 실패 (RVA 0x{:X})", kSpawnRva);
        g_spawn_target = nullptr;
    }

    g_notify_target = reinterpret_cast<void*>(g_base + kNotifyRva);
    const bool notify_ok = mem::hook_install(
        g_notify_target, &det_notify, reinterpret_cast<void**>(&g_orig_notify));
    if (!notify_ok) {
        log::errorf("알림 후킹 실패 (RVA 0x{:X})", kNotifyRva);
        g_notify_target = nullptr;
    }

    g_installed.store(true, std::memory_order_release);
    log::infof(
        "소환 진단 v5 - 휠함수 0x{:X} {} · 휠소환 0x{:X} {} · 게이트 0x{:X} {} ·"
        " 스폰 0x{:X} {} · 알림 0x{:X} {} (드래곤을 눌렀을 때 '휠함수 진입' ·"
        " '휠소환 진입' 중 어디까지 나오는지가 갈림길이다)",
        kWheelFnRva, wheelfn_ok ? "후킹" : "실패", kCallFnRva,
        callfn_ok ? "후킹" : "실패", kGateRva, gate_ok ? "후킹" : "실패",
        kSpawnRva, spawn_ok ? "후킹" : "실패", kNotifyRva,
        notify_ok ? "후킹" : "실패");
    return gate_ok || spawn_ok || notify_ok || callfn_ok || wheelfn_ok;
}

}  // namespace cdtb::game
