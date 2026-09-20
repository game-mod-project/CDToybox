#pragma once

// @build 1.0.0.2944  메시지 ID 둘 재도출 + 런타임 해석값을 쓰도록 고쳤다
// @build 1.0.0.2850  **런타임 구조 오프셋(+0x30 사슬)은 아직 2850 실측이다**
//   근거: specs/2026-09-18-game-update-2944.md §2 §10
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
//   <realm>WantedActorComponent  -> +0x30 -> WantedRegionData -> +0x30
//
// RTTI 로 컴포넌트를 찾는 것은 힙 전수 탐색이라 5분씩 걸린다. 그래서
// 시작에 한 번만 하고(인벤토리 컴포넌트와 같은 방식) 주소를 들고 있는다.
// realm 이 셋이지만 **힙은 한 번만** 훑는다 - `find_objects_of` 가 이름
// 여럿을 한 통과로 받는다. 클래스마다 따로 훑으면 3배가 된다.
//
// --- 무엇이 진짜 컴포넌트인가 (2026-09-20 정정) ------------------------
//
// 예전 기준은 "`+0x30` 이 `WantedRegionData` 를 가리킨다" 였다. 그건
// **범죄 기록이 있을 때만** 참이라, 죄가 없으면 컴포넌트를 영영 못 찾았다 -
// 2026-09-20 09:57~09:58 에 3회 연속 실패하고 그만둔 것이 그것이다.
//
// 진짜와 가짜를 가르는 것은 `+0x08` 이다. 가짜는 **등록표 행**이고
// `{vtable, 클래스번호}` 가 0x10 마다 반복된다.
//
//   클라 진짜  +0x08 -> ClientChildOnlyInGameActor
//   서버 진짜  +0x08 -> ServerChildOnlyInGameActor
//   등록표 행  +0x08 =  0x565 / 0x37E2   (포인터가 아니다)
//
// 그래서 **주인이 액터인가**로 가른다. 주소 범위로 추측하지 않는다.
//
// **주소는 세이브를 다시 부르면 죽는다.** 읽기·쓰기 전에 `+0x00` vtable 을
// 대조해 죽은 주소를 거른다 - 실제로 한 번 죽은 주소를 읽어 좌표 뭉치를
// 볼 뻔했다. 죽었으면 스스로 다시 찾는다.

// 컴포넌트를 찾는다. 이미 찾았고 아직 살아 있으면 아무것도 안 한다.
bool wanted_component_find(const mem::Rtti& rtti, const mem::Reader& reader);

// 쓸 준비가 됐는가. 화면이 칸을 가리는 데 쓴다.
bool bounty_ready(const mem::Reader& reader);

// **컴포넌트를 잡았는가.** `bounty_ready` 와 갈라 둔다 - 그것은 "잡았고 + 지금
// 지역에 범죄 기록이 있다" 라서, 둘을 뭉치면 화면이 "월드에 들어가면 저절로
// 잡습니다" 를 영원히 띄운다(사용자 보고 2026-09-18: 월드 안인데 안 바뀐다).
// 실제로는 컴포넌트를 잡고도 `+0x30` 이 널일 수 있다 - 범죄 기록이 없는 상태다
// (실측 2026-09-18: 힙에 살아 있는 WantedRegionData 가 0개였다).
bool wanted_component_ready();

// **더 안 찾고 있는가.** 못 찾으면 예전에는 영원히 다시 훑었다 - 한 번이
// ~45초짜리 힙 전수라 15초를 쉬어도 실질은 쉬지 않고 도는 것이었고, 로그가
// 경고로 도배되고 배경이 계속 무거웠다(실측 2026-09-18). 지금은 월드 안에서
// 몇 번 해 보고 그만둔다. 화면이 이 상태를 보이고 눌러서 다시 시킨다.
bool wanted_find_gave_up();

// 다시 찾게 한다(화면 버튼). 세는 수를 0 으로 되돌릴 뿐이라 값싸다.
void wanted_find_rearm();

