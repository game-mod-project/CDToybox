#pragma once

#include <cstdint>
#include <vector>

namespace cdtb::game {

// 케이브(트램폴린) 바이트를 만드는 **순수 부분**.
//
// 케이브는 손으로 짠 기계어다. 오프셋이 한 칸만 틀리면 게임의 남의 명령
// 한복판으로 뛴다 - 그런 실수는 로그를 안 남기고 죽는다. 그래서 산술이 들어간
// 조각은 여기로 빼고 시험이 바이트를 그대로 못박는다. 헤더 전용이라
// `cdtb_tests` 의 링크 배치를 안 건드린다(`TROUBLESHOOTING.md` §1.5).
//
// ---------------------------------------------------------------------------
// 왜 카운터가 필요한가 (2026-09-20)
// ---------------------------------------------------------------------------
// 가드 둘(`specguard` · `spawnguard`)을 게임에서 확인했을 때, 말할 수 있는
// 선은 *"가드가 걸렸고 죽던 절차가 안 죽었다"* 까지였다. **이번 판에 0/널
// 갈래를 실제로 탔는지는 못 쟀다** - 가드는 호출마다 로그를 안 남기기
// 때문이다. 그러면 다음 갱신에서 자리가 낡아 가드가 **아무것도 안 막고**
// 있어도, 마침 그 판에 크래시 조건이 안 걸리면 똑같이 "정상" 으로 보인다.
//
// 그래서 **막은 갈래에서만** 카운터를 올린다. 0 이면 "이번 판에 한 번도 안
// 막았다" 이고, 그것 자체가 정보다.

// 카운터를 올리는 조각.
//
//   push rax                 50
//   mov  rax, imm64          48 B8 <imm64>
//   lock inc dword ptr [rax] F0 FF 00
//   pop  rax                 58
//
// **rax 를 보존하고 플래그만 건드린다.** specguard 의 0 갈래에서는 바로 뒤에
// `xor eax,eax` 가 와서 플래그를 다시 쓰므로 무해하다 - 그래서 이 조각은
// 반드시 `xor` **앞**에 와야 한다. 뒤에 두면 게임이 읽는 ZF 가 달라진다.
//
// `lock` 을 붙이는 이유는 게임 스레드가 여럿이기 때문이다. 카운터는 우리
// DLL 의 `std::atomic<std::uint32_t>` 이고 그 주소를 imm64 로 박는다
// (RIP 상대로는 ±2GB 를 못 넘어 케이브에서 우리 DLL 에 못 닿을 수 있다).
inline constexpr std::size_t kHitBumpLen = 1 + 10 + 3 + 1;   // 15

inline void emit_hit_bump(std::vector<std::uint8_t>& b, std::uint64_t counter) {
    b.push_back(0x50);                      // push rax
    b.push_back(0x48);                      // mov rax, imm64
    b.push_back(0xB8);
    for (int i = 0; i < 8; ++i) {
        b.push_back(static_cast<std::uint8_t>((counter >> (i * 8)) & 0xFF));
    }
    b.push_back(0xF0);                      // lock
    b.push_back(0xFF);                      // inc dword ptr [rax]
    b.push_back(0x00);
    b.push_back(0x58);                      // pop rax
}

// 소환 가드의 썽크 전체.
//
//   sub  rsp, 0x28            48 83 EC 28     4   그림자 공간 + 정렬
//   mov  r11, <조회>          49 BB <imm64>  10
//   call r11                  41 FF D3        3
//   add  rsp, 0x28            48 83 C4 28     4
//   test rax, rax             48 85 C0        3
//   jne  ret                  75 19           2   <- 널이 아니면 그대로 돌려준다
//   <카운터 올리기>                          15   <- **널 갈래에서만** 센다
//   mov  rax, <빈 레코드>     48 B8 <imm64>  10
//   ret                       C3              1
//
// `jne` 가 건너뛰는 구간에 카운터 올리기가 들어 있어야 한다 - 안 그러면
// 숫자가 "막은 횟수" 가 아니라 "불린 횟수" 가 된다. 시험이 그것을 본다.
inline std::vector<std::uint8_t> build_spawn_thunk(std::uint64_t lookup,
                                                   std::uint64_t empty_record,
                                                   std::uint64_t counter) {
    std::vector<std::uint8_t> b;
    const auto put8 = [&b](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    b.insert(b.end(), {0x48, 0x83, 0xEC, 0x28});   // sub rsp,0x28
    b.insert(b.end(), {0x49, 0xBB});               // mov r11, imm64
    put8(lookup);
    b.insert(b.end(), {0x41, 0xFF, 0xD3});         // call r11
    b.insert(b.end(), {0x48, 0x83, 0xC4, 0x28});   // add rsp,0x28
    b.insert(b.end(), {0x48, 0x85, 0xC0});         // test rax,rax
    const std::size_t jne_at = b.size();
    b.insert(b.end(), {0x75, 0x00});               // jne <ret> (rel8 나중에)
    emit_hit_bump(b, counter);
    b.insert(b.end(), {0x48, 0xB8});               // mov rax, imm64
    put8(empty_record);
    const std::size_t ret_at = b.size();
    b.push_back(0xC3);                             // ret
    // rel8 은 **다음 명령 기준**이다. 손으로 세지 않는다 - 조각 길이가 바뀌면
    // 조용히 남의 자리로 뛴다.
    b[jne_at + 1] = static_cast<std::uint8_t>(ret_at - (jne_at + 2));
    return b;
}

}  // namespace cdtb::game
