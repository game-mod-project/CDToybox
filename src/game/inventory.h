#pragma once

#include <cstdint>
#include <vector>

#include "mem/reader.h"

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

    // 현재 내구도 (레코드 +0x40). **0xFFFF 면 내구도가 없는
    // 아이템**이다 - 아이템 표의 `_maxEndurance` 와 같은 표기다.
    // 갓 지급한 것은 게임이 저장을 한 바퀴 돌기 전까지 0 이다.
    std::uint32_t endurance = 0;
    std::int64_t count = 0;
    std::uintptr_t sockets = 0;      // 소켓 배열 (레코드 +0x60)
    std::uint32_t socket_count = 0;  // 소켓 칸 수 (실측 5)
};

// 소켓 한 칸. 실측 6바이트다.
//
//   FF FF 00 00 FF 03    빈 칸 (첫 칸)
//   FF FF 00 00 FF 00    빈 칸 (나머지)
//
//   +0x00  u16  박힌 것의 아이템 표 순번. 0xFFFF 면 빈 칸
//   +0x02  u16  뜻 모름 (실측 전부 0)
//   +0x04  u8   뜻 모름 (실측 전부 0xFF)
//   +0x05  u8   뜻 모름 (첫 칸만 3, 나머지 0)
//
// 뜻을 다 모르므로 원본 바이트를 그대로 들고 있는다. export 는 모르는
// 칸까지 되돌려야 하기 때문이다.
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

}  // namespace cdtb::game
