#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/localization.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 게임의 아이템 표(`pa::ItemInfoManager`)를 읽는다. 전부 읽기다.
//
// 실측 구조 (docs/superpowers/specs/2026-09-01-item-table.md):
//
//   매니저 +0x28  색인 표 포인터 ((u32 키, u32 용도미상) 쌍)
//          +0x30  u32 개수 / +0x34 u32 용량
//          +0x58  레코드 포인터 배열 (8바이트 항목)
//   레코드 +0x00  u32 키
//          +0x28  u64 이름 현지화 키
//
// 이름 키를 (키 << 32) | 0x70 으로 조립하지 않는다. 레코드가 게임
// 자신의 값을 +0x28 에 그대로 들고 있으므로 그것을 쓴다.

struct ItemEntry {
    std::uint32_t key = 0;
    std::uint64_t name_key = 0;   // 레코드가 들고 있는 현지화 키
    std::uintptr_t record = 0;
    std::uint8_t grade = 0;       // 0=없음, 1..5 = T1..T5
    std::uint8_t category = 0;    // 74종. 이름은 아직 못 붙였다
    std::uint32_t max_stack = 0;  // 한 칸에 쌓이는 최대 개수
    // 담금질로 올릴 수 있는 최고 값. 0 이면 담금질이 없는 아이템이다.
    //
    // 레코드 +0x250 은 상한 자체가 아니라 그것보다 하나 큰 값이다 -
    // 게임이 `담금질 <= [+0x250] - 1` 로 검사한다. 실측에서 장비는
    // 11 이라 0..10 이고 툴팁 게이지가 정확히 10칸이다.
    std::uint32_t max_temper = 0;

    // 이 아이템 종류에 박을 수 있는 소켓 칸 수. 레코드 +0x238 이다.
    //
    // 지급 작업 함수(0x26A2600)가 `표 +0x238 >= TrItemValue +0x5E`
    // 를 검사하고, 넘으면 조용히 거절한다. 실측에서 한손 무기 3,
    // 방패 2, 재료 0 이고 툴팁의 원 개수와 같다.
    std::uint32_t max_sockets = 0;

    // 이 아이템 종류의 최대 내구도. 레코드 +0x400 의 `_maxEndurance`
    // 이고, **0xFFFF 면 내구도가 없는 아이템**이다.
    //
    // 게임이 아이템을 쓸 때 이렇게 본다 (RVA 0x1D9CBB0).
    //
    //   _maxEndurance != 0xFFFF && 인스턴스 내구도 <= 0 &&
    //   _isDestoryWhenBroken(+0x1FC) != 0   -> 오류 0x36
    std::uint16_t max_endurance = 0xFFFF;

    // `_repairDataList` (레코드 +0x408) 의 항목 수. 다른 목록 칸과
    // 같은 꼴이다 - `{ptr +0x408, u32 개수 +0x410}`.
    //
    // 툴팁의 "수리 불가" 가 이것으로 갈리는지 보려고 읽는다.
    // 수리 데이터를 가진 종류가 하나라도 있으면 종류가 가르는
    // 것이고, 하나도 없으면 다른 것이 결정한다.
    std::uint32_t repair_entries = 0;

    // 장비 연마 상한. 레코드 +0x2E8 의 `_SharpnessData` 첫 i16 이고
    // 실측에서 무기가 100 이다. 0 이면 연마가 없는 아이템이다.
    //
    // 변환 함수가 `min(TrItemValue +0x1AE, 이 값)` 을 레코드 +0x58 에
    // 넣는다. 담금질과 달리 상한 그대로다("-1" 이 아니다).
    std::int16_t max_sharpness = 0;

    // `_equipTypeInfo` (레코드 +0x42). **0xFFFF 면 장비가 아니다** -
    // `_maxEndurance` 와 같은 표기법이다.
    //
    // 지급의 소켓 갈래를 가르는 값이라 읽는다. 생성 함수 0x2A70000 이
    // 판별자(0x2358A70 -> 0xF090BC0)로 갈래를 정하는데, 그 판별자의
    // 마지막 줄이 `return _equipTypeInfo == 0xFFFF` 다. 장비가 아니면
    // 소켓수>0 이 오류 갈래로 빠진다.
    // specs/2026-09-07-socket-grant-unlock-research.md 3.1 절.
    std::uint16_t equip_type = 0xFFFF;
};

