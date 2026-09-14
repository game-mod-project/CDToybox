#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// 낙사 방지 훅의 **케이브 조립**만 담는다. OS 도 전역도 만지지 않는 순수 계산이라
// 시험할 수 있다(설치·패치는 nofall.cpp).
//
// 케이브는 데미지/스테이터스 디스패처의 **진입점 앞**에 끼어들어, 인자만 보고
// 판정한다(관찰 학습 없음). 하는 일은 두 가지다.
//
// 1. **기록** - 플레이어의 생명 피해가 지날 때마다 출처의 정체를 남긴다:
//    sourceCtx, 그 vtable(= 클래스), [sourceCtx+0x68], 델타. 낙하일 때와 맞았을
//    때가 어떻게 다른지 밖에서 눈으로 보려는 것이다.
// 2. **취소** - 규칙(vars[kNofallRule])이 고른 판별식에 걸리면 r9 를 0 으로 만든다.
//    규칙을 **런타임에 바꿀 수 있게** 둔 이유는, 판별식 후보를 게임을 껐다 켜지
//    않고 화면에서 갈아 볼 수 있어야 하기 때문이다(2026-09-12 실측: 가해자 슬롯
//    판별이 이 빌드에서 낙하를 못 걸러 냈다 - 통과시킴만 오르고 취소함은 0).
//
// 공통 관문(셋 다 통과해야 아래로 간다):
//   델타(r9) < 0   &&   statusId(dx) == 0(Health)   &&   rcx == 내 root
//
// rcx·rdx·r8 은 건드리지 않는다. rax 와 플래그는 저장·복원하고, r9 만 의도적으로
// 0 으로 만든다. `mov [abs], rax`(48 A3) 를 써서 두 번째 레지스터가 필요 없다.
//
// 케이브 안에서 sourceCtx 가 [rsp+0x38] 인 이유: 사이트가 함수 진입점이라
// 진입 시점의 [rsp+0x28] 이 인자 5 인데, 케이브가 pushfq+push rax 로 0x10 을
// 더 밀기 때문이다(0x28 + 0x10 = 0x38).

namespace cdtb::game {

// vars 블록(128바이트) 레이아웃. 전부 qword.
//
// **root 가 두 칸인 이유**: 같은 캐릭터가 클라·서버 두 realm 으로 존재하고,
// 디스패처가 rcx 로 넘기는 것은 서버 root 다(2026-09-12 실측). 그런데
// player_char() 는 장비 창의 캐릭터 선택을 따라가 어느 realm 이 잡힐지 보장되지
// 않는다 - 한 칸만 봤을 때 훅이 설치되고 root 도 채워졌는데 카운터가 끝까지
// 0 인 실행이 나왔다. 두 칸을 나란히 두고 `cmp rcx,[rax]` / `cmp rcx,[rax+8]`
// 로 둘 다 맞춘다.
inline constexpr std::size_t kNofallOwner = 0;        // 내 root #1 (0 이면 꺼짐)
inline constexpr std::size_t kNofallOwner2 = 8;       // 내 root #2 (다른 realm)
inline constexpr std::size_t kNofallZeroed = 16;      // 취소함 횟수
inline constexpr std::size_t kNofallLetThrough = 24;  // 통과시킴 횟수
inline constexpr std::size_t kNofallRule = 32;        // 판별 규칙(아래 enum)
inline constexpr std::size_t kNofallLastSrc = 40;     // 마지막 sourceCtx
inline constexpr std::size_t kNofallLastVt = 48;      // 그 객체의 vtable
inline constexpr std::size_t kNofallLastAtk = 56;     // [sourceCtx+0x68]
inline constexpr std::size_t kNofallLastDelta = 64;   // 그때의 델타
inline constexpr std::size_t kNofallSrcLow = 72;      // sourceCtx 가 없던 횟수
inline constexpr std::size_t kNofallSrcBad = 80;      // 말 안 되는 포인터였던 횟수
// 내 캐릭터 객체(char) 두 realm. **낙하 판별의 핵심**이다 - 2026-09-12 실측에서
// 낙하 피해의 sourceCtx 가 내 char 자신이었다(적에게 맞으면 그 적의 char 다).
inline constexpr std::size_t kNofallSelf = 88;        // 내 char #1
inline constexpr std::size_t kNofallSelf2 = 96;       // 내 char #2 (다른 realm)
inline constexpr std::size_t kNofallVarsSize = 128;

// 판별 규칙. vars[kNofallRule] 에 쓰면 **다음 피해부터 바로** 바뀐다.
enum NofallRule : std::uint64_t {
    // **기본값.** 출처(sourceCtx)가 내 캐릭터 자신이면 취소한다. 2026-09-12 실측:
    // 낙하 피해의 sourceCtx 가 내 char(서버, 행 0, 생명 1,500,000)와 정확히
    // 같았다. 적에게 맞으면 출처는 그 적의 char 라 자연히 갈린다.
    kRuleSelfSource = 0,
    // 출처 뒤에 가해자가 없을 때만 취소한다(CT v5.0 의 판별식). **이 빌드에서는
    // 안 먹는다** - char+0x68 은 가해자가 아니라 actor 링크라 늘 채워져 있다.
    kRuleNoAttacker = 1,
    // 출처 자체가 없을 때만 취소한다.
    kRuleNoSource = 2,
    // 내 생명 피해면 무조건 취소한다. **사실상 생명 무적**이라 시험용이다.
    kRuleAlways = 3,
    kRuleMax = 3,
};

// 사이트에서 복사하는 원본 길이. 디스패처의 첫 명령 `mov [rsp+8], rbx` 가 정확히
// 5바이트라 E9 rel32 가 딱 떨어진다(NOP 패딩이 필요 없다).
inline constexpr std::size_t kNofallOrigSize = 5;

// 케이브에 잡아 주는 실행 메모리 크기.
inline constexpr std::size_t kNofallCaveSize = 512;

struct NofallCave {
    std::vector<std::uint8_t> code;
    bool ok = false;
    const char* why = "";  // ok 가 거짓일 때만 채운다

    // 케이브 안의 오프셋. 설치 쪽이 주소를 계산할 때 쓴다(하드코딩 금지).
    std::size_t done_at = 0;   // rax·플래그를 되돌리고 원본으로 나가는 자리
    std::size_t vt_at = 0;     // `mov rax,[rax]` - 폴트가 날 수 있다
    std::size_t deref_at = 0;  // `mov rax,[rax+0x68]` - 폴트가 날 수 있다
};

// 케이브 바이트열을 만든다. 길이가 상한을 넘으면 ok=false 로 돌려준다 - 그 경우
// **설치하면 안 된다**. done 으로 가는 분기는 전부 rel32 라 사거리 걱정이 없다.
//   orig : 사이트에서 복사한 kNofallOrigSize 바이트
//   vars : 128바이트 vars 블록의 주소
//   site : 사이트 주소(꼬리 점프는 site + kNofallOrigSize 로 돌아간다)
//   cave_max : 기본값 말고 다른 값을 넣는 것은 **시험**뿐이다(상한 가드를 태우려고).
NofallCave nofall_build_cave(const std::uint8_t* orig, std::uintptr_t vars,
                             std::uintptr_t site,
                             std::size_t cave_max = kNofallCaveSize);

}  // namespace cdtb::game
