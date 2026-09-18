#pragma once

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// --- 벌금(범죄수치) 값 ---------------------------------------------------
//
// 화면의 "데메니스 왕국 / 벌금 N.NN" 은 `pa::WantedRegionData` 의
// **`+0x30`(u64)** 이고 **2자리 고정소수**다.
//
//   10000 = 100.00   ·   9200 = 92.00   ·   0 = 0
//
// 두 번 독립으로 확인했다(2026-09-18). 10000 -> 5000 을 써 넣으니 화면이
// 따라 줄었고, 그 뒤 범죄를 저지르니 게임이 스스로 9200 으로 올렸다 -
// 게임이 그 칸을 읽고 **쓴다**.
//
// 액수가 0 이 되면 화면 라벨이 "현상 수배" 에서 "벌금" 으로 바뀐다.
// 상태는 이 레코드에 없다 - 100.00/현상수배 때와 0/벌금 때를 바이트로
// 견주면 `+0x30` 말고는 한 바이트도 안 다르다.

// 관측된 상한. 사용자 실측으로 화면이 100.00 에서 더 안 올라간다.
// 넘겨 쓰면 화면과 게임 판정이 어긋날 수 있으므로 자른다.
inline constexpr std::uint64_t kBountyMaxRaw = 10000;

// 화면 값(원 단위) -> 저장 값. 음수는 0 으로 떨어뜨리고 상한에서 자른다.
std::uint64_t bounty_to_raw(double shown);

// 저장 값 -> 화면 값.
double bounty_from_raw(std::uint64_t raw);

// --- 벌금 읽기·쓰기 ------------------------------------------------------
//
// 사슬은 둘뿐이다. 컴포넌트를 한 번 찾아 두면 그 뒤는 공짜다.
//
//   ClientSelfWantedActorComponent  -> +0x30 -> WantedRegionData -> +0x30
//
// RTTI 로 컴포넌트를 찾는 것은 힙 전수 탐색이라 5분씩 걸린다. 그래서
// 시작에 한 번만 하고(인벤토리 컴포넌트와 같은 방식) 주소를 들고 있는다.
//
// **주소는 세이브를 다시 부르면 죽는다.** 읽기·쓰기 전에 `+0x00` vtable 을
// 대조해 죽은 주소를 거른다 - 실제로 한 번 죽은 주소를 읽어 좌표 뭉치를
// 볼 뻔했다. 죽었으면 스스로 다시 찾는다.

// 컴포넌트를 찾는다. 이미 찾았고 아직 살아 있으면 아무것도 안 한다.
bool wanted_component_find(const mem::Rtti& rtti, const mem::Reader& reader);

// 쓸 준비가 됐는가. 화면이 칸을 가리는 데 쓴다.
bool bounty_ready(const mem::Reader& reader);

// 현재 지역의 벌금(저장 값). 못 읽으면 false.
bool bounty_read(const mem::Reader& reader, std::uint64_t* raw_out);

// 현재 지역의 벌금을 쓴다. 0 이면 지우는 것이다.
bool bounty_write(const mem::Reader& reader, std::uint64_t raw);

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

// --- 수배 상태 바꾸기 --------------------------------------------------
//
// 벌금을 0 으로 써도 지도에 지역 항목이 남는다(실측 2026-09-18:
// "데메니스 왕국 / 벌금 / 0"). 액수와 **상태는 다른 것**이고, 상태는
// `WantedRegionData` 안에 없다 - 두 상태를 바이트로 견주면 `+0x30` 말고는
// 한 바이트도 안 다르다. 게임이 그 일을 하는 메시지를 따로 들고 있다.
inline constexpr std::uint16_t kChangeWantedStateId = 2848;

// 머리 5 + 본문 6(u32 핸들 + u8 상태 + u8). 폭은 역직렬화기
// (RVA 0x29A6AA0)가 읽기 함수를 부르기 직전의 `r8d` 에서 읽었다 -
// 4 다음에 1, 다시 1 이다.
inline constexpr std::size_t kChangeWantedStateWireLen = 11;

// `state` 와 `extra` 의 뜻은 **아직 모른다.** 게임의 `WantedState` 열거형이
// 있다는 것과, 역직렬화기가 u8 을 둘 읽는다는 것까지만 확인했다. 그래서
// 화면에서 값을 쓸어 볼 수 있게 그대로 흘려보낸다.
bool build_change_wanted_state_wire(std::uint32_t handle, std::uint8_t state,
                                    std::uint8_t extra, std::uint8_t* out,
                                    std::size_t cap, std::size_t* len_out);

// 플레이어 본인으로 **추정**하는 핸들. actors.h 가 용병의 고용주
// 칸에서 이 값을 실측했다("플레이어 쪽은 0xA0100001"). 수배 메시지의
// 대상으로도 맞는지는 아직 확인 못 했으므로 화면에서 고칠 수 있게
// 둔다 - 박아 두면 틀렸을 때 왜 안 되는지 안 보인다.
inline constexpr std::uint32_t kAssumedPlayerHandle = 0xA0100001u;

// --- 아래는 게임에 붙는 배관이다 (단위 시험 없음) ----------------------
//
// wire 를 만드는 순수 부분만 시험이 덮는다. 여기는 resolve_message /
// request_message 로 넘기는 얇은 접착이고, 이 레포의 다른 요청 경로
// (companion 의 request_hire_target 등)와 같은 관례다 - 검증은
// 게임에서 한다.

// 메시지를 한 번 해석해 둔다. 이미 됐으면 아무것도 안 한다.
bool wanted_resolve(const mem::Rtti& rtti, const mem::Reader& reader);

// 해석이 끝났는가. 화면이 버튼을 가리는 데 쓴다.
bool wanted_ready();

// 해석된 메시지 ID. 0 이면 아직이다. 진단 표시용.
std::uint32_t wanted_clear_message_id();

// 수배 해제를 걸어 둔다. 지급과 같은 대기열(게임 스레드 실행 지점 ·
// SEH · 쿨다운)을 탄다. 세션은 여기서 고른다 - 로드 직후 죽은 세션이
// 뽑히던 일이 있어 pick_drive_session 한 곳으로 모아 둔 규칙이다.
bool request_clear_wanted(const mem::Reader& reader, std::uint32_t handle,
                          std::uint8_t flag);

// 수배 상태를 바꿔 건다. state/extra 의 뜻을 모르므로 그대로 흘려보내고
// 보낸 값을 로그에 남긴다 - 화면에서 쓸어 보며 찾는 것이 목적이다.
bool request_change_wanted_state(const mem::Reader& reader,
                                 std::uint32_t handle, std::uint8_t state,
                                 std::uint8_t extra);

}  // namespace cdtb::game
