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
//
// 이름은 "캡처" 지만 조사용이 아니라 **생산 배관**이다. det_hire_response 가 채우는
// g_acks 를 clan.cpp 의 획득 뒤처리(tick_hire_cleanup -> resolve_spawn_flag, 게임 메모리
// 쓰기)가 소비하고, det_catch_summon 이 채우는 g_last_catch 를 request_catch 가 쓴다.
// 지우면 "획득 직후 소환 먹통" 이 조용히 되살아난다(2026-09-10 죽은 코드 정리 때 확인).

// 페이로드 머리를 푼다. 길이가 모자라면 false.
bool decode_message_header(const std::uint8_t* payload, std::size_t len,
                           std::uint16_t* id_out, std::uint16_t* body_len_out);

// 2338 본문(머리 뒤 5바이트)을 푼다. 머리의 본문길이가 5가 아니면 false.
bool decode_hire_to_target(const std::uint8_t* payload, std::size_t len,
                           std::uint32_t* handle_out, std::uint8_t* flag_out);

// 16진 덤프. cap 바이트까지만 찍고 넘치면 "…" 를 붙인다.
std::string hex_bytes(const std::uint8_t* p, std::size_t n, std::size_t cap);

// 역직렬화 훅을 건다. 한 번만. 하나라도 걸리면 true.
bool companion_capture_install(const mem::Rtti& rtti,
                               const mem::Reader& reader);

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

// 소환 작업 함수. 2894 처리기(RVA 0x29621E0)가 관문을 통과한 뒤
// 이것을 부른다 - 정상 소환에서 실제로 일하는 자리다.
//
//   f(문객체, u32* 결과, u64 용병번호, float* 좌표)
//
// 획득한 개체가 지역 재적재 전에는 소환이 안 되는데, 그때 클라이언트가
// 2894 를 아예 안 보낸다(실측 2026-09-06). 그래서 이 함수가 불리기는
// 하는지, 불린다면 어떤 코드를 돌려주는지를 봐야 어디서 갈리는지
// 알 수 있다. 읽고 찍기만 한다.
inline constexpr std::uint64_t kSpawnWorkRva = 0x2ACF600;
// 마지막 소환 결과. 코드 0 이 성공이다.
//
// 게임에는 소환 쿨타임이 있다(TrocTrCallMercenaryCoolTime* 계열).
// 그 구간에는 어떤 개체를 넣어도 0x533C0A53 으로 거부된다 - 실측
// 2026-09-06: 방금 소환한 개체도, 전에 잘 되던 개체도 똑같이
// 거부됐고, 몇 분 뒤에는 여섯 번 연속 전부 성공했다.
//
// 이것을 몰라서 "획득한 개체는 재적재가 필요하다"고 오진했다.
// 화면에 코드를 띄워 다음에는 바로 알아보게 한다.
//
// roster_panel 이 매 프레임 읽어 소환 거부 문구를 띄운다 - 조사용 아님, 지우지 말 것.
struct SpawnWorkResult {
    bool valid = false;
    std::uint64_t merc_no = 0;
    std::uint32_t code = 0;
};
SpawnWorkResult last_spawn_work();

// 소환 쿨타임으로 보이는 거부 코드. 실측값이라 다른 이유도 이 코드를
// 쓸 수 있다 - 화면에는 "쿨타임으로 보임" 정도로만 적는다.
inline constexpr std::uint32_t kSpawnCooldownCode = 0x533C0A53;

// 소환 작업 결과를 last_spawn_work 로 넘기는 생산 훅. 진단용이 아니다.
bool companion_spawn_trace_install(const mem::Reader& reader);
bool companion_hire_trace_install(const mem::Reader& reader);
// 마지막 결과 코드(0 이면 성공). 아직 없으면 valid=false.
struct HireWorkResult {
    bool valid = false;
    std::uint32_t handle = 0;
    std::uint8_t flag = 0;
    std::uint32_t code = 0;
};
HireWorkResult last_hire_work();

// 획득 응답(`TrocTrResponseHiredMercenaryToTargetAck`, 2107) 에서 꾹낸
// 새 동반자 번호. 본문 20바이트 = {u32 사용자, u32 대상, u32 사용자,
// u64 MercenaryNo} 이고 번호는 본문 +12 에 있다(실측 2026-09-09).
//
// 이것이 필요한 이유: 2338 획득은 명부 레코드의 +0x50 에 **그 순간의
// 야생 액터 핸들**을 박아 둘다. 그 액터는 곳 사라지는데 값은 남아
// 게임이 "이미 소환됨" 으로 오판하고, 그러면 그 개체는 소환도
// 해제도 안 된다. 지금까지 지역 이동·세이브 로드로만 풀리던 그것이다.
inline constexpr std::uint16_t kHireAckId = 2107;

struct HireAck {
    bool valid = false;
    std::uint64_t merc_no = 0;
    unsigned long long at_ms = 0;
    bool handled = false;   // 뒤처리를 끝냈나
};
// 응답을 여러 개 기억한다. 칸이 하나면 연속 획득에서 앞의 것이
// 덮어쓰여 뒤처리가 전부 누락된다 - 실측 2026-09-09: 8번 연속
// 획득에서 뒤처리 로그가 한 줄도 없었다.
inline constexpr int kHireAckSlots = 16;

// 아직 뒤처리를 안 한 응답을 모아 낸다. 반환값은 채운 개수.
int pending_hire_acks(HireAck* out, int cap);
// 그 번호의 응답을 끝났다고 표시한다.
void mark_hire_ack_handled(std::uint64_t merc_no);

