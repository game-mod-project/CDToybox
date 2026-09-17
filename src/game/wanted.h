#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::game {

// 수배(범죄수치) 치트 메시지.
//
// 게임이 개발용 요청 메시지를 그대로 들고 있다. 우리는 새 경로를
// 만들지 않고 그것을 부른다 - 지급·내구도와 같은 배관이다
// (grant.h 의 resolve_message / request_message).
//
// 해석 결과 (2026-09-17, exe 2850):
//   TrocTrClearWantedReq        ID 2646  처리기 RVA 0x2B7F6D0
//   TrocTrSetWantedForDevReq    ID 2328  처리기 RVA 0x2B7F6D0  (같은 처리기)
//   TrocTrChangeWantedStateReq  ID 2848  처리기 RVA 0x2B7F020
//
// wire 형식은 이 게임의 모든 요청 메시지가 공유한다.
//
//   [ID u16][0 u8][본문길이 u16][본문]
//
// 역직렬화기(ClearWantedReq 는 RVA 0x29A7C50)가 이렇게 검사한다.
//
//   movzx ecx, word [페이로드 + 3]    ; 본문 길이
//   movzx eax, word [패킷 + 0x10]     ; 전체 길이
//   sub   rax, 5                      ; 머리를 뺀다
//   cmp   rax, rcx / jne 실패
//
// **길이가 어긋나면 게임이 메시지를 통째로 버린다.** 화면에는
// "아무 일도 안 남" 으로만 보이므로, 이 불변식은 시험으로 건다.

inline constexpr std::uint16_t kClearWantedId = 2646;

// 머리 5 + 본문 5(u32 핸들 + u8 플래그). 본문 폭은 역직렬화기가
// 읽기 함수를 부르기 직전의 `r8d` 에서 읽었다 - 4 다음에 1 이다.
inline constexpr std::size_t kClearWantedWireLen = 10;

// 수배 해제 요청의 wire 를 만든다.
//
// handle 은 대상 액터다. 플레이어 본인은 `0xA0100001` 이다(actors.h
// 의 고용주 핸들 실측과 같은 값). 0 은 "대상 없음" 이라 거절한다 -
// 세션이 아직 안 잡힌 상태를 조용히 넘기면 원인을 못 찾는다.
//
// flag 의 뜻은 아직 모른다. 역직렬화기가 u8 하나를 더 읽는다는 것만
// 확인했다. 게임에서 0 과 1 을 견줘 봐야 한다.
bool build_clear_wanted_wire(std::uint32_t handle, std::uint8_t flag,
                             std::uint8_t* out, std::size_t cap,
                             std::size_t* len_out);

}  // namespace cdtb::game
