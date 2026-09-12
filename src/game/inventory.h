#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 서버 인벤토리 컴포넌트(`pa::ServerInventoryActorComponent`)를 읽는다.
//
// 실측 구조 (docs/superpowers/specs/2026-09-02-inventory.md):
//
//   컴포넌트 +0x18  ptr 컨테이너 포인터 배열
//            +0x20  u32 개수 / +0x24 u32 용량   (실측 18 / 18)
//   컨테이너 +0x00  ptr 레코드 배열
//            +0x08  u32 배열 칸 수 (실측 1460)
//            +0x0C  u32 같은 값
//            +0x10  u16 종류 / +0x12 u16 사용 / +0x14 u16 용량
//   레코드   0xC8 간격
//            +0x00  u64 인스턴스 ID   (빈 칸은 전부 0xFF)
//            +0x08  u16 순번 / +0x0A u16 담금질
//            +0x10  i64 개수
//            +0x60  ptr 소켓 배열 / +0x68 u32 5 / u32 5
//
// 컴포넌트 앞부분을 훑어 `{포인터, 사용, 용량}` 꼴을 찾는 방식은
// 쓰지 않는다. 그렇게 하면 엉뚱한 컨테이너가 걸리고 정작 플레이어
// 가방은 못 잡는다 - 가방은 `+0x18` 의 포인터 배열 너머에 있다.
//
// 컨테이너의 `용량`은 화면에 나오는 값과 같다. 실측에서 종류 1 이
// 97/130, 종류 4 가 144/300 이었고 화면 표시와 일치했다.

struct InventoryContainer {
    std::uintptr_t address = 0;   // 컨테이너 자신
    std::uintptr_t records = 0;   // 레코드 배열
    std::uint32_t slots = 0;      // 배열 칸 수
    std::uint16_t kind = 0;       // 종류 (실측 0~4)
    std::uint16_t used = 0;       // 사용 개수 (믿지 않는다, 아래 참고)
    std::uint16_t capacity = 0;   // 화면에 나오는 용량
};

struct InventoryRecord {
    std::uintptr_t address = 0;      // 레코드 주소
    std::uint32_t slot = 0;          // 배열에서의 칸 번호
    std::uint64_t instance_id = 0;
    std::uint32_t index = 0;         // 아이템 표에서의 순번
    std::uint32_t temper = 0;        // 담금질

    // 장비 연마 (레코드 +0x58). 게임 툴팁의 "장비 연마 100/100" 이다.
    // 상한은 아이템 표의 `_SharpnessData`(+0x2E8).
    std::uint32_t sharpness = 0;

    // 현재 내구도 (레코드 +0x40). **0xFFFF 면 내구도가 없는
    // 아이템**이다 - 아이템 표의 `_maxEndurance` 와 같은 표기다.
    // 갓 지급한 것은 게임이 저장을 한 바퀴 돌기 전까지 0 이다.
    std::uint32_t endurance = 0;
    std::int64_t count = 0;
    std::uintptr_t sockets = 0;      // 소켓 배열 (레코드 +0x60)

    // 배열의 크기 (레코드 +0x68). **아이템의 소켓 칸 수가 아니다** -
    // 게임이 늘 5칸을 잡아 실측이 예외 없이 5다. 용량(+0x6C)도 5다.
    std::uint32_t socket_count = 0;

    // **열린 소켓 칸 수** (레코드 +0x70, u8). 이것이 사람이 보는
    // "이 장비의 소켓 개수" 다. 잠긴 칸은 배열에 남아 있되
    // `raw[4] == 0xFF` 다. 아이템 표의 상한은 `max_sockets`(+0x238).
    std::uint8_t open_sockets = 0;
};

// 소켓 한 칸. 실측 6바이트다.
//
//   24 0D FF FF 00 §§    보석이 박힌 칸 (칸 0)
//   FF FF 00 00 01 §§    열려 있는 빈 칸 (칸 1)
//   FF FF 00 00 FF ??    잠긴 칸 (게임이 [5] 를 안 건드려 쓰레기값)
//
//   +0x00  u16  박힌 것의 아이템 표 순번. 0xFFFF 면 빈 칸
//   +0x02  u16  채움 표시. 보석 있으면 0xFFFF, 비면 0x0000
//   +0x04  u8   **칸 번호(=열림) / 0xFF(=잠김, items.h 의 kSocketLocked)**
//   +0x05  u8   뜻 모름(§§). **고정 상수가 아니다** - 한 판 안에서는
//               열린 칸이 전부 같은데 판이 바뀌면 달라질 수 있다(실측
//               다섯 판: 04 · 05 · 03 · 02 · 02). 로드할 때 게임이 다시
//               매기므로 우리가 쓴 값은 남지 않는다.
//
// 근거: specs/2026-09-07-socket-grant-unlock-research.md 2.2 절.
inline constexpr std::size_t kSocketSize = 6;