// ----------------------------------------------------------------------
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
// 알에서 깬 개체 거두기 (`TrocTrCatchBySummonReq`, ID 2386)
//
// **이름과 달리 야생 개체를 잡는 경로가 아니다.** 처음에 그렇게 읽고
// 임의의 야생 동물에게 쏴 봤지만 아무 일도 일어나지 않았다. 사용자가
// 알려 준 실제 절차는 이렇다(2026-09-07):
//
//   와이번 알 -> 둥지에 올리기 -> 5분 대기 -> 부화 -> 획득
//
// 그래서 캡처된 두 메시지의 뜻은 이렇다.
//
//   2676 아이템 사용  = 알을 둥지에 올린다 (본문의 매번 다른 u64 가 알)
//   2386 이 메시지    = 부화한 개체를 거둔다
//
// 와이어는 머리 5 + 본문 8 바이트다.
//
//   52 09 00 08 00 | 01 00 10 A0 | 0D 37 10 B0
//
//   첫째 u32 = 0xA0100001 - 네 표본이 전부 같다. 둥지로 보인다.
//              액터 매니저가 아닌 별도 매니저에서 조회된다(0x2B7CE20).
//   둘째 u32 = 부화체 액터 핸들 (0xB010 = 일반 액터)
//
// 처리기(역직렬화 0x28A7860 -> 작업 0x2B7CDF0)는 첫째 핸들이 그 매니저에
// 없으면 오류 코드만 쓰고 끝낸다. 우리가 쏜 것이 조용히 아무 일도 못 한
// 이유다 - 둥지도 부화체도 없었다.
//
// 그러니 이 메시지만으로는 새 동반자를 얻을 수 없다. 알을 지급해
// (render/roster_panel 의 동반자 아이템 탭) 게임의 절차를 그대로
// 밟는 것이 실제로 되는 길이다. 조립·해석은 표본이 있으니 남겨 둔다.
// ----------------------------------------------------------------------
// 소지품으로 고용 (`TrocTrHireMercenaryFromInventoryReq`, ID 2454)
//
// **종을 골라 등록하는 데는 쓸 수 없다.** 실측으로 끝까지 확인했다
// (2026-09-07). 인자가 종을 가리키지 않는다.
//
//   [ID 2454][00][본문길이 4][u16 A][u16 B]
//
//   A = 컨테이너 종류 (1..21). 등록 작업이 표(0x8752A40)로 A -> A-1 로
//       옮긴다. 그 표는 21행이고 키 1..21, 값 0..20 이 전부다 -
//       실제로 열어서 읽었다. 범위 밖이면 코드 0x73353994.
//   B = **그 컨테이너 안의 슬롯 번호.** 조회(0x2078A70)가
//       [컨테이너] + B * 0xC8 로 슬롯을 집는다. 슬롯 수를 넘거나 빈
//       슬롯이면 코드 0x06306EB0.
//
// 즉 **종은 그 슬롯에 든 아이템이 정한다.** 동행의 부적이 6종뿐이니
// 이 길의 한계도 6종이다. 원하는 종을 임의로 올릴 수는 없다.
//
// 남겨 두는 이유: 부적을 인벤토리에서 손으로 쓰지 않고 구동으로
// 쓸 수 있고, 거부 코드를 읽는 훅이 붙어 있어 다른 조사에 쓸모가 있다.
//
// 처리기 사슬:
//   역직렬화 0x2965510 -> 작업 0x2AD1FC0
//   rcx = [[세션액터+0x68]+0x110]  MercenaryClanActorComponent
//   rdx = &결과 u32 (0 이면 성공)   r8w = A   r9w = B
inline constexpr std::uint16_t kHireFromInvId = 2454;
inline constexpr std::size_t kHireInvWireLen = 5 + 4;

// 머리 5바이트 + 본문 4바이트(u16 A, u16 B)를 조립한다.
bool build_hire_inv_wire(std::uint16_t a, std::uint16_t b, std::uint8_t* out,
                         std::size_t cap, std::size_t* len_out);
// 본문 4바이트를 읽는다. 길이나 ID 가 다르면 false.
bool decode_hire_inv(const std::uint8_t* payload, std::size_t len,
                     std::uint16_t* a_out, std::uint16_t* b_out);
// 2454 를 게임 스레드에서 구동한다.
bool request_hire_from_inventory(std::uintptr_t session, std::uint16_t a,
                                 std::uint16_t b);
bool hire_from_inventory_ready();

inline constexpr std::uint16_t kCatchBySummonId = 2386;
inline constexpr std::size_t kCatchWireLen = 5 + 8;
// 표본 네 개가 전부 이 값이었다. 세션마다 달라질 수 있으니 캡처에서
// 본 값이 있으면 그것을 먼저 쓴다.
inline constexpr std::uint32_t kCatchSelfDefault = 0xA0100001;

// 게임이 보낸 붙잡기에서 읽어 둔 것. 아직 없으면 valid=false.
struct CatchCapture {
    bool valid = false;
    std::uint32_t self = 0;
    std::uint32_t target = 0;
};
CatchCapture last_catch();

// 본문 8바이트(u32 잡는쪽, u32 대상)를 읽는다. 길이가 다르면 false.
bool decode_catch(const std::uint8_t* payload, std::size_t len,
                  std::uint32_t* self_out, std::uint32_t* target_out);
// 머리 5바이트 + 본문 8바이트를 조립한다.
bool build_catch_wire(std::uint32_t self, std::uint32_t target,
                      std::uint8_t* out, std::size_t cap, std::size_t* len_out);
// 2386 을 게임 스레드에서 구동한다. self 가 0 이면 캡처에서 본 값,
// 그것도 없으면 kCatchSelfDefault 를 쓴다.
bool request_catch(std::uintptr_t session, std::uint32_t target,
                   std::uint32_t self = 0);
// 2386 이 해석돼 있는가.
bool catch_ready();

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
