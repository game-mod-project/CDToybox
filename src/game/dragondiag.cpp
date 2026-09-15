#include "game/dragondiag.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "mem/hook.h"
#include "mem/reader.h"

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

// 게이트: void* gate(rcx, rdx=out, r8, r9). r9 하위16=검색 키. 4 레지스터뿐.
using GateFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t);
// 스폰: 관찰된 5곳 모두 rcx, rdx=out, r8d, r9, [rsp+0x20]=out5 로 부른다.
// 결과는 *rdx 에 쓰인다(호출자들이 그 자리를 읽는다). 반환값 rax 는 안 쓰임.
using SpawnFn = void*(__fastcall*)(void*, void*, std::uint32_t, void*, void*);

GateFn g_orig_gate = nullptr;
SpawnFn g_orig_spawn = nullptr;
void* g_gate_target = nullptr;
void* g_spawn_target = nullptr;
std::uintptr_t g_base = 0;
std::atomic<bool> g_installed{false};

std::atomic<int> g_gate_budget{1000};
std::atomic<int> g_spawn_budget{2000};
std::atomic<long> g_spawn_seq{0};

std::uintptr_t caller_rva(const void* ret) {
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(ret);
    return (g_base != 0 && raw > g_base) ? raw - g_base : raw;
}

void* __fastcall det_gate(void* a1, void* out, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    const std::uint16_t key = static_cast<std::uint16_t>(a4);
    void* r = g_orig_gate(a1, out, a3, a4);
    std::uint32_t result = 0xFFFFFFFFu;
    if (out != nullptr) std::memcpy(&result, out, sizeof(result));
    if (g_gate_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("소환게이트: 키=0x{:X} 호출자=+0x{:X} 결과={}", key,
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
        log::infof("스폰0x2A22DE0[{}]: 호출자=+0x{:X} r8={} 결과={}", seq,
                   caller_rva(ret), r8, result);
    }
    return r;
}

}  // namespace

bool dragondiag_install(const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;

    g_base = reader.module_base();
    if (g_base == 0) return false;
    if (!mem::hook_init()) return false;

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

    g_installed.store(true, std::memory_order_release);
    log::infof(
        "소환 진단 v2 - 게이트 0x{:X} {} · 스폰 0x{:X} {} "
        "(휠에서 사자·드래곤 클릭 후 '스폰0x2A22DE0' 줄로 실경로 판정)",
        kGateRva, gate_ok ? "후킹" : "실패", kSpawnRva,
        spawn_ok ? "후킹" : "실패");
    return gate_ok || spawn_ok;
}

}  // namespace cdtb::game