// 등급 표시 이름. 표 밖의 값은 "?" 다.
const char* grade_label(std::uint8_t grade);

// 개수가 이보다 크면 매니저를 잘못 집은 것으로 본다. 실측 6,810개다.
inline constexpr std::uint32_t kMaxItemCount = 1u << 20;

// 색인 표와 레코드 배열이 같은 키를 말하는지 본다.
//
// RTTI 후보가 진짜인지 판별하는 용도다. 카메라에서 vtable 값을 우연히
// 담은 메모리를 후보로 집어 11회 어긋난 적이 있어, 구조 자체의
// 앞뒤가 맞는지로 확인한다.
bool looks_like_item_manager(const mem::Reader& reader,
                             std::uintptr_t manager);

// RTTI 로 살아 있는 매니저를 찾는다. looks_like_item_manager 를
// 통과하는 첫 후보를 쓴다.
bool find_item_manager(const mem::Rtti& rtti, const mem::Reader& reader,
                       std::uintptr_t* out);

// 레코드 포인터 배열을 걸어 항목을 모은다. max 가 0 이면 전부.
// 널 슬롯이나 읽기 실패한 레코드는 건너뛴다.
bool read_item_table(const mem::Reader& reader, std::uintptr_t manager,
                     std::vector<ItemEntry>* out, std::size_t max);

// ------------------------------------- 아이템 키 <-> 짧은 식별자 대응표

// 인벤토리 레코드가 저장하는 것은 아이템 표 키가 아니라 **표에서의
// 순번**이다. 게임은 변환 함수 안에서 전역이 가리키는 객체의 `+0x68`
// 표를 키로 조회해 그 순번을 u16 으로 얻는다.
//
// 실측 구조 (docs/superpowers/specs/2026-09-02-inventory.md):
//
//   전역 [RVA 0x6331358] -> 객체
//   객체 +0x68  표
//   표   +0x00  u32 ?             307
//        +0x04  u32 개수          6810 (아이템 표 개수와 같다)
//        +0x08  u32 해시 용량     8088
//        +0x0C  u32 레코드 개수   6810
//        +0x10  ptr 해시 슬롯 배열
//        +0x18  ptr 레코드 포인터 배열
//   슬롯 {u32 아이템 키, u32 순번} 8바이트. 빈 칸은 키가 0xFFFFFFFF
//        다 - 배열 끝이 그 값으로 차 있다.
//   레코드 (16바이트)
//        +0x00  u32 ?   순번이 아니다 (순번 5915 의 레코드가 946)
//        +0x04  u32 아이템 키
//
// 레코드 순서는 `ItemInfoManager` 의 순서와 같다 - 실측에서 0번이
// 편전(2200), 1번이 화살(50001), 5915번이 그로테반트 판금 투구로
// 양쪽이 같았다.
//
// RVA 는 박아 두지 않는다. Denuvo 가 빌드마다 섹션을 뒤섞으므로
// 현지화 전역과 같이 함수 본문 패턴에서 disp32 를 읽어 구한다.
struct ItemKeyMap {
    std::uintptr_t global = 0;       // 전역의 주소
    std::uintptr_t object = 0;       // 전역이 가리키는 객체
    std::uintptr_t table = 0;        // object + 0x68
    std::uintptr_t slots = 0;        // 해시 슬롯 배열
    std::uintptr_t records = 0;      // 레코드 포인터 배열
    std::uint32_t count = 0;         // 표가 말하는 항목 수
    std::uint32_t capacity = 0;      // 해시 슬롯 칸 수
    std::uint32_t record_count = 0;  // 레코드 개수
};

struct ItemKeyPair {
    std::uint32_t key = 0;  // 아이템 표 키 (1955057078)
    std::uint32_t id = 0;   // 인벤토리가 저장하는 순번 (5915)
};

