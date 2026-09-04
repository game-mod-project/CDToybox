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
//   매니저 +0x28  색인 표 포인터
//          +0x30  u32 개수
//          +0x58  레코드 포인터 배열 (8바이트 항목)
//
// **레코드 구조는 아이템과 다르다** (실측 2026-09-04):
//   레코드 +0x00  **u16 키** (VehicleKey/MercenaryKey/CharacterKey 는
//                 전부 타입 G = u16). 아이템은 u32 였다.
//          +0x08  _stringKey - 엔진 문자열 객체 **포인터**. 아이템은
//                 여기가 u64 현지화 키였는데, 이 표들은 포인터다.
//
// 엔진 문자열 객체:
//   +0x00  char*  UTF-8 데이터 (내부 이름, 예 "CarmabirdSaurusWarMachine")
//   +0x08  u32    길이
//
// **담긴 것은 내부 이름이지 한글 표시명이 아니다.** 한글 이름은 이
// 문자열 풀 근처에 있으나 _stringKey 가 직접 가리키지 않는다 - 현지화
// 경로가 아이템과 달라 별도 조사가 필요하다(Tier 1b).

struct RosterEntry {
    std::uint32_t key = 0;   // u16 키를 담는다
    std::string name;        // 내부 이름 (빈 문자열이면 못 읽음)
};

// 캐릭터 표가 가장 크다. 개수가 이보다 크면 매니저를 잘못 집은 것.
inline constexpr std::uint32_t kMaxRosterCount = 1u << 21;

// 엔진 문자열 객체에서 UTF-8 내부 이름을 읽는다. 못 읽으면 빈 문자열.
std::string read_engine_string(const mem::Reader& reader,
                               std::uintptr_t string_obj);

// 매니저가 진짜인지 본다 - 색인 표의 첫 키와 첫 레코드의 키가 같아야
// 한다. 아이템 매니저 검증과 같은 방식이다.
bool looks_like_static_manager(const mem::Reader& reader,
                               std::uintptr_t manager);

// 주어진 매니저 클래스(`.?AVVehicleInfoManager@pa@@` 등)의 살아 있는
// 인스턴스를 RTTI 로 찾아, 레코드를 걷고 내부 이름을 붙인다.
bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class,
                          std::vector<RosterEntry>* out);

// 배경에서 세 카탈로그를 만들어 캐시한다. 아이템 표처럼 게임이
// 현지화보다 먼저 올릴 수 있어, 현지화가 올라온 뒤 한 번 더 만들어야
// 이름이 채워진다. 준비 전에는 조용히 false - 재시도 루프에서 부른다.
bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader);

// 세 카탈로그가 모두 준비됐는가. 준비되면 내부 이름까지 채워져 있다.
bool roster_ready();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<RosterEntry>& vehicle_catalog();
const std::vector<RosterEntry>& mercenary_catalog();
const std::vector<RosterEntry>& character_catalog();

}  // namespace cdtb::game