struct InventorySocket {
    std::uint32_t slot = 0;
    std::uint16_t index = 0xFFFF;
    std::uint8_t raw[kSocketSize]{};

    bool empty() const { return index == 0xFFFF; }
};

// 소켓 칸이 이보다 많으면 레코드를 잘못 집은 것으로 본다. 실측 5칸이다.
inline constexpr std::uint32_t kMaxSockets = 64;

// 레코드의 소켓 배열을 읽는다. 배열이 없으면 false 다.
bool read_inventory_sockets(const mem::Reader& reader,
                            const InventoryRecord& record,
                            std::vector<InventorySocket>* out);

// 배열 칸 수가 이보다 크면 컨테이너를 잘못 집은 것으로 본다.
// 실측 1,460칸이다.
inline constexpr std::uint32_t kMaxInventorySlots = 1u << 15;

// 컨테이너가 이보다 많으면 컴포넌트를 잘못 집은 것으로 본다.
// 실측 18개다.
inline constexpr std::uint32_t kMaxInventoryContainers = 256;

// `+0x18` 의 포인터 배열을 따라 컨테이너를 모은다. 널 슬롯과 읽기
// 실패한 것은 건너뛴다.
bool read_inventory_containers(const mem::Reader& reader,
                               std::uintptr_t component,
                               std::vector<InventoryContainer>* out);

// 레코드 배열을 통째로 읽어 빈 칸을 뺀 것을 모은다.
//
// `used` 에서 멈추지 않고 칸 전체를 본다. 실측에서 144 라고 하는데
// 실제 레코드는 143개였다 - 중간에 빈 칸이 섞여 있어 그 값으로
// 자르면 뒤가 잘린다.
bool read_inventory_records(const mem::Reader& reader,
                            const InventoryContainer& container,
                            std::vector<InventoryRecord>* out);


// --------------------------------------------------- 모드용 배경 탐색

// 서버 인벤토리 컴포넌트를 RTTI 로 찾아 캐시한다. 찾으면 그다음부터
// 즉시 참을 돌려주므로 재시도 루프에서 부르면 된다.
//
// **내용이 있는 첫 컴포넌트**를 고른다. 빈 것이 여럿 살아 있어서
// (NPC · 상자 등) 그냥 첫 번째를 잡으면 늘 비어 보인다. probe 가
// 쓰는 기준과 같다.
bool discover_inventory(const mem::Rtti& rtti, const mem::Reader& reader);
bool inventory_ready();
std::uintptr_t inventory_component();

// 가방 확장(bag_expand_all)은 되돌렸다. 등록 컨테이너 전부에 무차별로
// 쓰면 임시 버퍼(4개 평행 사본 중 2개)를 건드려 게임이 크래시하고 지급
// 경로까지 손상됐다(2026-09-05 실측). CT 처럼 어느 사본이 안전한지 구조로
// 식별하는 리버스를 마친 뒤 다시 붙인다. 설계는
// docs/superpowers/specs/2026-09-05-player-teleport-port.md 아래에 추가 예정.

// 캐시를 버린다. 게임이 인벤토리를 새로 만들면(재접속 등) 옛 주소가
// 남으므로 화면에서 다시 찾을 수 있어야 한다. 버리면 곧 다시 찾도록
// 요청까지 남긴다 - 한쪽만 부르는 자리를 만들지 않는다.
void forget_inventory();

// "지금 다시 찾아라". RTTI 인스턴스 탐색은 힙 전수라 값싸지 않아
// 배경 루프가 10초에 한 번만 돌린다. 화면에서 다시 찾기를 누른
// 사람에게 그 10초는 "눌렀는데 아무 일도 안 난다" 로 보인다.
// 요청을 남기면 루프가 다음 2초 안에 집어간다.
//
// 탐색에 필요한 `Rtti` 는 배경 루프의 것이다(350MB 이미지를 들고
// 있다). 화면 스레드가 직접 부르지 않고 요청만 남기는 이유다.
void request_inventory_rescan();

// 요청을 집어간다. 남아 있었으면 참이고, 그 즉시 지워진다.
bool take_inventory_rescan();

// --------------------------------------------------------- 표시용 변환

// 인벤토리 한 줄의 표시용 문자열. 규칙을 화면 코드에서 떼어 낸다.
struct InventoryRowText {
    std::string endurance;   // 비면 내구도가 없는 아이템이다
    std::string sharpness;   // 비면 0
    std::string sockets;     // 비면 박힌 것이 없다
};

// 내구도가 없는 아이템은 레코드에 0xFFFF 가 들어 있다. 그대로 내면
// 65535 로 보이므로 빈칸이어야 한다.
inline constexpr std::uint32_t kNoEndurance = 0xFFFF;

InventoryRowText format_inventory_row(std::uint32_t endurance,
                                      std::uint32_t sharpness,
                                      const std::vector<std::string>& gems);

}  // namespace cdtb::game