// 변환 함수 본문이 일치하는 곳을 전부 모아, 그 disp32 가 가리키는
// 전역의 RVA 를 낸다. 같은 꼴의 조회 코드가 표마다 있어 한 곳만
// 일치하지 않는다 - 실측에서 여러 곳이 걸렸다. 이미지 밖을 가리키는
// disp 는 뺀다.
std::vector<std::uint64_t> find_item_key_map_rvas(
    const std::vector<std::uint8_t>& image, std::size_t max);

// 후보를 하나씩 따라가 표의 개수가 expected_count 인 것을 고른다.
// 아이템 표 개수(실측 6,810)를 주면 아이템 대응표가 잡힌다.
//
// 개수가 판별자인 이유: 실측 후보 34곳 중 용량이 8,088 인 것만도
// 셋이었지만(6810 / 7251 / 6703), 개수가 6,810 인 것은 하나뿐이었다.
//
// 조건에 맞는 후보가 없거나 둘 이상이면 false 다 - 둘 이상이면
// 무엇이 아이템 표인지 고를 수 없다. 표가 아직 안 올라왔을 때도
// 조용히 false 이므로 재시도 루프에서 부르면 된다.
bool find_item_key_map(const mem::Reader& reader,
                       const std::vector<std::uint8_t>& image,
                       std::uint32_t expected_count, ItemKeyMap* out);

// 함수 본문 패턴에 기대지 않고, 힙에서 대응표 구조를 직접 찾는다.
//
// 변환 함수 패턴은 스택 오프셋·명령 인코딩에 의존해 게임이 갱신되면
// 깨진다(2026-09-04 업데이트에서 후보 0곳이 됐다). 대응표 객체는 힙에
// 있고 그 컨테이너 레이아웃(+0x04 개수, +0x08 용량, +0x10 슬롯,
// +0x18 레코드)은 엔진 코드라 데이터·주소가 바뀌어도 그대로다.
//
// 개수가 expected_count 인 자리를 찾고, 레코드 몇 개의 키가 실제
// 아이템 키(sorted_keys)에 있는지로 확인한다 - 이 의미 검증이 있어야
// 우연히 개수만 같은 다른 해시맵을 배제한다. sorted_keys 는 오름차순.
// global 은 0 으로 둔다(전역 슬롯이 아니라 객체를 직접 찾았다).
bool find_item_key_map_by_scan(const mem::Reader& reader,
                               std::uint32_t expected_count,
                               const std::vector<std::uint32_t>& sorted_keys,
                               ItemKeyMap* out);

// 가장 튼튼하고 빠른 길: 대응표는 ItemInfoManager 자신의 +0x68 이다.
// 매니저를 RTTI 로 찾아(find_item_manager) 그 주소를 넘기면 스캔 없이
// 즉시 읽는다. 개수가 expected_count 여야 한다.
bool find_item_key_map_from_manager(const mem::Reader& reader,
                                    std::uintptr_t manager,
                                    std::uint32_t expected_count,
                                    ItemKeyMap* out);

// 레코드 배열을 걸어 {아이템 키, 순번} 을 모은다. 순번은 배열에서의
// 위치 그대로다 - 널 슬롯을 건너뛰어도 앞으로 당기지 않는다.
//
// 해시 슬롯이 아니라 레코드를 걷는 이유는 이쪽이 전수이기 때문이다.
// 해시 슬롯은 8,088칸 중 빈 칸이 섞여 있어 순번을 얻으려면 결국
// 레코드를 봐야 한다.
bool read_item_key_map(const mem::Reader& reader, const ItemKeyMap& map,
                       std::vector<ItemKeyPair>* out);

// 대응표에 없는 키의 답.
inline constexpr std::uint32_t kNoItemId = 0xFFFFFFFFu;

// 키로 정렬된 대응표에서 순번을 찾는다. 없으면 kNoItemId.
std::uint32_t find_item_id(const std::vector<ItemKeyPair>& sorted,
                           std::uint32_t key);

// --------------------------------------------------------------- 목록

