#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 동반자(탈것·반려동물) 획득 경로의 메시지 캡처 (Phase 1, 진단).
//
// 설계: docs/superpowers/specs/2026-09-05-companion-summon-acquire-design.md
// 길들이기·등록·부르기 때 게임이 실제로 보내는 메시지의 역직렬화
// 함수(서술자 vtable[2])를 후킹해 와이어 본문을 로그로 낸다. 형식을
// 배우는 것이 목적이라 아무것도 바꾸지 않는다.
//
// 와이어: 패킷 +0x10 u16 전체길이 · +0x18 페이로드 포인터.
// 페이로드 머리 5바이트 = [메시지ID u16][u8][본문길이 u16] (역직렬화가
// `word[payload+3] == 전체길이-5` 를 검사한다). 본문은 그 뒤.
//
// 획득 대상 메시지 `TrocTrHireMercenaryToTargetReq`(ID 2338) 본문은
// 정적 분석으로 `u32 대상 액터 핸들 + u8 플래그` 5바이트다.

// 페이로드 머리를 푼다. 길이가 모자라면 false.
bool decode_message_header(const std::uint8_t* payload, std::size_t len,
                           std::uint16_t* id_out, std::uint16_t* body_len_out);

// 2338 본문(머리 뒤 5바이트)을 푼다. 머리의 본문길이가 5가 아니면 false.
bool decode_hire_to_target(const std::uint8_t* payload, std::size_t len,
                           std::uint32_t* handle_out, std::uint8_t* flag_out);

// 16진 덤프. cap 바이트까지만 찍고 넘치면 "…" 를 붙인다.
std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t cap);

// 마지막으로 잡힌 획득 대상. valid 가 false 면 아직 없다.
struct HireTargetCapture {
    bool valid = false;
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
};
HireTargetCapture last_hire_target();

// 잡힌 메시지 수(전체).
int companion_capture_count();

// 역직렬화 훅을 건다. 한 번만. 하나라도 걸리면 true.
bool companion_capture_install(const mem::Rtti& rtti,
                               const mem::Reader& reader);
bool companion_capture_installed();

// 고용 작업 함수 추적 (진단).
//
// `TrocTrHireMercenaryToTargetReq`(2338) 역직렬화는 게이트를 지난 뒤
// **RVA 0x2ADE280** 을 부르고, 그 함수가 낸 u32 코드가 0 이면 성공,
// 아니면 그 코드를 0x3f5 태그로 클라이언트에 오류 알림으로 보낸다
// (역직렬화 0x2960CA0 실측, 2026-09-06).
//
//   rcx = MercenaryClanActorComponent ([[세션액터+0x68]+0x110])
//   rdx = &결과 u32      r8 = &핸들 u32      r9d = 0
//   [rsp+0x20] = 플래그 u8
//
// 이 훅은 인자와 결과 코드를 로그로 낸다. 아무것도 바꾸지 않는다.
// 붙잡기·부적 사용이 왜 거부되는지는 이 코드로만 알 수 있다.
inline constexpr std::uint64_t kHireWorkRva = 0x2ADE280;
bool companion_hire_trace_install(const mem::Reader& reader);
bool companion_hire_trace_installed();
// 마지막 결과 코드(0 이면 성공). 아직 없으면 valid=false.
struct HireWorkResult {
    bool valid = false;
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    std::uint32_t code = 0;
};
HireWorkResult last_hire_work();

// ----------------------------------------------------------------------
// 아이템 사용 구동 (`TrocTrUseItemByItemInfoReq`, ID 2976)
//
// 역직렬화(RVA 0x29373D0)가 읽는 본문 13바이트: u32 A, u32 B, u8 C, u32 D.
// A 는 아이템(ItemInfo) 키로 추정, C 는 0x0D 를 검사한다, B·D 는 미상
// (0 으로 시작). 컨테이너 핸들을 쓰지 않아 소켓을 막았던 핸들 월드
// 문제가 없다. 지급한 부적을 이것으로 사용시켜 등록 경로를 캡처한다.
inline constexpr std::uint16_t kUseItemByInfoId = 2976;
inline constexpr std::uint8_t kUseItemByInfoKindC = 0x0D;
inline constexpr std::size_t kUseItemWireLen = 5 + 13;

// 머리 5바이트 + 본문 13바이트를 조립한다. out 은 kUseItemWireLen 이상.
bool build_use_item_wire(std::uint32_t item_key, std::uint32_t b, std::uint8_t c,
                         std::uint32_t d, std::uint8_t* out, std::size_t cap,
                         std::size_t* len_out);

// 2976 서술자·역직렬화를 해석해 둔다(한 번). 성공하면 true.
bool companion_use_item_resolve(const mem::Rtti& rtti, const mem::Reader& reader);
bool companion_use_item_ready();

