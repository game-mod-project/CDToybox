#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace cdtb::game {

// 특수기능 아이템 크래시 가드가 거는 **자리 표**.
//
// 헤더에 두는 이유는 시험이 보기 위해서다. `specguard.cpp` 는
// `cdtb_tests` 타깃에 없고(윈도 API·케이브 할당이 들어 있다), 더할 경우
// 링크 배치가 바뀌어 `watchpoint_shutdown_is_idempotent` 가 죽은 전례가 있다
// (`TROUBLESHOOTING.md` §1.5 · `STATUS.md` §1.5). 그래서 순수 데이터만 뗀다.
//
// ---------------------------------------------------------------------------
// 이 자리들은 무엇인가 (2026-09-20 실측)
// ---------------------------------------------------------------------------
// 일곱 자리는 흩어진 것이 아니라 **한 가족**이다. 넷을 디스어셈블해 얻은
// 공통 표식:
//
//   - 전역 0x06D69458 을 RIP 상대로 읽는다
//   - TLS gs:[0x58] + 슬롯 0x1EC 를 보고 cmovne
//   - 0x01416C00 을 부른다
//   - 구조체 {+0x00 u32 first, +0x04 u32 상한, +0x08 u64 나누는값, +0x10 u64}
//   - `xor edx, edx` 바로 뒤 `div <나누는값>` -> `cmp eax, 상한`
//
// 크래시가 나는 까닭은 둘 중 하나다:
//   (A) 0 검사가 **아예 없다**            - 0x240937D
//   (B) 검사는 있는데 **0 갈래가 div 로 샌다** - 0xF83F65B · 0xF83F6B4 · 0x24095B4
//
// 이 표식으로 exe 397MB 전량(`.pdata` 연접 병합 함수)을 훑으면 그 꼴이 다섯
// 곳이고, 그중 넷이 아래 실측 자리다(대조군 통과). 다섯째 0x0212CBC8 은
// **게임이 스스로 막아 두었다**(`je` 가 div 를 건너뛴다) - 안 건다.
// 전문: `docs/superpowers/specs/2026-09-20-specguard-div-family.md`.
//
// patch_len = div 바이트 + (div<5 일 때) 5바이트를 채우려고 함께 옮기는 꼬리
// 명령. 꼬리는 위치 독립이어야 한다 - `specguard_tail_is_safe` 가 설치 전에
// 확인하고 아니면 warn 후 건너뛴다.
struct SpecguardSite {
    std::uint64_t rva;
    int patch_len;
};

// div qword ptr [mem] 자리.
//
// 앞 셋은 **2850 값이고 2944 에는 없다.** 바이트열을 파일 397MB 전량에서
// 찾아 0곳임을 확인했다(대조군인 알려진 넷의 바이트열은 전부 나온다):
//     48 F7 B5 F0 00 00 00   div qword [rbp+0xF0]   0곳
//     48 F7 74 24 38         div qword [rsp+0x38]   0곳
//     48 F7 75 C8            div qword [rbp-0x38]   0곳
// 그런데도 **표에서 지우지 않는다.** install 이 opcode 를 보고 거부하므로
// 안전하고, 남아 있어야 kSpecguardSiteTotal 이 실제 자리 수를 세어
// `specguard_unsupported()` 가 참이 되고 지급 창이 경고한다. 지우면
// "전부 설치" 라는 거짓 보고가 된다.
//
//   0xEB1BF4(2760) -> 0xEB26B4(2850) -> 없음(2944)   가방 렌더
//   0xEA874F(2760) -> 0xEA920F(2850) -> 없음(2944)   가방 렌더
//   0x21DA368(2760) -> 0x21DB8D8(2850) -> 없음(2944) 가방 렌더
//   0x234EC7D(2760) -> 0x235021D(2850) -> 0x240937D(2944)  착용
inline constexpr SpecguardSite kSpecguardDivMem[] = {
    {0xEB26B4, 7}, {0xEA920F, 5}, {0x21DB8D8, 7}, {0x240937D, 6}};

// div <reg64> 자리. 나누는 값이 레지스터다. patch_len = div(3) + 위치독립 꼬리.
//
//   0xF064E5B(2760) -> 0xF01981B(2850) -> 0xF83F65B(2944)  `div r8` + `cmp eax,[rdi+4]`
//   **0xF83F6B4(2944)**                                    `div r8` + `test rdx,rdx`
//   0x234EA9B(2760) -> 0x235003B(2850) -> 0x240919B(2944)  `div r14` + `mov ecx,edi`
//   0x234EEB4(2760) -> 0x2350454(2850) -> 0x24095B4(2944)  `div r9` + `mov ecx,r8d`
//
// **0xF83F6B4 는 2026-09-20 에 새로 찾은 자리다.** 0xF83F65B 와 **같은 함수**
// (0xF83F5D0..0xF83F734) 안에 89바이트 뒤에 있고, 같은 `[rdi+8]` 로 나누며
// 같은 (B) 결함을 가진다:
//
//     0x0F83F682  mov  r8, [rdi + 8]
//     0x0F83F68A  test r8, r8
//     0x0F83F68D  jne  0xF83F693      ; r8 != 0 이면 정상 갈래
//     0x0F83F68F  mov  eax, [rdi]     ; r8 == 0 갈래
//     0x0F83F691  jmp  0xF83F6B2      ;   -> 곧장 div 로 뛴다
//     0x0F83F6B2  xor  edx, edx
//     0x0F83F6B4  div  r8             ; r8 == 0 -> 0xC0000094
//
// 앞의 것만 걸면 **크래시를 89바이트 뒤로 옮길 뿐이다** - 가드가 몫 0 을 내면
// `cmp eax,[rdi+4] / jae` 가 상한>0 인 한 안 뛰어 그대로 여기로 흘러온다.
inline constexpr SpecguardSite kSpecguardDivReg[] = {
    {0xF83F65B, 6}, {0xF83F6B4, 6}, {0x240919B, 5}, {0x24095B4, 6}};

inline constexpr int kSpecguardSiteTotal =
    static_cast<int>(std::size(kSpecguardDivMem) +
                     std::size(kSpecguardDivReg));

}  // namespace cdtb::game
