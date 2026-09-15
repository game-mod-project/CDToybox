#pragma once

// 스토리 탈것(드래곤·ATAG) 소환 진단.
//
// 종을 바꾼 드래곤·ATAG 를 부르기(2318)로 구동하면 슬롯은 채워지지만 실제
// 스폰이 안 된다. 소환 게이트(0x2ACA250, 소유 타입 레지스트리 조회 → 매칭 시
// 내부에서 스폰 0x2A22DE0 호출)에 로깅 훅을 걸어, 부르기 구동 시:
//   - 게이트가 호출되는가(어느 호출자에서), 검색 키(타입행) 값은 무엇인가
//   - 게이트 결과(*out; 0=무매칭/스폰0반환, !=0=스폰성공)
//   - 스폰 call 자리(0x2ACA441)를 실제로 밟는가(케이브 카운터)
// 를 로그해 사자(소환됨) vs 드래곤(안 됨) 을 대조한다.
//
// 순수 진단이다 - 게임 동작을 바꾸지 않고 원본을 그대로 부른다.
// 조사 문서: docs/superpowers/specs/2026-09-14-dragon-wheel-handoff.md

namespace cdtb {
namespace mem {
class Reader;
}  // namespace mem

namespace game {

// 첫 프레임에 설치(모듈 베이스만 필요). 성공하면 true, 이미 설치됐어도 true.
bool dragondiag_install(const mem::Reader& reader);

}  // namespace game
}  // namespace cdtb