// --- 구역 기록은 **여럿**이다 (2026-09-19 정정) ------------------------
//
// `ClientSelfWantedActorComponent +0x30` 은 단일 포인터가 아니라
// **`{데이터, 크기, 용량}` 벡터**다. 원소는 **0x40 바이트**이고
// `+0x28` 이 구역 키, `+0x30` 이 벌금이다.
//
// 포인터로 읽으면 **첫 원소만** 보고 쓴다 - 벡터의 데이터 포인터가 곧
// 원소 [0] 의 주소라 vtable 대조까지 통과해서, 틀린 줄 모르고 오래 썼다.
// 증상은 "벌금을 0 으로 내렸는데 수배가 안 풀린다" 였다: 데메니스를 0 으로
// 만드는 동안 에르난드의 81.00 은 한 번도 안 건드려졌다(실측 2026-09-19,
// 화면과 바이트가 맞았다).
//
// 영토는 다섯이다 - 에르난드 공국 · 페일론 연합국 · 데메니스 왕국 ·
// 델레시아 공화국 · 붉은사막. 그래서 기록도 다섯까지 난다.
//
// **게임 창은 지금 있는 구역 하나만 보여 준다.** 우리 창이 첫 칸만 보이면
// 둘이 어긋나 "고쳤는데 안 고쳐진" 것처럼 보인다 - 전부 낸다.
struct WantedRegion {
    // 원소 주소. **들고 있지 말 것** - 구역이 하나 늘면 벡터가 재할당돼
    // 이 주소가 죽는다(실측: 0x26387991EC0 -> 0x262F33FAC00).
    std::uintptr_t addr = 0;
    std::uint32_t key = 0;    // +0x28 구역 키 (1000138 데메니스 · 1000131 에르난드)
    std::uint64_t raw = 0;    // +0x30 벌금 raw (2자리 고정소수)
};

// 벡터 원소 크기와, 머리를 믿어도 되는 선.
inline constexpr std::size_t kWantedRegionStride = 0x40;
inline constexpr std::uint32_t kWantedRegionCapMax = 64;

// 벡터 머리가 말이 되는가. 되면 읽을 원소 수, 아니면 **0**.
//
// 힙이 요동칠 때 머리만 읽으면 쓰레기가 나온다. 말이 안 되면 아무것도 안
// 만지는 쪽이 안전하다 - 이 값이 쓰기 자리를 정한다. 순수 함수라 시험한다.
std::size_t wanted_region_count(std::uint64_t data, std::uint32_t size,
                                std::uint32_t cap);

// --- 값은 **두 벌**이다 (2026-09-20 실측) -------------------------------
//
// 이 게임은 싱글인데 안이 MMO 프레임워크라 클라와 서버가 한 프로세스에
// 산다. 수배 컴포넌트도 realm 마다 따로 살고, **같은 구역 키의 레코드가
// 값이 다르다.**
//
//   서버 0x47776405440  +0x28 = 1000131  +0x30 = 10000  (100.00)
//   클라 0x477E87F4300  +0x28 = 1000131  +0x30 =  3000  ( 30.00)
//
// 같은 `WantedRegionData` 클래스이고 `+0x1B`·`+0x1E`·`+0x2E` 도 갈린다.
// 그동안 우리는 **클라 한 벌만** 썼다 - "벌금을 0 으로 내려도 수배가 안
// 풀린다" 의 한 축이 이것이다. `setspecies` 가 이미 클라+서버로 쓴다.
enum class WantedRealm { kClient = 0, kServer = 1, kCommon = 2 };
inline constexpr int kWantedRealmCount = 3;

// 로그·화면에 쓰는 짧은 이름. realm 밖의 값이면 "?".
const char* wanted_realm_name(WantedRealm realm);

// 화면 한 줄 = 한 구역 키의 realm 별 값.
struct WantedRegionRow {
    std::uint32_t key = 0;
    std::uint64_t raw[kWantedRealmCount]{};
    bool have[kWantedRealmCount]{};

    // realm 끼리 값이 어긋나 있는가. **가진 realm 끼리만** 견준다.
    // 화면이 이걸 짚어 줘야 오늘 같은 일을 다시 안 겪는다.
    bool disagrees() const;
};

// 키별로 realm 을 합친다. 키 오름차순이다.
//
// **순수 함수라 시험한다.** 오늘 서버 값을 못 본 것이 이 합침이 없어서였다.
std::vector<WantedRegionRow> merge_region_rows(
    const std::vector<WantedRegion> (&per_realm)[kWantedRealmCount]);

// 한 realm 의 구역 기록을 **전부** 읽는다. 하나도 못 읽으면 false.
bool wanted_regions_of(const mem::Reader& reader, WantedRealm realm,
                       std::vector<WantedRegion>* out);

