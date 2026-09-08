#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 내가 가진 동반자 명부 (탈것·특수탈것·반려동물·사람 용병).
//
// 실측 2026-09-09 (specs/2026-09-08-companion-catalog-and-acquire-research.md).
// 세이브 구조체(`MercenarySaveData`)는 플레이 중에 살아 있지 않다 -
// 저장·적재 순간에만 만들어진다. 런타임 명부는 용병단 컴포넌트에 산다.
//
//   ServerMercenaryClanActorComponent
//     +0x18  레코드 포인터 배열
//     +0x20  u32 개수      +0x24 u32 용량
//     +0xF8  타입별 한도 배열 (+0x100 개수) - 여기가 아니다
//     +0x108 소환 중인 번호 배열 (+0x110 개수) - 여기도 아니다
//
//   레코드 0x1C0 바이트
//     +0x20  u16 **캐릭터 표 행 번호** = 종
//     +0x22  u16 0xFFFF (전 레코드 공통, 판별에 쓴다)
//     +0x28  u64 MercenaryNo (부르기 2894 가 쓰는 번호)
//     +0x50  u32 액터 핸들. 월드에 나와 있지 않으면 0
//
// 검증: 22개 레코드의 행·번호가 근처 목록의 소유 액터 7개와 1:1 로
// 맞았다(흑마 행 4074/번호 0x4809, Damian 행 3, Luke 행 6861 …).

inline constexpr std::size_t kClanRecordsOff = 0x18;
inline constexpr std::size_t kClanCountOff = 0x20;
inline constexpr std::size_t kClanCapacityOff = 0x24;
inline constexpr std::size_t kClanRecordRow = 0x20;
inline constexpr std::size_t kClanRecordGuard = 0x22;
inline constexpr std::size_t kClanRecordNo = 0x28;
inline constexpr std::size_t kClanRecordHandle = 0x50;
// 명부가 이보다 크면 컴포넌트를 잘못 집은 것이다.
inline constexpr std::uint32_t kClanMaxRecords = 4096;

struct ClanEntry {
    std::uintptr_t record = 0;
    std::uint16_t row = 0xFFFF;   // 캐릭터 표 행 번호 = 종
    std::uint64_t merc_no = 0;
    std::uint32_t handle = 0;     // 0 이면 월드에 안 나와 있다

    // roster 로 푼 것. 행을 못 풀면 비어 있다.
    std::uint32_t key = 0;
    std::string name;             // 내부 이름
    std::string label;            // 인게임 표시명
    std::uint16_t merc_row = 0xFFFF;  // 동반자 타입 행

    bool spawned() const { return handle != 0; }
    const std::string& display() const { return label.empty() ? name : label; }
};

// 컴포넌트가 진짜인지 본다 - 개수·용량이 말이 되고 첫 레코드의
// +0x22 가 0xFFFF 여야 한다.
bool looks_like_clan_component(const mem::Reader& reader, std::uintptr_t clan);

// RTTI 로 용병단 컴포넌트를 찾는다. 살아있지 않은 후보가 섞여 있으므로
// 명부를 가장 많이 든 것을 고른다.
bool find_clan_component(const mem::Reader& reader, const mem::Rtti& rtti,
                         std::uintptr_t* out);

// 명부를 읽는다. 이름은 roster 가 준비돼 있을 때만 붙는다.
bool read_clan_roster(const mem::Reader& reader, std::uintptr_t clan,
                      std::vector<ClanEntry>* out);


// --- 모드용 캐시 --------------------------------------------------------
// 액터 매니저와 같은 방식이다. 컴포넌트를 한 번 찾아 두고, 요청이
// 있을 때만 다시 읽는다. 월드를 나가면 컴포넌트가 바뀔 수 있으므로
// 읽기가 실패하면 다시 찾는다.
bool discover_clan(const mem::Rtti& rtti, const mem::Reader& reader);
bool clan_ready();
// 지금 읽는다. 실패하면 false 이고 옛 판을 유지한다.
bool refresh_clan_roster(const mem::Reader& reader);
const std::vector<ClanEntry>& clan_roster();

}  // namespace cdtb::game
