#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// 낙사 방지 훅의 **케이브 조립**만 담는다. OS 도 전역도 만지지 않는 순수 계산이라
// 시험할 수 있다(설치·패치는 nofall.cpp).
//
// 케이브는 데미지/스테이터스 디스패처의 **진입점 앞**에 끼어들어, 인자만 보고
// 판정한다(관찰 학습 없음):
//
//   델타(r9) < 0  &&  statusId(dx) == 0(Health)  &&  rcx == 내 root
//   &&  sourceCtx([rsp+0x38]) 뒤에 가해자가 없다   ->  r9 = 0
//
// **이 판별식은 "낙하" 가 아니라 "가해자가 없는 Health 피해" 다.** 낙하가 대표적인
// 경우일 뿐, 출처 없는 환경 피해(익사·지형·지속 피해)도 같이 취소된다. 사용자에게
// 그렇게 알려야 한다 - "낙하만 막는다" 고 적으면 모르는 사이 부분 갓모드가 된다.
//
// 하나라도 어긋나면 아무것도 하지 않고 원본 명령으로 흘려보낸다.
// rcx·rdx·r8 은 건드리지 않는다. rax 와 플래그는 저장·복원하고, r9 만 의도적으로
// 0 으로 만든다.
//
// 케이브 안에서 sourceCtx 가 [rsp+0x38] 인 이유: 사이트가 함수 진입점이라
// 진입 시점의 [rsp+0x28] 이 인자 5 인데, 케이브가 pushfq+push rax 로 0x10 을
// 더 밀기 때문이다(0x28 + 0x10 = 0x38).

namespace cdtb::game {

// vars 블록(32바이트) 레이아웃. 전부 qword.
inline constexpr std::size_t kNofallOwner = 0;        // 내 root (0 이면 꺼짐)
inline constexpr std::size_t kNofallZeroed = 8;       // 취소함 횟수
inline constexpr std::size_t kNofallLetThrough = 16;  // 통과시킴 횟수
inline constexpr std::size_t kNofallVarsSize = 32;

// 사이트에서 복사하는 원본 길이. 디스패처의 첫 명령 `mov [rsp+8], rbx` 가 정확히
// 5바이트라 E9 rel32 가 딱 떨어진다(NOP 패딩이 필요 없다).
inline constexpr std::size_t kNofallOrigSize = 5;

// 케이브에 잡아 주는 실행 메모리 크기. 실제 조립 결과는 126바이트다.
inline constexpr std::size_t kNofallCaveSize = 256;

struct NofallCave {
    std::vector<std::uint8_t> code;
    bool ok = false;
    const char* why = "";  // ok 가 거짓일 때만 채운다

    // 케이브 안의 오프셋. 설치 쪽이 주소를 계산할 때 쓴다(하드코딩 금지).
    std::size_t zero_at = 0;   // r9 를 0 으로 만드는 갈래
    std::size_t done_at = 0;   // rax·플래그를 되돌리고 원본으로 나가는 자리
    std::size_t deref_at = 0;  // `mov rax,[rax+0x68]` - **폴트가 날 수 있는 유일한 명령**
};

// 케이브 바이트열을 만든다. rel8 분기가 사거리를 벗어나거나 길이가 상한을 넘으면
// ok=false 로 돌려준다 - 그 경우 **설치하면 안 된다**.
//   orig : 사이트에서 복사한 kNofallOrigSize 바이트
//   vars : 32바이트 vars 블록의 주소
//   site : 사이트 주소(꼬리 점프는 site + kNofallOrigSize 로 돌아간다)
NofallCave nofall_build_cave(const std::uint8_t* orig, std::uintptr_t vars,
                             std::uintptr_t site);

}  // namespace cdtb::game