// 살아 있는 realm 을 전부 읽어 키별로 합친다. 한 줄도 없으면 false.
bool wanted_region_rows(const mem::Reader& reader,
                        std::vector<WantedRegionRow>* out);

// 그 구역의 벌금을 **살아 있는 모든 realm 에** 쓴다. 쓴 realm 수를 낸다 -
// 0 이면 아무 데도 못 썼다는 뜻이다.
//
// **쓸 때마다 벡터를 다시 읽어** 자리를 다시 찾는다 - 주소를 들고 있다
// 쓰면 재할당된 뒤 죽은 배열에 쓴다.
int bounty_write_region(const mem::Reader& reader, std::uint32_t key,
                        std::uint64_t raw);

// 전부 0 으로. 바꾼 개수를 낸다. 구역이 여럿일 때 하나씩 누르게 하면
// 오늘과 같은 일이 난다.
bool bounty_clear_all(const mem::Reader& reader, int* changed_out);

// --- 구역 이름은 아직 못 붙인다 (2026-09-19) --------------------------
//
// 한 번 붙였다가 **걷어냈다.** 이름의 필드 번호를 모르는 채 "0x00..0x80 을
// 훑어 **처음 비지 않은 문자열**을 쓴다" 로 지었더니, 0x60 이 걸려 화면이
// 데메니스 왕국을 **"구매하기({price})"** 라고 불렀다. 4.8 이 적어 둔
// *"폴백은 틀린 답을 자신 있게 내놓는다"* 를 그대로 재현했다 - 그 교훈을
// 커밋 메시지에 인용해 놓고 같은 짓을 했다.
//
// **라이브로 재서 알아낸 것**(`probe loc find "에르난드 공국" 40`):
//
//   cat 9  엔티티 61 · 81 · 202 · 6101   필드 0x92   "에르난드 공국"
//   cat 9  엔티티 6101                   필드 0x93   "에르난드 공국의 심장부…"
//
// 즉 구역 이름은 **카테고리 9 · 필드 0x92** 다. 그런데 엔티티가 61·81·202·
// 6101 같은 작은 수라, 수배 레코드의 `+0x28`(1000131 · 1000138)은 **현지화
// 엔티티 키가 아니다.** 가정 둘이 다 틀렸다.
//
// 남은 길: 수배 키 -> `RegionInfoManager` 의 행 -> `_displayRegionName`
// (+0x18, `fields.py` 로 떴다) -> 그 객체가 든 진짜 엔티티 -> 0x92 로 해석.
// `towngate.h` 가 이미 그 표의 매니저와 `_stringKey`(+0x08)를 쓴다.
//
// 그때까지 화면은 **키를 그대로** 보인다.

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

// @class TrocTrClearWantedReq
inline constexpr std::uint16_t kClearWantedId = 2837;   // 2850 까지 2646

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
// @class TrocTrChangeWantedStateReq
inline constexpr std::uint16_t kChangeWantedStateId = 2983;   // 2850 까지 2848

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

// **박아 둔 ID 와 게임에서 해석한 ID 가 갈렸는가.** 갈렸으면 `*use` 에
// 해석값을 넣고 참을 돌려준다(전송에는 언제나 해석값을 쓴다).
//
// 왜 필요한가: 메시지 ID 는 게임 갱신에 바뀐다 - 1.0.0.2944 가 `TrocTr*`
// 1115개를 통째로 재번호했고 **옛 번호를 다른 메시지에 재사용**했다. RVA 는
// 틀리면 프롤로그·opcode 검사가 걸러 설치가 거부되지만 **메시지 ID 에는 그
// 그물이 없다** - 유효한 번호이기만 하면 게임은 그 번호의 처리기를 돌린다.
// 이 모듈은 이미 클래스 이름으로 서술자를 찾고 있었는데(이름은 갱신을 안
// 탄다) 와이어는 상수를 쓰고 있어서, 갱신 뒤 **로그는 맞는 값을 보여 주면서
// 전송은 틀린 값으로 나가는** 상태였다(2026-09-18 발견).
bool message_id_drifted(std::uint16_t baked, std::uint16_t resolved,
                        std::uint16_t* use);

// 전송에 실제로 쓰는 ID. 해석 전에는 박아 둔 상수다.
std::uint16_t wanted_effective_clear_id();
std::uint16_t wanted_effective_state_id();

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
