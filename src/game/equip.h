#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "mem/reader.h"
#include "mem/rtti.h"

// 착용 장비(worn gear) 에디터의 읽기 계층. 소켓·담금질·연마·염색을 다룬다 -
// 인벤토리 컨테이너 핸들도 NPC 도 필요 없다. 힙에서 `EquipSlotActorComponent`
// 를 전부 찾아(collect_equip_tables) 슬롯 태그가 서로 다른 것으로 착용 배열을
// 유도하고(find_equip_table), 정신력 풀 + 착용 조각 최다로 플레이어 것을
// 고른다(pick_player_table). cplayer 포인터 경로(MGRCHAIN)는 안 쓰여 지웠다.
//
// 출처: 소켓·장비 컴포넌트 매핑은 XeTrinityz/Trinity (MIT). Nexus 3209 CT 가
// Cheat Engine 으로 이식한 것을 실측 참고해 다시 이식했다. 자세한 근거는
// docs/superpowers/specs/2026-09-05-equip-editor-port.md.
//
// 이 헤더는 **읽기 전용**이다. 쓰기(both-realms)는 라이브 검증 뒤에 붙인다.

namespace cdtb::game {

// 착용 장비 테이블(배열/개수/스트라이드).
struct EquipTable {
    std::uintptr_t arr = 0;
    std::uint32_t cnt = 0;
    std::uint32_t stride = 0;
    std::uintptr_t comp = 0;   // 이 테이블을 유도한 장비 컴포넌트(액터 앵커용)
    // 이번 힙 스캔에는 없고 캐시에서 검증해 되살린 연속 횟수(0 = 이번 스캔에 있음). 스캔이
    // 매번 완전하지 않아 두는 것이고, 되살린 표는 표시용 선택의 동률 깨기(prefer)를 못 받는다.
    int missed = 0;
};

// 확정된 컴포넌트에서 착용장비 배열을 유도한다. 서로 다른 슬롯 태그 수로
// 점수화해 최고를 택한다(다른 아이템 값 배열과 구분). 실패면 false.
bool find_equip_table(const mem::Reader& reader, std::uintptr_t comp,
                      EquipTable* out);

struct WornSocket {
    std::uint16_t gem = 0;       // 보석 순번 (+0)
    std::uint16_t marker = 0;    // 채움=0xFFFF, 빔=0 (+2)
    std::uint8_t index = 0xFF;   // 열림=k, 잠김=0xFF (+4)
    bool locked() const { return index == 0xFF; }
    bool filled() const { return marker == 0xFFFF && gem != 0xFFFF; }
};

// 염색 레코드 하나. entry+0x78 벡터, 16바이트/레코드. zone 이 정체성이다
// (행 번호가 아님) - 한 조각은 12 zone 중 일부만 레코드로 가진다.
struct WornDye {
    int rec = 0;                 // 벡터 안 인덱스(both-realms 쓰기 인자)
    std::uint8_t zone = 0xFF;    // +6, 정체성
    std::uint8_t r = 0;          // +7
    std::uint8_t g = 0;          // +8
    std::uint8_t b = 0;          // +9
};

struct WornPiece {
    std::uintptr_t entry = 0;
    std::uint64_t instance = 0;   // +0x00, both-realms 매칭 키
    std::uint32_t key = 0;        // +0x08 하위16, 아이템 순번
    // +0x0A 담금질(툴팁 게이지 10칸, 상한 ItemCatalogEntry::max_temper). 이식 원본(Trinity)이
    // "refinement" 라 불러 오래 "연마" 로 표시됐는데, 인벤토리 레코드 +0x0A 와 같은 자리라
    // 담금질이다(실측 2026-09-12: 창에서 10 을 쓴 장비가 전부 +0x0A = 10, +0x58 은 무기 한 개만
    // 50 이고 나머지 0).
    std::uint16_t temper = 0;
    // +0x58 장비 연마(툴팁 "장비 연마 N/100", 상한 max_sharpness). 인벤토리 레코드와 같은 자리.
    std::uint16_t sharpness = 0;
    std::uint16_t slot_tag = 0;   // +(stride-8)
    int unlocked = 0;             // 열린 소켓 수
    WornSocket sockets[5]{};      // entry+0x60 벡터
    std::vector<WornDye> dyes;    // entry+0x78 벡터 (있으면)
    // 가방(인벤토리 레코드)에도 있는 장비인가. 장비 표에 가방 아이템이 섞인다 - 슬롯 태그 23,
    // 게임 툴팁 "비활성화"(2026-09-22 실측). 그런 장비는 가방 레코드가 진짜라 담금질·연마·
    // 소켓을 가방 레코드에서 읽고(apply_bag_truth) 쓰기도 가방 레코드에 같이 한다(eq_write_*, 염색 제외).
    bool in_bag = false;
};

// 착용 장비 목록을 읽는다(빈 슬롯 제외). 실패면 false.
bool read_worn_gear(const mem::Reader& reader, const EquipTable& t,
                    std::vector<WornPiece>* out);

// ------------------------------------------------------------------ 자동 선택
// RTTI 로 서버·클라 장비 컴포넌트 인스턴스를 모두 열거해, 유효한 착용장비
// 테이블만 수집한다(잡음 필터: 실제아이템 >=3). both-realms 쓰기의 대상.
int collect_equip_tables(const mem::Rtti& rtti, const mem::Reader& reader,
                         std::vector<EquipTable>* out);

// 플레이어의 착용장비를 한 번에 읽는다 - 테이블은 pick_player_table 이 고른다
// (정신력 풀 + 착용 조각 최다). out 은 그 테이블, pieces 는 그 착용 장비.
// 실패면 false.
bool read_player_worn(const mem::Rtti& rtti, const mem::Reader& reader,
                      EquipTable* table_out, std::vector<WornPiece>* pieces_out);

// ------------------------------------------------------------------ 캐릭터 선택
// 월드에 있는 플레이어형 캐릭터(정신력 풀 보유)마다 하나. 클리프·웅카·데미안은 번갈아
// 조종하는데 "조각 최다" 규칙은 늘 클리프를 골라, 웅카로 플레이해도 클리프 장비만 보였다
// (사용자 보고 2026-09-12). 캐릭터 행은 comp+0x08 의 캐릭터 객체에서 actor_character_row
// 로 푼다(실측: 조각 21 = 행 0 클리프, 16 = 행 5 웅카, 11 = 행 3 데미안). 행을 못 푼
// 테이블은 후보에 안 든다(자동 선택에는 든다). 정신력 풀은 동행에도 있어 플레이어블
// (roster.h is_playable_character_row: 주인공 행 또는 Mercenary_Main)만 후보로 남긴다.
// 이름은 로스터(character_by_row)에서 그릴 때 푼다 - 발견 시점에 로스터가 아직 없을 수 있다.
struct EquipCharacter {
    std::uint16_t row = 0xFFFF;   // 캐릭터 행
    int pieces = 0;               // 착용 조각 수(서로 다른 슬롯, realm 중 큰 것)
};
inline constexpr std::uint16_t kEquipAutoCharacter = 0xFFFF;   // 자동(조각 최다)

// 마지막 발견의 후보 목록(행 오름차순). 캐시 복사.
std::vector<EquipCharacter> equip_characters();
// 보여 줄 캐릭터를 고른다(kEquipAutoCharacter = 자동). 다음 발견 주기에 반영된다 - 부르는
// 쪽이 equip_request_refresh 로 당긴다. 고른 캐릭터가 월드에 없으면 자동으로 돌아간다.
void equip_select_character(std::uint16_t row);
std::uint16_t equip_selected_character();
// 지금 캐시된 테이블의 캐릭터 행(못 풀었으면 kEquipAutoCharacter).
std::uint16_t equip_current_character();
// 마지막 발견이 소화한 선택. equip_selected_character 와 다르면 아직 갱신 중이다(창이
// "월드에 없음" 과 "갱신 중" 을 가르는 데 쓴다, 리뷰 E-2).
std::uint16_t equip_resolved_character();

// ------------------------------------------------------------------ 캐시/발견
// 분석 스레드에서 주기적으로 부른다(힙 스캔이라 값싸지 않음). 플레이어
// 착용장비와 both-realms 테이블을 캐시한다. UI/쓰기는 캐시를 쓴다.
void equip_discover(const mem::Rtti& rtti, const mem::Reader& reader);

// 캐시된 플레이어 착용장비 스냅샷(UI 용). 없으면 false.
bool equip_snapshot(std::vector<WornPiece>* out);
bool equip_ready();

// 캐시된 플레이어 장비 컴포넌트(액터 앵커용, comp+0x08=char). 없으면 0.
std::uintptr_t equip_player_comp();

// 캐시된 both-realms 장비 테이블 전체를 복사한다(NPC 포함). 플레이어 치트가
// 클라·서버 양쪽 게이지를 찾을 때 쓴다(사망 판정은 서버 게이지가 권위).
void equip_tables_copy(std::vector<EquipTable>* out);

// 패널이 즉시 새로고침을 요청. 분석 스레드가 다음 주기에 처리한다.
void equip_request_refresh();
bool equip_take_refresh();

// 쓰기 직후용 재읽기: 힙 스캔 없이 캐시된 플레이어 테이블에서 착용장비만 다시 읽어
// 스냅샷을 갱신한다. 가방 장비를 가리려고 서버 인벤토리를 한 번 훑는다(수 MB 복사 - 클릭마다
// 한 번이라 렌더 스레드에서 불러도 되지만 매 프레임 부르지 말 것).
void equip_refresh_pieces(const mem::Reader& reader);

// 장비 창 쓰기의 결과. realms = 장비 표에 쓴 realm 수(클라·서버), bag = 가방(인벤토리)
// 레코드에 쓴 수(서버·클라), in_bag = 이 인스턴스가 가방에도 있었는가(쓰기 직전에 확인).
// 판정은 eq_verdict(equip_bag.h) - 가방 장비는 가방 레코드가 진짜라 거기에 썼으면 성공이다.
struct EqWriteResult {
    int realms = 0;
    int bag = 0;
    bool in_bag = false;
};

// 가방 색인 - 서버·클라 인벤토리의 인스턴스 -> 레코드 주소. 일괄 쓰기는 이것을 한 번만 만들어
// 넘긴다(장비마다 새로 만들면 한 프레임에 수백 MB 를 복사한다 - TS §2.16 과 같은 꼴).
struct EqBagIndex {
    std::unordered_map<std::uint64_t, std::uintptr_t> server;
    std::unordered_map<std::uint64_t, std::uintptr_t> client;
};

// 지금의 서버·클라 인벤토리 컴포넌트로 가방 색인을 만든다(컴포넌트가 없으면 그쪽은 빈 표).
EqBagIndex eq_bag_index(const mem::Reader& reader);

// ------------------------------------------------------------------ 쓰기 (인프로세스)
// **모드(주입 DLL)에서만 부른다.** 게임과 같은 주소공간에서 직접 쓴다.
// 전부 SEH 로 감싸고 read-back 으로 검증한다. 잠긴 소켓은 거부한다.
//
// both-realms: collect_equip_tables 로 모은 모든 테이블에서 인스턴스 ID 로
// entry 를 찾아 각각에 쓰고, 가방에도 있는 장비는 가방 레코드(서버·클라)에도 쓴다(염색 제외).
// 결과는 EqWriteResult - bag 을 주면 그 색인을 쓰고, 안 주면 이번 쓰기만을 위해 만든다.

// 이미 열린 소켓 k(0..4)에 보석 순번을 박는다. gem==0xFFFF 면 비운다.
EqWriteResult eq_write_socket(const mem::Reader& reader, std::uint64_t instance,
                              int k, std::uint16_t gem,
                              const EqBagIndex* bag = nullptr);

// 담금질(+0x0A)을 설정한다. 상한은 부르는 쪽이 표(max_temper)로 자른다.
EqWriteResult eq_write_temper(const mem::Reader& reader, std::uint64_t instance,
                              std::uint16_t level, const EqBagIndex* bag = nullptr);

// 장비 연마(+0x58)를 설정한다. 상한은 부르는 쪽이 표(max_sharpness)로 자른다.
EqWriteResult eq_write_sharpness(const mem::Reader& reader, std::uint64_t instance,
                                 std::uint16_t level, const EqBagIndex* bag = nullptr);

// 염색 레코드 rec 의 RGB 를 설정한다.
int eq_write_dye(const mem::Reader& reader, std::uint64_t instance, int rec,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b);

// **잠긴 소켓 칸을 연다.** 이미 열린 칸과 박힌 보석은 안 건드린다.
//
// 게임의 지급 코드가 하는 것과 같은 두 줄이다 - 레코드 `+0x70 = 칸 수` 와
// 칸 `[4] = k`. 락은 그 둘이 전부이고 검증이 없다(실측 2026-09-08: 열린 칸
// 0개짜리 장비를 5칸으로 열어 저장·재시작을 넘겼다).
// 근거: specs/2026-09-07-socket-grant-unlock-research.md 9절.
//
// 아이템표의 상한(`max_sockets`)과는 별개다. 표는 **툴팁 목록**만 정하고
// 스탯 계산은 여기서 연 칸을 그대로 더한다. 표보다 많이 열어도 동작하지만
// 툴팁에 다 안 보인다 - `items::socket_cap_apply` 로 표도 같이 올린다.
//
// 착용 장비는 both-realms 로, 가방 장비는 가방 레코드에도 쓴다. 결과는 EqWriteResult.
EqWriteResult eq_unlock_sockets(const mem::Reader& reader, std::uint64_t instance,
                                int want, const EqBagIndex* bag = nullptr);

// 위와 같은 일을 **레코드 주소로** 한다. 인벤토리 레코드와 착용 장비
// entry 는 같은 구조라(둘 다 `+0x60` 벡터, `+0x68` 크기, `+0x70` 열린 수)
// 한 함수로 된다. 인벤토리 레코드는 그 자체가 authoritative 라 단일 쓰기로
// 저장까지 살아남는다(both-realms 불필요).
//
// 연 칸 수를 돌려준다. 0 이면 아무것도 안 열었다.
int socket_unlock_record(const mem::Reader& reader, std::uintptr_t record,
                         int want);

}  // namespace cdtb::game
