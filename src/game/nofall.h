#pragma once

#include <cstdint>

#include "game/nofall_cave.h"
#include "mem/reader.h"
#include "mem/rtti.h"

// 낙사 방지(No Fall Damage) - **플레이어 전용**.
//
// 2026-09-12 재설계. 빌드 2.01.00 이 옛 사이트(낙하 누적값 쓰기)를 지웠고 돌아오지
// 않는다 - 2850 이미지에서 옛 AOB `48 89 5F ?? 48 8B 5C 24 ?? 48 89 77 ?? 66 89 6F`
// 은 0곳이고, 결합부 `48 89 77 ?? 66 89 6F` 마저 0곳이다. 그래서 누적기가 아니라
// **데미지/스테이터스 디스패처의 진입점**을 훅한다(참고 모드도 같은 결론을 냈다).
//
// 디스패처 호출 규약(2850 실측, RVA 0x01719850):
//   rcx = 대상 소유자   dx = statusId(0 = Health)   r8 = 시각
//   r9  = 델타(데미지는 음수)      [rsp+0x28] = sourceCtx(인자 5)
// 판별식은 nofall_cave.h 를 보라. 관찰 학습이 없으므로 **첫 낙하부터** 보호된다.
//
// 내 root = [[[char+0x68]+0x20]+0x18] - player.h 의 게이지 체인과 같은 자리다.
// 캐릭터 교체·지역 이동으로 바뀌므로 **캐시하지 않고 매 프레임 갱신**한다.
//
// 가해자 판정(sourceCtx+0x68)은 정적으로 못 박는다. 틀리면 조용한 갓모드가 되므로
// **취소함/통과시킴 카운터 2개**로 눈에 보이게 한다 - 검증의 유일한 수단이다.
// 검증: 「무적」을 끈 채로 ① 낮은 데서 떨어지면 취소함이 오르고, ② 적에게 맞으면
// 통과시킴만 올라야 한다. ②에서 취소함이 오르면 판별식이 틀린 것이니 즉시 끈다.
//
// 출처: CT v5.0 "No Fall Damage"(mul0095/Trinity, MIT) 이식. 옛 구현의 출처였던
//       XeTrinityz/Trinity (MIT) 도 같은 벽에 부딪혀 교체했다.
//
// **모드(주입 DLL)에서만** 부른다. 라이브 코드 패치.

namespace cdtb::game {

// 훅을 설치한다(사이트 AOB 가 유일할 때만). 이미 설치돼 있으면 참. 실패면 거짓.
bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader);
bool nofall_installed();

// AOB 가 이 게임 빌드에서 유일 매칭되지 않아 설치 불가로 판정됨(안전, 무효).
bool nofall_unsupported();

// 내 root 를 다시 계산해 케이브에 건네준다. **렌더 틱(~16ms)에서** 부른다 -
// 분석 통과는 수십 초라 캐릭터 교체 뒤 낡은 root 로 남는 창이 너무 길다.
// 꺼져 있으면 0 을 써서 케이브의 `test rax,rax / je done` 으로 무력화한다.
void nofall_refresh(const mem::Reader& reader);

// 켜기/끄기. 끄면 root 를 지워 케이브가 곧바로 빠져나간다(훅은 남되 무효).
void nofall_set(bool on);
bool nofall_enabled();

// 케이브가 센 두 숫자. 취소함 = 낙하로 보고 0 으로 만든 횟수,
// 통과시킴 = 가해자가 있어 그대로 둔 횟수. 미설치면 둘 다 0.
std::uint64_t nofall_zeroed();
std::uint64_t nofall_let_through();

}  // namespace cdtb::game
