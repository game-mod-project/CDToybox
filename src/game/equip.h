#pragma once

#include <cstdint>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

// 착용 장비(worn gear) 에디터의 읽기 계층. 소켓(이미 열린 칸)·연마·염색을
// cplayer 포인터 경로로 다룬다 - 인벤토리 컨테이너 핸들도 NPC 도 필요 없다.
//
// 출처: 소켓·장비 컴포넌트 매핑은 XeTrinityz/Trinity (MIT). Nexus 3209 CT 가
// Cheat Engine 으로 이식한 것을 실측 참고해 다시 이식했다. 자세한 근거는
// docs/superpowers/specs/2026-09-05-equip-editor-port.md.
//
// 이 헤더는 **읽기 전용**이다. 쓰기(both-realms)는 라이브 검증 뒤에 붙인다.

namespace cdtb::game {

// 코어 전역과 오프셋. MGRCHAIN AOB 로 해석한다(RVA 를 박지 않는다).
struct EquipGlobals {
    std::uintptr_t g = 0;      // 코어 전역의 절대 주소
    std::uint32_t pm = 0;      // 보통 0x30
    std::uint32_t blk = 0;     // 보통 0x68 (actor -> 서브)
    std::uint32_t mo = 0;      // 보통 0xB8 (인벤 매니저용, 참고)
    bool ok() const { return g != 0; }
};

// MGRCHAIN 을 디스크 이미지에서 스캔해 G/pm/blk/mo 를 디코드한다. 여러 사이트가
// 맞으면 전부 같은 값이어야 한다(자기검증). 실패면 false.
bool resolve_equip_globals(const mem::Rtti& rtti, const mem::Reader& reader,
                           EquipGlobals* out);

// 클라이언트 플레이어 액터 = [[[G]+pm]+0x50]. 실패면 0.
std::uintptr_t equip_player_actor(const mem::Reader& reader,
                                  const EquipGlobals& g);

// 액터에서 장비 컴포넌트를 찾는다. actor+blk -> 서브, 서브+0x38 -> comp,
// comp+0x08 == actor 백참조로 검증. 실패시 서브(및 액터) 안에서 +0x08==actor
// 인 포인터를 0x400 범위로 백참조 검색. 실패면 0.
std::uintptr_t equip_component(const mem::Reader& reader, std::uintptr_t actor,
                               std::uint32_t blk);

// 착용 장비 테이블(배열/개수/스트라이드).
struct EquipTable {
    std::uintptr_t arr = 0;
    std::uint32_t cnt = 0;
    std::uint32_t stride = 0;
    std::uintptr_t comp = 0;   // 이 테이블을 유도한 장비 컴포넌트(액터 앵커용)
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
    std::uint16_t refine = 0;     // +0x0A
    std::uint16_t slot_tag = 0;   // +(stride-8)
    int unlocked = 0;             // 열린 소켓 수
    WornSocket sockets[5]{};      // entry+0x60 벡터
    std::vector<WornDye> dyes;    // entry+0x78 벡터 (있으면)
};

// 착용 장비 목록을 읽는다(빈 슬롯 제외). 실패면 false.
bool read_worn_gear(const mem::Reader& reader, const EquipTable& t,
                    std::vector<WornPiece>* out);

// entry+0x60 소켓 벡터에서 열린 소켓 수. 잠김(+4==0xFF)에서 멈춘다. 벡터가
// 아니면 -1.
int socket_unlocked(const mem::Reader& reader, std::uintptr_t entry);

// ------------------------------------------------------------------ 자동 선택
// RTTI 로 서버·클라 장비 컴포넌트 인스턴스를 모두 열거해, 유효한 착용장비
// 테이블만 수집한다(잡음 필터: 실제아이템 >=3). both-realms 쓰기의 대상.
int collect_equip_tables(const mem::Rtti& rtti, const mem::Reader& reader,
                         std::vector<EquipTable>* out);

// 플레이어의 착용장비를 한 번에 읽는다(가장 큰 테이블 = 플레이어). out 은
// 그 테이블, pieces 는 그 착용 장비. 실패면 false.
bool read_player_worn(const mem::Rtti& rtti, const mem::Reader& reader,
                      EquipTable* table_out, std::vector<WornPiece>* pieces_out);

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

// 쓰기 직후용 빠른 재읽기: 힙 스캔 없이 캐시된 플레이어 테이블에서
// 착용장비만 다시 읽어 스냅샷을 갱신한다(렌더 스레드에서 값싸다).
void equip_refresh_pieces(const mem::Reader& reader);

// ------------------------------------------------------------------ 쓰기 (인프로세스)
// **모드(주입 DLL)에서만 부른다.** 게임과 같은 주소공간에서 직접 쓴다.
// 전부 SEH 로 감싸고 read-back 으로 검증한다. 잠긴 소켓은 거부한다.
//
// both-realms: collect_equip_tables 로 모은 모든 테이블에서 인스턴스 ID 로
// entry 를 찾아 각각에 쓴다. 쓴 realm 수를 돌려준다(0 이면 실패).

// 이미 열린 소켓 k(0..4)에 보석 순번을 박는다. gem==0xFFFF 면 비운다.
int eq_write_socket(const mem::Reader& reader, std::uint64_t instance,
                    int k, std::uint16_t gem);

// 연마(refinement)를 설정한다.
int eq_write_refine(const mem::Reader& reader, std::uint64_t instance,
                    std::uint16_t level);

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
// 착용 장비는 both-realms 로 쓴다. 쓴 realm 수를 돌려준다(0 이면 실패).
int eq_unlock_sockets(const mem::Reader& reader, std::uint64_t instance,
                      int want);

// 위와 같은 일을 **레코드 주소로** 한다. 인벤토리 레코드와 착용 장비
// entry 는 같은 구조라(둘 다 `+0x60` 벡터, `+0x68` 크기, `+0x70` 열린 수)
// 한 함수로 된다. 인벤토리 레코드는 그 자체가 authoritative 라 단일 쓰기로
// 저장까지 살아남는다(both-realms 불필요).
//
// 연 칸 수를 돌려준다. 0 이면 아무것도 안 열었다.
int socket_unlock_record(const mem::Reader& reader, std::uintptr_t record,
                         int want);

}  // namespace cdtb::game
