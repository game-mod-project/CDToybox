#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cdtb::game {

// 소환 가드가 돌릴 **호출 자리**와 그 자리의 판정.
//
// 헤더에 두는 이유는 시험이 보기 위해서다. `spawnguard.cpp` 는 `cdtb_tests`
// 타깃에 없고(윈도 API·케이브 할당이 들어 있다), 더할 경우 링크 배치가 바뀌어
// `watchpoint_shutdown_is_idempotent` 가 죽은 전례가 있다
// (`TROUBLESHOOTING.md` §1.5). 그래서 순수 부분만 뗀다.
//
// ---------------------------------------------------------------------------
// 이 자리는 무엇인가 (2026-09-20 실측, exe 1.0.0.2944)
// ---------------------------------------------------------------------------
// 명부 레코드의 종(+0x20)을 제자리에서 바꾸면 게임의 종류별 색인이 안 따라와
// "번호 -> 레코드" 조회가 **널**을 낸다. 게임은 그 반환값을 대부분의 자리에서
// `lea reg,[빈 레코드] ; test rax,rax ; cmovne` 로 막는데, **한 자리만** 막지
// 않고 곧장 역참조한다.
//
//     0x02B8DD2E  lea  rcx, [r12 + 0x18]
//     0x02B8DD33  mov  rdx, rbx
//     0x02B8DD36  call 0x2146110              <- 여기를 우리 썽크로 돌린다
//     0x02B8DD3B  cmp  qword ptr [rax+0x28], -1   <- 널이면 여기서 죽는다
//     0x02B8DD40  je   0x2B8DE32
//     0x02B8DD46  mov  ecx, 0xffff
//     0x02B8DD4B  cmp  word ptr [rax+0x20], cx    <- 빈 레코드 표식 둘째
//     0x02B8DD4F  je   0x2B8DE32
//
// 썽크가 널 대신 **빈 레코드**를 돌려주면 `cmp [rax+0x28], -1` 이 맞아
// `je` 로 빠진다 - 게임이 다른 자리에서 이미 하는 것과 같은 동작이다.
//
// **같은 이웃의 형제 자리**(게임이 스스로 막는 쪽)가 호출자 함수 안에 있고,
// 2760 기록과 레지스터까지 같다:
//
//     0x02B89E36  call 0x2146110
//     0x02B89E3B  lea  rbx, [rip+..]   -> 0x6CF2F70 빈 레코드
//     0x02B89E42  test rax, rax
//     0x02B89E45  cmovne rbx, rax
//     0x02B89E49  cmp  qword ptr [rbx+0x28], -1
//
// 사슬: 0x2B89CF0(호출자, 형제 자리 보유) -> 0x2B8C823 -> 0x2B8D2F0(우리 자리).
// 0x2B8D2F0 을 부르는 곳은 **한 곳뿐**이다.
//
// 빌드별 자리: 0x2AD51F8(2760) -> 0x2AD7238(2850) -> **0x2B8DD36(2944)**.
// 전문: `docs/superpowers/specs/2026-09-20-spawnguard-callsite.md`.
inline constexpr std::uint64_t kSpawnCallSiteRva = 0x2B8DD36;
inline constexpr std::uint64_t kSpawnLookupRva = 0x2146110;
inline constexpr std::uint64_t kSpawnEmptyRecordRva = 0x6CF2F70;

// 자리 뒤에 곧바로 오는 `cmp qword ptr [rax+0x28], -1`.
//
// **이 꼬리까지 봐야 자리가 확정된다.** 조회를 부르는 자리가 이 exe 에 273곳
// 이고 그중 266곳은 게임이 이미 막아 둔 자리다. `E8` + 대상만 보면 그 266곳도
// 전부 통과하므로, 자리가 낡아 우연히 다른 호출에 떨어지면 멀쩡한 자리를
// 건드리게 된다. 뒤가 이 꼬리인 자리는 **파일 397MB 전량에서 한 곳뿐**이다
// (실측 2026-09-20). `TROUBLESHOOTING.md` §1.5 의 "확인 폭 ≠ 쓰기 폭" 이다.
inline constexpr std::uint8_t kSpawnDerefTail[5] = {0x48, 0x83, 0x78, 0x28,
                                                    0xFF};

enum class SpawnSite {
    kOk,
    kShort,         // 읽은 바이트가 모자라다
    kNotCall,       // 0xE8 이 아니다 (자리가 낡았다)
    kWrongTarget,   // call 대상이 조회 함수가 아니다
    kGuarded,       // 조회는 맞는데 뒤가 그 `cmp` 가 아니다 - 이미 막힌 자리다
};

// `o` 는 `site` 에서 읽은 바이트. 10바이트 이상이어야 한다.
inline SpawnSite spawnguard_check_site(const std::uint8_t* o, std::size_t n,
                                       std::uintptr_t site,
                                       std::uintptr_t lookup) {
    if (o == nullptr || n < 5 + sizeof(kSpawnDerefTail)) return SpawnSite::kShort;
    if (o[0] != 0xE8) return SpawnSite::kNotCall;
    std::int32_t rel = 0;
    std::memcpy(&rel, o + 1, 4);
    const std::uintptr_t target =
        site + 5 + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
    if (target != lookup) return SpawnSite::kWrongTarget;
    if (std::memcmp(o + 5, kSpawnDerefTail, sizeof(kSpawnDerefTail)) != 0) {
        return SpawnSite::kGuarded;
    }
    return SpawnSite::kOk;
}

}  // namespace cdtb::game
