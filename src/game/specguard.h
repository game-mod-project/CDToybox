#pragma once

#include "mem/reader.h"

namespace cdtb::game {

// 특수기능 아이템 크래시 가드 (divide-by-zero 방지 인라인 훅).
//
// 특수기능(+0x58)이 탑재된 아이템을 치트로 지급하면, 인벤토리 UI 렌더
// 함수가 그 특수기능 데이터로 나눗셈을 하는데 분모가 0이라 정수 0으로
// 나누기(0xC0000094)로 게임이 죽는다. 실측(크래시 덤프 + 디스어셈블,
// 2026-09-07): CrimsonDesert.exe RVA 0xEB1BF4 `div qword ptr [rbp+0xf0]`,
// 분모 [rbp+0xf0]=0.
//
// 이 훅은 그 div 앞에서 **분모가 0이면 나눗셈을 건너뛰고 몫=0**으로
// 두고 이어간다. 정상 아이템은 분모가 0이 아니라 원래대로 나눈다 -
// 즉 정상 동작은 그대로, 크래시만 없앤다. AOB 가 유일할 때만 패치한다.
// rtti 불필요(모듈 베이스+확정 RVA). 렌더 루프(overlay on_frame)에서 매
// 프레임 불러 첫 프레임에 즉시 설치한다 - 분석 루프의 늦은 지점에서 설치하면
// 그 전에 지급/가방을 열어 크래시가 났다(실측 2026-09-07).
// 여러 스레드(렌더 루프·분석 루프)가 불러도 한 번만 설치한다. 설치가 끝나면 true.
bool specguard_install(const mem::Reader& reader);
bool specguard_installed();
// 한 곳이라도 못 걸었다(전부 못 건 것 포함). 그 자리에서는 특수기능 아이템이
// 원래대로 죽을 수 있으니 지급 창이 경고한다(Codex 지적 2026-09-11: 전에는
// 0곳일 때만 알렸다).
bool specguard_unsupported();

}  // namespace cdtb::game
