#pragma once

#include "mem/reader.h"

namespace cdtb::game {

// 동반자 소환 크래시 가드 (게임의 빠진 널 검사를 메운다).
//
// 실측 2026-09-09 (crash 파일 + 정적 디스어셈블):
// 종을 바꾼 동반자를 소환하면 `모듈+0x2AD520F` 의
// `cmp qword ptr [rax+0x28], -1` 에서 주소 0x28 을 읽다 죽는다.
// rax 는 바로 앞 `call 0x208F5C0`(용병번호 -> 레코드 해시 조회)의
// 반환값이고, 그 조회가 못 찾으면 0 을 낸다.
//
// 넘어가는 번호는 호출자(0x2ABD850)에서 이렇게 정해진다.
//
//   0x2ABD8C0  mov rdi, -1                 기본값
//   0x2ABD98A  call 0x2095600              종류로 등록된 동반자를 찾는다
//   0x2ABD9A2  cmovne rdi, [찾은 것 +0x28] 못 찾으면 -1 그대로
//   0x2ABDBB9  call 0x2AD4F70(.., rdi, ..) -1 을 그대로 넘긴다
//
// 그리고 0x2095600 은 못 찾으면 전역 **빈 레코드**(0x6BB3F70)를 낸다.
// 우리가 명부 레코드의 종(+0x20)을 제자리에서 바꾸면 게임이 종류별로
// 들고 있는 색인이 갱신되지 않아 이 조회가 빈다.
//
// 게임 자신도 같은 조회를 두 곳에서 쓰는데, **한 곳에는 널 검사가 있다**:
//
//   0x2ABD8EB  call 0x208F5C0
//   0x2ABD8F0  lea  rbx, [0x6BB3F70]   빈 레코드
//   0x2ABD8F7  test rax, rax
//   0x2ABD8FA  cmovne rbx, rax         널이면 빈 레코드를 쓴다
//
// 이 가드는 죽는 쪽 호출 하나에 똑같은 처리를 넣는다 - 조회가 널이면
// 빈 레코드를 돌려준다. 그러면 이어지는 `cmp [rax+0x28], -1` 이 맞아
// 정상 분기로 빠진다. 게임이 다른 자리에서 이미 하는 것과 같은 동작이라
// 없는 행동을 지어내지 않는다.
//
// 호출 지점 하나만 바꾼다(그 자리의 `call rel32` 를 우리 썽크로 돌린다).
// 조회 함수 자체는 건드리지 않으므로 다른 호출자는 그대로다.
//
// 전역 빈 레코드가 정말 초기화됐는지(+0x28 == -1, +0x20 == 0xFFFF)
// 확인한 뒤에만 설치한다. 아직이면 false 를 내고, 부르는 쪽이 다음
// 프레임에 다시 시도한다.
bool spawnguard_install(const mem::Reader& reader);
bool spawnguard_installed();
bool spawnguard_unsupported();   // 바이트가 달라 이 빌드에선 못 걺

}  // namespace cdtb::game
