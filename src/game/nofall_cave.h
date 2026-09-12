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
//   &&  sourceCtx([rsp+0x38]) 뒤에 가해자가 없다   ->  r9 = 0  (낙하 취소)
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

// 케이브에 잡아 주는 실행 메모리 크기. 실제 조립 결과는 124바이트다.
inline constexpr std::size_t kNofallCaveSize = 256;

struct NofallCave {
    std::vector<std::uint8_t> code;
    bool ok = false;
    const char* why = "";  // ok 가 거짓일 때만 채운다
};

// 케이브 바이트열을 만든다. rel8 분기가 사거리를 벗어나거나 길이가 상한을 넘으면
// ok=false 로 돌려준다 - 그 경우 **설치하면 안 된다**.
//   orig : 사이트에서 복사한 kNofallOrigSize 바이트
//   vars : 32바이트 vars 블록의 주소
//   site : 사이트 주소(꼬리 점프는 site + kNofallOrigSize 로 돌아간다)
NofallCave nofall_build_cave(const std::uint8_t* orig, std::uintptr_t vars,
                             std::uintptr_t site);

}  // namespace cdtb::game