struct ItemCatalogEntry {
    std::uint32_t key = 0;
    std::uint64_t name_key = 0;
    std::string name;             // 빈 문자열이면 현지화 표에 없는 것
    std::uint8_t grade = 0;
    std::uint8_t category = 0;
    std::uint32_t max_stack = 0;
    std::uint32_t max_temper = 0;   // ItemEntry 의 같은 칸
    std::uint32_t max_sockets = 0;  // ItemEntry 의 같은 칸
    std::uint16_t max_endurance = 0xFFFF;  // ItemEntry 의 같은 칸
    std::uint32_t repair_entries = 0;      // ItemEntry 의 같은 칸
    std::int16_t max_sharpness = 0;        // ItemEntry 의 같은 칸
    std::uint16_t equip_type = 0xFFFF;     // ItemEntry 의 같은 칸
};

// 표를 걷고 이름까지 붙인다. sys 가 비어 있으면(valid() 아님) 이름
// 없이 키만 채운다.
//
// 이름이 안 풀린 항목도 목록에서 빼지 않는다. 실측에서 6,810개 중
// 72개가 그랬는데, 키는 있는 아이템이므로 지급 대상이 될 수 있다.
bool build_item_catalog(const mem::Reader& reader, std::uintptr_t manager,
                        const LocSystem& sys,
                        std::vector<ItemCatalogEntry>* out);

// --------------------------------------------------- 모드용 배경 탐색

// 이미 이미지를 읽어 둔 Rtti 로 목록을 만들어 캐시한다. 350MB 이미지
// 읽기와 힙 전수 조사를 두 번 하지 않도록, 이미 그것을 한 배경
// 스레드가 호출자다.
//
// 표가 아직 안 올라왔으면 조용히 false 다. 재시도 루프에서 부르면
// 된다 - 이미 준비됐으면 즉시 true 로 빠진다.
// 목록을 (다시) 만들어야 하는가.
//
// 게임은 아이템 표를 현지화보다 먼저 올린다. 그 순간에 만들고 굳으면
// 이름이 영영 빈다 - 실제로 그렇게 났다. 현지화가 올라온 뒤 한 번
// 더 만들어야 한다.
bool should_rebuild_catalog(bool have_catalog, bool names_resolved,
                            bool loc_available);

// 이름까지 풀린 목록을 만들었으면 true. 아직이면 다시 부르면 된다.
bool discover_items(const mem::Rtti& rtti, const mem::Reader& reader);

// 키 -> 순번 대응표를 배경에서 한 번 읽어 캐시한다. 아이템 표가
// 먼저 올라와 있어야 한다 - 후보를 개수로 가리기 때문이다.
//
// 소켓을 지급할 때 필요하다. 보관함 파일은 보석의 **아이템 키**를
// 들고 있는데 게임에 보내는 6바이트는 **순번**으로 시작한다. 순번은
// 표에서의 위치라 게임이 갱신되면 달라진다.
bool discover_item_ids(const mem::Rtti& rtti, const mem::Reader& reader);
bool item_ids_ready();

// 캐시에서 키의 순번을 찾는다. 아직 안 읽었거나 없으면 kNoItemId.
std::uint32_t item_id_for_key(std::uint32_t key);

// 이 아이템을 새로 줄 때 채워야 할 현재 내구도.
//
// 표의 `_maxEndurance` 가 0xFFFF 면 내구도가 없는 아이템이므로 0 이다.
// 그 밖에는 최대치를 준다 - 게임이 새로 만드는 것과 같은 상태다.
// 표가 아직 없으면 0 이다.
std::uint16_t full_endurance_for(std::uint32_t item_key);

// 이 아이템의 장비 연마 상한. 표가 없거나 그런 칸이 없으면 0.
std::int16_t max_sharpness_for(std::uint32_t item_key);

// 소켓에 박는 보석의 분류(`_itemType`). 실측으로 확인했다 - 바람
// 가르기와 파괴 I 이 74 이고 한손검이 56 이다. 표에 190개 있고
// `category_name` 이 "심연 장비" 로 부른다.
inline constexpr std::uint8_t kSocketGemCategory = 74;

