#pragma once

#include <cstdint>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

// 플레이어 스탯 블록(체력·스태미나·정신력)의 읽기/발견 계층. B-1 치트
// (Godmode·무한 자원)의 기반. 값은 전부 x1000 (시트 1650 → 메모리 1650000).
//
// 출처: XeTrinityz(=ReXooGen)/Trinity (MIT). Nexus 3209 CT 실측 참고.
// 설계: docs/superpowers/specs/2026-09-05-player-teleport-port.md.
//
// 이 헤더는 **읽기/발견 전용**이다. 쓰기(freeze)는 라이브 검증 뒤에 붙인다.

namespace cdtb::game {

// 스탯 블록 오프셋 (CT 실측, 확정). 쌍은 0x10 간격.
//   체력    cur +0x08  / max +0x18   (CT 가 0x518 로 오인했다가 0x08 로 정정)
//   스태미나 cur +0x6C8 / max +0x6D8
//   정신력  cur +0x758 / max +0x768
inline constexpr std::size_t kHpCur = 0x08;
inline constexpr std::size_t kHpMax = 0x18;
inline constexpr std::size_t kStaCur = 0x6C8;
inline constexpr std::size_t kStaMax = 0x6D8;
inline constexpr std::size_t kSpiCur = 0x758;
inline constexpr std::size_t kSpiMax = 0x768;

struct StatPool {
    std::int32_t cur = 0;
    std::int32_t max = 0;
};

struct StatBlock {
    std::uintptr_t addr = 0;
    std::uintptr_t actor = 0;   // 이 블록을 가진 액터(백참조 출처)
    StatPool health;
    StatPool stamina;
    StatPool spirit;
};

// r+cur / r+max 가 그럴듯한 자원 쌍인가. max 는 0<max<=50,000,000,
// cur 는 0<=cur<=max. (값 x1000)
bool stat_pair_ok(const mem::Reader& reader, std::uintptr_t r, std::size_t cur,
                  std::size_t max);

// 스태미나 + 정신력 쌍을 둘 다 요구한다. 적/NPC 는 정신력 풀이 없어
// 걸러진다(CT 의 _looks 와 같은 판별).
bool looks_like_stat_block(const mem::Reader& reader, std::uintptr_t r);

// 0x90 그리드에 유효 자원 풀이 몇 개 늘어서는가(0..16). 진짜 상태 블록
// 판별의 핵심 신호.
int stat_grid_run(const mem::Reader& reader, std::uintptr_t r);

// 착용 장비 컴포넌트(EquipSlotActorComponent)의 액터 백참조(comp+0x08)에서
// 출발해, 액터가 가리키는 포인터를 훑어 _looks 를 만족하는 스탯 블록을
// 모은다. 플레이어 것은 값이 캐릭터 시트 x1000 과 맞는 블록이다.
// (MGRCHAIN G 체인이 이 빌드에서 안 잡혀 equip 과 같은 RTTI 경로를 쓴다.)
int find_stat_blocks(const mem::Rtti& rtti, const mem::Reader& reader,
                     std::vector<StatBlock>* out, int max_hits);

// 한 액터에서 스탯 블록 하나를 찾는다(첫 히트). 없으면 0.
std::uintptr_t stat_block_in_actor(const mem::Reader& reader,
                                   std::uintptr_t actor);

// 블록의 세 쌍을 읽어 채운다. 실패면 false.
bool read_stat_block(const mem::Reader& reader, StatBlock* b);

}  // namespace cdtb::game