// 등록 뒤 소환을 마무리한다(TrocTrCompleteCalculateSummonAfterRegistReq).
//
// 획득(2338)만으로는 목록에 들어가기만 하고 소환이 안 된다 - 실측
// 2026-09-06: 같은 날 추가한 진돗개·참새가 목록에는 있는데 소환에
// 아무 반응이 없었다. 부적 경로에는 등록 뒤 이 단계가 있고
// (SummonMercenaryAfterRegistAck 가 그 응답이다) 우리는 그것을
// 건너뛰고 있었다.
//
// 본문은 u64 용병번호 + float3 좌표다(deser RVA 0x2965E40 에서
// 8바이트 그리고 12바이트를 읽는다). 좌표는 카메라 초점을 쓴다 -
// 바닥 스폰이 쓰는 것과 같은 자리다.
//
// 주의: 작업 함수가 0 이 아닌 코드를 돌려주면 게임이 오류 1013 을
// 만들어 로그아웃한다. 번호가 유효할 때만 부를 것.
inline constexpr std::uint16_t kCompleteSummonId = 2962;
// 좌표를 어디서 얻을지는 밖에서 준다. 카메라 코드는 시험 대상에
// 링크되지 않으므로 이쪽이 그것을 직접 부르면 안 된다.
using PositionFn = bool (*)(float out[3]);
void companion_set_position_source(PositionFn fn);

bool complete_summon_ready();
bool build_complete_summon_wire(std::uint64_t merc_no, const float pos[3],
                                std::uint8_t* out, std::size_t cap,
                                std::size_t* len_out);
bool request_complete_summon(std::uintptr_t session, std::uint64_t merc_no,
                             const float pos[3]);

// 세션의 게임 스레드에서 2976 을 구동한다(grant 의 대기열 재사용).
bool request_use_item(std::uintptr_t session, std::uint32_t item_key,
                      std::uint32_t b = 0, std::uint8_t c = kUseItemByInfoKindC,
                      std::uint32_t d = 0);

// 실험용으로 미리 해석해 두는 메시지들. 이름·ID 는 companion.cpp 의 표.
// 한 번만 해석하고, 명령 파일의 `msg` 가 ID 로 골라 쓴다.
bool companion_resolve_messages(const mem::Rtti& rtti, const mem::Reader& reader);
// 해석된 메시지 수.
int companion_message_count();
// 16진 문자열("A0 0B 00 ..." 또는 "a00b00...")을 바이트로. 실패하면 false.
bool parse_hex_bytes(const std::string& text, std::uint8_t* out, std::size_t cap,
                     std::size_t* len_out);

// ----------------------------------------------------------------------
// 획득 (`TrocTrHireMercenaryToTargetReq`, ID 2338)
//
// 월드에 있는 동반자 타입 액터를 그 자리에서 동반자로 등록한다.
// **부적 없이 되는 것이 실측으로 확인됐다**(2026-09-06): 진돗개·고양이·
// 야생 참새는 반려동물 탭에, 사육 말은 이름 붙은 탑승물 탭에 들어갔다.
// 거부되는 대상도 있다(유니크 전설마·스토리 동료·일부 야생). 거부는
// 조용히 코드만 남기고 게임 상태를 바꾸지 않는다.
//
// 대상 핸들은 game/actors 의 근처 목록에서 얻는다.
inline constexpr std::uint16_t kHireToTargetId = 2338;
inline constexpr std::size_t kHireWireLen = 5 + 5;

// 머리 5바이트 + 본문 5바이트(u32 핸들, u8 플래그)를 조립한다.
bool build_hire_wire(std::uint32_t handle, std::uint8_t flag, std::uint8_t* out,
                     std::size_t cap, std::size_t* len_out);

// 2338 을 게임 스레드에서 구동한다. 대기열이 차 있으면 false.
bool request_hire_target(std::uintptr_t session, std::uint32_t handle,
                         std::uint8_t flag = 0);
// 2338 이 해석돼 있는가.
bool hire_target_ready();
// 그란트 패널과 같은 규칙으로 서버 세션을 고른다. 없으면 0.
std::uintptr_t companion_pick_session();

// ----------------------------------------------------------------------
// 명령 파일 (DLL 옆 cdtoybox_cmd.txt)
//
// 오버레이를 누르지 않고도 밖에서 실험을 걸 수 있게 한다. 한 줄에
// 명령 하나. 읽으면 파일을 지운다.
//   give <아이템키> [개수]
//   useitem <아이템키> [B] [C] [D]      (C·D 는 10진 또는 0x 16진)
//   msg <16진 와이어>                   미리 해석한 메시지를 그대로 구동
//                                      (2676·2976·2338·2454·2386·2894)
// 세션은 그란트 패널과 같은 규칙(서버 세션 중 가장 유력한 것)으로 고른다.
void companion_command_start(const mem::Reader& reader);
void companion_command_stop();
// 명령 한 줄을 처리한다(시험용으로 공개). 실행했으면 true.
bool companion_run_command(const std::string& line, std::string* reply);

}  // namespace cdtb::game