// 이 아이템에 **지급으로** 실을 수 있는 소켓 칸 수. 0 이면 소켓 금지다.
//
// 게임의 규칙 그대로다(0x2A70000 · 0xF090BC0, 실측 디스어셈블):
//   - 장비가 아니면(`equip_type == 0xFFFF`) 소켓수>0 은 오류 갈래다.
//   - 겹치는 아이템(`max_stack > 1`)도 마찬가지다.
//   - 장비면 `표 +0x238(max_sockets) >= 소켓수` 여야 통과한다.
// 배열 다섯 칸 상한은 여기서 안 자른다 - 부르는 쪽(grant)이 자른다.
//
// 순수 함수라 표 없이 시험할 수 있다.
std::uint32_t socket_room(std::uint32_t max_sockets, std::uint32_t max_stack,
                          std::uint16_t equip_type);

// 위를 아이템 키로 조회한다. 표가 아직 없거나 키가 없으면 0(=금지).
std::uint32_t socket_room_for(std::uint32_t item_key);

// 소켓 한 칸의 6바이트를 조립한다.
//
// 게임의 복사 루프(0x234FC31~)가 `TrItemValue +0x40 + k*6` 을 그대로
// 옮긴 뒤 **다섯 번째 바이트만 칸 번호(k)로 덮어쓴다.** 그래서 순번과
// 꼬리 상수만 채우면 된다.
//
//   24 0D FF FF 00 04   보석이 박힌 칸  [순번][FF FF][k][04]
//   FF FF 00 00 00 04   열려 있는 빈 칸 [FFFF][00 00][k][04]
//
// `[2..3]` 은 채움 표시다 - 보석이 있으면 0xFFFF, 비면 0x0000
// (equip.cpp `socket_fill_entry` 와 같은 규칙).
//
// `[5]` 는 **고정 상수가 아니다.** 한 판 안에서는 열린 칸이 전부 같은
// 값인데 판이 바뀌면 달라질 수 있다(실측 다섯 판: 04 · 05 · 03 · 02 · 02).
// 무엇이 정하는지는 모른다. 잠긴 칸은 게임이 안 건드려 풀 쓰레기값이 남는다.
//
// **그래서 우리가 무엇을 보내든 상관없다.** 실측 2026-09-08: 세션 값이
// 0x05 인 판에 0x04 를 실어 지급했는데 정상으로 들어왔고, 저장·재시작
// 뒤에는 그 판의 값(0x03)으로 게임이 덮어써 있었다. 즉 게임이 로드할
// 때 다시 매긴다.
//
// 아래 상수는 그 "아무 값"이다. 예전에 쓰던 0xFF 만은 라이브 어디에도
// 없던 값이라 피한다. 더 충실히 하려면 같은 인벤토리의 열린 칸에서
// 살아 있는 값을 읽어 그대로 쓰면 된다(지금은 그럴 이유를 못 찾았다).
// specs/2026-09-07-socket-grant-unlock-research.md 2.2 절.
inline constexpr std::uint8_t kSocketOpenTail = 0x04;

void make_socket_bytes(std::uint16_t gem_id, std::uint8_t out[6]);

// 보석의 **아이템 키**로 위를 조립한다. 대응표를 아직 못 읽었거나
// 키가 표에 없으면 false 다.
//
// 조용히 넘기지 않고 거절하는 이유: 순번을 틀리면 엉뚱한 보석이
// 박힌다. 안 박히는 것이 낫다.
bool socket_bytes_for_key(std::uint32_t gem_key, std::uint8_t out[6]);

// 이름이 실제로 풀렸는가.
bool items_named();

// 로딩 표시용. 마지막으로 만든 카탈로그의 (이름 풀린 수, 전체 수).
// 이름은 현지화 후 뒤늦게 채워지므로, 창이 '불러오는 중 N/M' 으로
// 진행을 보여 주면 느린 로드인지 멈춘 것인지 사람이 가릴 수 있다.
std::size_t items_named_count();
std::size_t items_total_count();

// 캐시가 준비됐는가. 준비된 뒤에는 목록이 다시 바뀌지 않는다.
bool items_ready();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<ItemCatalogEntry>& item_catalog();

}  // namespace cdtb::game
