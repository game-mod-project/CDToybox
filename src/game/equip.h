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

struct WornPiece {
    std::uintptr_t entry = 0;
    std::uint64_t instance = 0;   // +0x00, both-realms 매칭 키
    std::uint32_t key = 0;        // +0x08 하위16, 아이템 순번
    std::uint16_t refine = 0;     // +0x0A
    std::uint16_t slot_tag = 0;   // +(stride-8)
    int unlocked = 0;             // 열린 소켓 수
    WornSocket sockets[5]{};      // entry+0x60 벡터
};

// 착용 장비 목록을 읽는다(빈 슬롯 제외). 실패면 false.
bool read_worn_gear(const mem::Reader& reader, const EquipTable& t,
                    std::vector<WornPiece>* out);

// entry+0x60 소켓 벡터에서 열린 소켓 수. 잠김(+4==0xFF)에서 멈춘다. 벡터가
// 아니면 -1.
int socket_unlocked(const mem::Reader& reader, std::uintptr_t entry);

}  // namespace cdtb::game
