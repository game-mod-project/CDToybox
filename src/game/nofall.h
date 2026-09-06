#pragma once

#include <cstdint>

#include "mem/reader.h"
#include "mem/rtti.h"

// 낙사 방지(No Fall Damage) - **플레이어 전용**. 낙하 누적값 쓰기 명령을 인라인
// 훅으로 가로채, 떨어지는 개체(rdi)가 플레이어일 때만 누적을 건너뛴다.
//
// 왜 NOP 이 아니라 훅인가: 그 명령은 **떨어지는 모든 개체**에 돌고, 같은 쓰기가
// 데미지 누적도 먹여서, 그냥 지우면 적이 데미지를 안 입는다(참고 모드 실측).
//
// 출처: XeTrinityz/Trinity (MIT), Nexus 3209 CT "No Fall Damage".
// AOB: 48 89 5F ?? 48 8B 5C 24 ?? 48 89 77 ?? 66 89 6F  (변위는 런타임 복사)
//
// **모드(주입 DLL)에서만** 부른다. 라이브 코드 패치.

namespace cdtb::game {

// 훅을 설치한다(사이트 AOB 가 유일할 때만). 이미 설치돼 있으면 참. 실패면 거짓.
bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader);
bool nofall_installed();

// AOB 가 이 게임 빌드에서 유일 매칭되지 않아 설치 불가로 판정됨(안전, 무효).
bool nofall_unsupported();

// 낙하하는 개체(케이브가 기록)가 플레이어인지 판정해 고정한다. 분석 스레드에서
// 주기적으로 부른다. 플레이어가 한 번 떨어져야 학습된다(첫 낙하는 아플 수 있음).
void nofall_identify(const mem::Reader& reader);

// 켜기/끄기. 끄면 학습된 플레이어를 잊어 누적이 정상 동작한다(훅은 남되 무효).
void nofall_set(bool on);
bool nofall_enabled();

}  // namespace cdtb::game
