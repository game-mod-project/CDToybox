#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/localization.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 탈것·용병·캐릭터 카탈로그를 읽는다. 전부 읽기다.
//
// 이 표들은 아이템 표와 같은 `StaticInfoManager2` 컨테이너다
// (docs/superpowers/specs/2026-09-04-vehicle-pet-review.md). 컨테이너
// 배치는 `ItemInfoManager` 와 같다:
//
//   매니저 +0x28  색인 표 포인터 ((u32 키, u32 …) 쌍)
//          +0x30  u32 개수
//          +0x58  레코드 포인터 배열 (8바이트 항목)
//
// 레코드 안 이름 키 오프셋만 아이템과 다르다. `tools/rtti/fields.py`
// 로 세 표를 뽑으니 전부 `_stringKey` 가 +0x08 이었다(아이템은 +0x28).
//   레코드 +0x00  u32 키
//          +0x08  u64 _stringKey (이름 현지화 키)

struct RosterEntry {
    std::uint32_t key = 0;
    std::uint64_t name_key = 0;   // 레코드 +0x08 _stringKey
    std::string name;             // 빈 문자열이면 현지화 표에 없는 것
};

// 캐릭터 표가 가장 크다. 개수가 이보다 크면 매니저를 잘못 집은 것.
inline constexpr std::uint32_t kMaxRosterCount = 1u << 21;

// 매니저가 진짜인지 본다 - 색인 표의 첫 키와 첫 레코드의 키가 같아야
// 한다. 아이템 매니저 검증과 같은 방식이다.
bool looks_like_static_manager(const mem::Reader& reader,
                               std::uintptr_t manager);

// 주어진 매니저 클래스(`.?AVVehicleInfoManager@pa@@` 등)의 살아 있는
// 인스턴스를 RTTI 로 찾아, 레코드를 걷고 이름을 붙인다. sys 가
// 유효하지 않으면 이름 없이 키만 채운다.
bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, const LocSystem& sys,
                          std::vector<RosterEntry>* out);

// 배경에서 세 카탈로그를 만들어 캐시한다. 아이템 표처럼 게임이
// 현지화보다 먼저 올릴 수 있어, 현지화가 올라온 뒤 한 번 더 만들어야
// 이름이 채워진다. 준비 전에는 조용히 false - 재시도 루프에서 부른다.
bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader);

// 세 카탈로그가 모두 준비됐는가.
bool roster_ready();

// 이름이 실제로 풀렸는가.
bool roster_named();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<RosterEntry>& vehicle_catalog();
const std::vector<RosterEntry>& mercenary_catalog();
const std::vector<RosterEntry>& character_catalog();

}  // namespace cdtb::game
