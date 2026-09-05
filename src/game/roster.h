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
//   레코드 +0x00  **u16 키** (VehicleKey/CharacterKey 는 타입 G = u16).
//                 아이템은 u32 였다. **용병 표는 여기가 포인터**라 키로
//                 못 쓴다 - 행 번호가 키 노릇을 한다(아래).
//          +0x08  _stringKey - 엔진 문자열 객체 **포인터**.
//
// 엔진 문자열 객체:
//   +0x00  char*  UTF-8 데이터 (내부 이름, 예 "CarmabirdSaurusWarMachine")
//   +0x08  u32    길이
//
// **담긴 것은 내부 이름이지 한글 표시명이 아니다.**
//
// 캐릭터 표의 동반자 필드 (실측 2026-09-05,
// specs/2026-09-05-catchable-companions.md):
//   +0xBE  u16 _mercenaryInfo   - **MercenaryInfo 레코드 행 번호**. 0xFFFF 없음.
//                                 이 값이 있어야 탈것·펫·가축 등 동반자다.
//   +0x148 u8  _isCatchable      - 거의 모든 캐릭터가 1 (붙잡기 공용).
//   +0x14B u8  _isUnique
//   +0x156 u8  _isHirable        - 고용(소유 등록) 가능.
//
// 용병(MercenaryInfo) 표 21행 (실측 2026-09-05):
//   +0x08  _stringKey ("Vehicle_Horse", "Pet", "Wagon" …)
//   +0x20  u8 _mercenaryType   1 본대, 2 탈것, 3 마차, 4 펫, 5 가축(어류·곤충 포함),
//                              6~12 사람 용병·관찰자·회복
//   행 번호가 캐릭터 표 +0xBE 와 맞는다. 매니저 검증(색인 첫 키 == 첫
//   레코드 키)은 +0x00 이 포인터라 통과 못 하므로 느슨한 판정을 쓴다.

struct RosterEntry {
    std::uint32_t key = 0;   // u16 키를 담는다. 용병 표는 행 번호.
    std::string name;        // 내부 이름 (빈 문자열이면 못 읽음)

    // 캐릭터 표 전용. 다른 표는 기본값이다.
    std::uint16_t merc_row = 0xFFFF;  // MercenaryInfo 행 번호. 0xFFFF 없음
    bool hirable = false;
    bool catchable = false;
    bool unique = false;

    // 용병 표 전용.
    std::uint8_t merc_type = 0;  // _mercenaryType

    bool is_companion() const { return merc_row != 0xFFFF; }
};

enum class RosterKind { Vehicle, Mercenary, Character };

// 캐릭터 표가 가장 크다. 개수가 이보다 크면 매니저를 잘못 집은 것.
inline constexpr std::uint32_t kMaxRosterCount = 1u << 21;
// 용병 표는 작다. 느슨한 판정의 상한.
inline constexpr std::uint32_t kMaxMercenaryRows = 256;

// _mercenaryType 값. 실측 21행에서 얻었다.
inline constexpr std::uint8_t kMercTypeMain = 1;
inline constexpr std::uint8_t kMercTypeVehicle = 2;
inline constexpr std::uint8_t kMercTypeWagon = 3;
inline constexpr std::uint8_t kMercTypePet = 4;
inline constexpr std::uint8_t kMercTypeDomestic = 5;

// 탈것·마차·펫·가축 - "동반자" 로 다루는 용병 타입인가.
constexpr bool is_companion_merc_type(std::uint8_t t) {
    return t >= kMercTypeVehicle && t <= kMercTypeDomestic;
}

// 엔진 문자열 객체에서 UTF-8 내부 이름을 읽는다. 못 읽으면 빈 문자열.
std::string read_engine_string(const mem::Reader& reader,
                               std::uintptr_t string_obj);

// 매니저가 진짜인지 본다 - 색인 표의 첫 키와 첫 레코드의 키가 같아야
// 한다. 아이템 매니저 검증과 같은 방식이다. 용병 표는 통과 못 한다.
bool looks_like_static_manager(const mem::Reader& reader,
                               std::uintptr_t manager);

// 용병 표용 느슨한 판정: 개수가 작고, 첫 레코드의 +0x08 문자열이 읽힌다.
bool looks_like_mercenary_manager(const mem::Reader& reader,
                                  std::uintptr_t manager);

// 주어진 클래스의 살아 있는 매니저(looks_like 통과)를 찾는다.
bool find_static_manager(const mem::Reader& reader, const mem::Rtti& rtti,
                         const char* manager_class, std::uintptr_t* out);

// 매니저의 개수와 레코드 배열을 낸다(진단용).
bool roster_header(const mem::Reader& reader, std::uintptr_t manager,
                   std::uint32_t* count, std::uintptr_t* records);

// 매니저 주소를 알 때 레코드를 걷는다. kind 에 따라 키·부가 필드를 읽는다.
// RTTI 없이 돌아 시험이 쉽다.
bool build_catalog_from_manager(const mem::Reader& reader,
                                std::uintptr_t manager, RosterKind kind,
                                std::vector<RosterEntry>* out);

// 주어진 매니저 클래스(`.?AVVehicleInfoManager@pa@@` 등)의 살아 있는
// 인스턴스를 RTTI 로 찾아, 레코드를 걷고 내부 이름을 붙인다.
// (탈것·캐릭터용. 용병은 build_mercenary_catalog.)
bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class,
                          std::vector<RosterEntry>* out);
bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, RosterKind kind,
                          std::vector<RosterEntry>* out);

// 내부 이름 규칙 도우미. 포획 대상 판별에 쓴다
// (specs/2026-09-05-catchable-companions.md "읽는 법").
bool roster_is_wild(const std::string& name);       // "_Wild" 가 든다
std::string roster_species(const std::string& name); // 접미사 뗀 종 이름

// 배경에서 카탈로그를 만들어 캐시한다. 준비 전에는 조용히 false -
// 재시도 루프에서 부른다.
bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader);

// 탈것·캐릭터가 준비됐는가. 용병 표는 있으면 좋고 없어도 준비로 친다.
bool roster_ready();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<RosterEntry>& vehicle_catalog();
const std::vector<RosterEntry>& mercenary_catalog();
const std::vector<RosterEntry>& character_catalog();

// 용병 표 행 번호 -> 내부 이름. 모르면 빈 문자열.
const std::string& mercenary_type_name(std::uint16_t row);
// 행 번호 -> _mercenaryType. 모르면 0.
std::uint8_t mercenary_type_of_row(std::uint16_t row);

}  // namespace cdtb::game
