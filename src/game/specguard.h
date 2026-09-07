#pragma once

#include "mem/reader.h"
#include "mem/rtti.h"

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
bool specguard_install(const mem::Rtti& rtti, const mem::Reader& reader);
bool specguard_installed();
bool specguard_unsupported();   // AOB 미매칭 등으로 이 빌드에서 못 걺

}  // namespace cdtb::game
