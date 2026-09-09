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
    std::uint32_t row = 0;   // 레코드 포인터 배열의 행 번호. 액터가 이 값으로 참조한다.
    std::string name;        // 내부 이름 (빈 문자열이면 못 읽음)
    std::string label;       // 인게임 표시명 (없는 행도 있다 - 그때는 빈 문자열)

    // 보여 줄 이름. 표시명이 있으면 그것, 없으면 내부 이름.
    const std::string& display() const {
        return label.empty() ? name : label;
    }

    // 캐릭터 표 전용. 다른 표는 기본값이다.
    std::uint16_t merc_row = 0xFFFF;  // MercenaryInfo 행 번호. 0xFFFF 없음
    bool hirable = false;
    bool catchable = false;
    bool unique = false;
    // 탈것(VehicleInfo) 표 연결. 0xFFFF 면 없다.
    //
    // **이 값이 없으면 게임 목록 어디에도 안 뜼다** - 실측 2026-09-09:
    // 낙타(3583)·성체 와이번(4214)은 0xFFFF 이고 탈것·특수·반려동물
    // 어느 목록에도, 캐릭터 선택창에도 없었다. 사자(3436)는 9,
    // 새끼 와이번(6608)은 10, Riding_Wyvern_1000(6804)은 9 로 전부 보인다.
    std::uint16_t vehicle_link = 0xFFFF;

    // 게임의 소환 표에 이 키가 있는가. 캐릭터 표 전용.
    // 소환 치트(2988)가 키를 이 표에서 찾고, 없으면 오류를 낸다.
    bool spawnable = false;
    // 용병 표 전용.
    std::uint8_t merc_type = 0;  // _mercenaryType

    bool is_companion() const { return merc_row != 0xFFFF; }
    // 게임이 목록에 올려 주는 종인가. 종을 고를 때의 기준이다.
    bool listable() const { return is_companion() && vehicle_link != 0xFFFF; }
};

// ----------------------------------------------------------------------
// 소환 표
//
// 소환 치트 처리기(RVA 0x2B6E530)가 캐릭터 키를 찾는 해시 표다. 여기
// 없는 키는 소환되지 않는다 - 어느 종이 되는지 하나씩 눌러 볼 필요가
// 없다. 표를 통째로 읽어 카탈로그에 표시한다.
//
//   전역 RVA 0x6C29FF8 -> 표 포인터
//     +0x68 u32 버킷 수      +0x6C u32 (0 이면 비어 있음)
//     +0x78 버킷 배열        버킷 하나가 0x100 바이트
//     버킷: [0] u32 항목 수, +8 부터 {u32 키, u32 색인} 쌍
//
// 하드코딩된 주소라 패치마다 어긋날 수 있다. 버킷 수·항목 수가
// 말이 되는지 보고, 아니면 조용히 포기한다(표시만 비고 기능은 산다).
inline constexpr std::uint64_t kSpawnTableGlobalRva = 0x6C29FF8;
inline constexpr std::size_t kSpawnBucketStride = 0x100;
inline constexpr std::uint32_t kSpawnBucketMaxEntries = 31;  // (0x100-8)/8
inline constexpr std::uint32_t kSpawnMaxBuckets = 4096;

// 소환 표의 키를 전부 읽는다. 정렬된 채로 돌려준다. 못 읽으면 false.
bool read_spawn_table(const mem::Reader& reader,
                      std::vector<std::uint32_t>* out);

// 캐릭터의 인게임 표시명이 든 현지화 필드.
//
// 실측 2026-09-06: 엔티티 30048(Animal_Bear_Wild_30048) 을 훑으니
// 0x30 [cat 3] '곰', 0x31 'Animal_Bear', 0x32 '탈것 등록' 이었다.
// 0x30 이 표시명이다. 다만 모든 행에 있지는 않다 - 키 20955
// (Animal_Wolf_Wild_30020) 는 어느 필드에도 없었다. 없으면 내부
// 이름으로 물러난다.
inline constexpr std::uint32_t kCharNameField = 0x30;

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
// 게임이 목록으로 보여 주는 세 갈래.
//
// 명부에는 들어가도 게임 화면 어디에도 안 뜨는 타입이 있다 -
// 실측 2026-09-09: 성체 와이번(`Animal_Wyvern_30508`, 타입행 6)으로
// 바꾸자 탈것·특수 탑승물·반려동물 어느 목록에도 없었다.
// 새끼 와이번(행 6608)은 타입행 9 라 잘 보인다.
//
// 그래서 목록·고르기는 이 셋만 보여 준다. 안 보이는 타입으로
// 바꾸면 되돌리기 전까지 게임에서 찾을 수가 없다.
inline constexpr std::uint16_t kMercRowHorse = 1;     // 탈것(말)
inline constexpr std::uint16_t kMercRowSpecial = 5;   // 특수 탑승물
inline constexpr std::uint16_t kMercRowPet = 9;       // 반려동물

constexpr bool is_listed_companion_row(std::uint16_t row) {
    return row == kMercRowHorse || row == kMercRowSpecial || row == kMercRowPet;
}

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

// 내부 이름 끝의 숫자. 없으면 0.
//
// 현지화 엔티티가 레코드 키와 다른 행이 많다 - 실측 2026-09-06:
// Animal_Parrot_Wild_32884 는 엔티티 32884 에 '스픽스마코 앵무새'가
// 있는데 레코드 키는 그 값이 아니라 조회가 빗나갔다.
// Animal_Wolf_Wild_30019 의 키가 17969 인 것도 같은 어긋남이다.
std::uint32_t roster_name_suffix(const std::string& name);

// 표시명을 채운다. 현지화에 없는 행은 건드리지 않는다(내부 이름으로
// 물러난다). 카탈로그 생성과 나눠 둔 이유는, 생성 쪽은 가짜 메모리로
// 시험하는데 현지화까지 끌고 들어가면 시험이 무거워지기 때문이다.
// 채운 개수를 돌려준다.
std::size_t apply_roster_labels(const mem::Reader& reader, const LocSystem& sys,
                                std::vector<RosterEntry>* entries);

// 배경에서 카탈로그를 만들어 캐시한다. 준비 전에는 조용히 false -
// 재시도 루프에서 부른다.
bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader);

// 탈것·캐릭터가 준비됐는가. 용병 표는 있으면 좋고 없어도 준비로 친다.
bool roster_ready();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<RosterEntry>& vehicle_catalog();
const std::vector<RosterEntry>& mercenary_catalog();
const std::vector<RosterEntry>& character_catalog();

// 캐릭터 표 행 번호(레코드 배열 인덱스) -> 항목. 없으면 널. 살아 있는
// 액터가 캐릭터를 이 행 번호로 가리킨다(game/actors.h).
const RosterEntry* character_by_row(std::uint32_t row);

// 용병 표 행 번호 -> 내부 이름. 모르면 빈 문자열.
const std::string& mercenary_type_name(std::uint16_t row);
// 행 번호 -> _mercenaryType. 모르면 0.
std::uint8_t mercenary_type_of_row(std::uint16_t row);

}  // namespace cdtb::game
