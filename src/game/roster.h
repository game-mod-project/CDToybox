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
    // 현지화 엔티티 키. 레코드 +0x00 의 **u32 전체**다.
    //
    // 예전에는 이 자리를 u16 으로 잘라 `key` 로만 썼고, 그것으로 이름을
    // 찾다 빗나가면 내부 이름 끝자리 숫자로 다시 찾았다. 그 폴백이
    // 엉뚱한 이름을 붙였다(새끼 와이번 -> "클리프").
    //
    // 진짜 키는 `_characterName`(+0x18)이 들고 있다 - 그 객체의 +0x10 이
    // 필드 번호(0x30), +0x14 가 엔티티 키이고, 그 값이 레코드 +0x00 의
    // u32 와 같다(실측 2026-09-09).
    //
    //   새끼 와이번  u16 21005  u32 1004557
    //   늑대         u16 17969  u32 1004081
    //   사자         u16 20752  u32 1004304
    std::uint32_t loc_entity = 0;
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
    // `_equipInfo` (+0x6A). **탈것 표 연결이 아니다** - 그렇게 적어
    // 뒀던 것은 틀렸다(이름 확정 2026-09-09, tools/rtti/fields.py).
    // 0xFFFF 면 장비 정보가 없다.
    //
    // 관찰: 낙타(3583)·성체 와이번(4214)은 0xFFFF 이고 게임 목록
    // 어디에도 안 떴다. 사자(3436) 9, 새끼 와이번(6608) 10,
    // Riding_Wyvern_1000(6804) 9 는 전부 보인다. **상관은 있으나
    // 인과는 확인 안 됐다** - 아래 owned_merc_row 와 견줘 볼 것.
    std::uint16_t equip_info = 0xFFFF;

    // `_ownedMercenaryCharacterInfo` (+0x100). 야생판 종이 가리키는
    // **소유판 캐릭터 행**. 0xFFFF 면 없다. 종을 고를 때 야생판 대신
    // 이쪽을 골라야 하는지 판정할 후보다.
    std::uint16_t owned_merc_row = 0xFFFF;
    // `_isMercenaryCountAble` (+0x16E). 동반자 수에 세는가.
    bool merc_countable = false;

    // 게임의 소환 표에 이 키가 있는가. 캐릭터 표 전용.
    // 소환 치트(2988)가 키를 이 표에서 찾고, 없으면 오류를 낸다.
    bool spawnable = false;
    // 용병 표 전용.
    std::uint8_t merc_type = 0;  // _mercenaryType
    // 용병 표 전용. `_isPlayable` (+0x22).
    //
    // **"플레이어블 캐릭터"가 아니라 "플레이어가 조종하는가"다** -
    // 실측 2026-09-09: Mercenary_Main 뿐 아니라 Vehicle_Horse·
    // Vehicle_Dragon·Vehicle_WarMachine·Vehicle_Special·Vehicle 까지
    // 전부 1이고, Vehicle_Ship 과 펫·가축·용병은 0이다. 즉 탈것 여부에
    // 가깝다. 플레이어블 캐릭터는 _mercenaryType 으로 가린다.
    bool merc_playable = false;

    bool is_companion() const { return merc_row != 0xFFFF; }
    // 게임이 목록에 올려 주는 종인가. 종을 고를 때의 기준이다.
    //
    // 한때 여기에 `_equipInfo != 0xFFFF` 를 함께 걸고 "탈것 표 연결이
    // 없으면 게임 목록에 안 뜬다" 고 적어 뒀는데, 둘 다 틀렸다.
    // 이름은 _equipInfo 이고(fields.py 확정), 안 뜨는 진짜 이유는
    // **동반자 타입**이었다 - 낙타(3583)도 성체 와이번(4214)도 타입행 6
    // (Vehicle)이라 애초에 1/5/9 필터가 거른다. 성체 와이번은
    // _equipInfo 가 9 인데도 안 뜬다.
    //
    // 전수 집계(probe gate, 2026-09-09): 타입행 1·9 는 _equipInfo 가
    // 없는 것이 0건, 타입행 5 는 63중 4건뿐이다. 그 4건은 열기구 3종과
    // 호랑이(7038) - 숨길 이유가 없다. 조건을 뺐다.
    bool listable() const { return is_companion(); }
};

// ----------------------------------------------------------------------
// 소환 표
//
// 소환 치트 처리기(RVA 0x2B6E530)가 캐릭터 키를 찾는 해시 표다. 여기
// 없는 키는 소환되지 않는다 - 어느 종이 되는지 하나씩 눌러 볼 필요가
// 없다. 표를 통째로 읽어 카탈로그에 표시한다.
//
//   CharacterInfoManager 인스턴스(카탈로그를 만들 때 RTTI 로 찾는 그 객체) -> 표
//     +0x68 u32 버킷 수      +0x6C u32 (0 이면 비어 있음)
//     +0x78 버킷 배열        버킷 하나가 0x100 바이트
//     버킷: [0] u32 항목 수, +8 부터 {u32 키, u32 색인} 쌍
//
// 2760 까지는 고정 전역 0x6C29FF8 에서 이 포인터를 읽었다. 2850 갱신에서 +0x4150 으로
// 옮긴 0x6C2E148 은 핸들 은행(0x0001000D 같은 값)이라 실패했고, 실제 전역은 0x6C2E288
// (+0x4290 - 프로브로 [전역] == RTTI 인스턴스 0x49EDCED3340 확인, 2026-09-11)이었다.
// 2차 리뷰가 경고한 대로 데이터 구간의 이동 폭은 고르지 않다. 그래서 상수를 없애고
// 카탈로그를 만들 때 찾은 인스턴스에서 바로 읽는다 - 갱신에 안 밀린다. 버킷 수·항목
// 수가 말이 되는지 보고, 아니면 조용히 포기한다(표시만 비고 기능은 산다).
inline constexpr std::size_t kSpawnBucketStride = 0x100;
inline constexpr std::uint32_t kSpawnBucketMaxEntries = 31;  // (0x100-8)/8
inline constexpr std::uint32_t kSpawnMaxBuckets = 4096;

// 소환 표의 키를 전부 읽는다. manager 는 CharacterInfoManager 인스턴스. 정렬된
// 채로 돌려준다. 못 읽으면 false.
bool read_spawn_table(const mem::Reader& reader, std::uintptr_t manager,
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
// manager_out 을 주면 찾은 매니저 인스턴스 주소를 넣어 준다(소환 표 읽기에 쓴다 -
// 힙을 다시 훑지 않으려고).
bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, RosterKind kind,
                          std::vector<RosterEntry>* out,
                          std::uintptr_t* manager_out = nullptr);

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

// 카탈로그를 만들 때 RTTI 로 찾은 `CharacterInfoManager` 인스턴스. 아직 못 찾았으면
// 0. **고정 전역을 쓰지 말고 이것을 쓴다** - 데이터 전역은 갱신마다 고르지 않게
// 밀린다(위 소환 표 주석). 레코드를 직접 봐야 하는 기능이 이것으로 표에 닿는다.
std::uintptr_t roster_char_manager();

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
// 그 용병 타입이 **플레이어블 캐릭터**인가.
//
// 기준은 `_mercenaryType == 1`(Mercenary_Main)이다. 이름을 박지 않고
// 데이터로 가리므로 DLC·모드로 늘어도 따라간다. `_isPlayable` 은 탈것도
// 1이라 이 용도로 못 쓴다(merc_playable 설명).
bool is_playable_merc_row(std::uint16_t row);

// 명부를 세 갈래로 접는다. 기준은 `_mercenaryType` 이다(용병 표 21행을
// 실측해 얻은 대응, 2026-09-10).
//
//   People  사람.   1 Mercenary_Main(플레이어블) · 6 근접 · 7 원거리
//                   · 8 일꾼 · 9 상점 · 12 객원
//                   -> 플레이어블 캐릭터들이 속한 **용병대**의 멤버다.
//   Mount   탈것·펫. 2 탈것 · 3 마차 · 4 펫 · 5 가축(어류·곤충 포함)
//   System  시스템.  10 관찰자(WorldObserver) · 11 회복
//                   -> 플레이어 동반자가 아니다. 기본으로 접어 둔다.
enum class CompanionGroup { People, Mount, System, Unknown };
CompanionGroup companion_group_of_row(std::uint16_t merc_row);

// --- 플레이어블 캐릭터 -------------------------------------------------
//
// 실측 2026-09-09. 두 갈래로 갈린다.
//
//  1) **주인공 클리프**는 용병 표에 없다(행 0 `Kliff`, _mercenaryInfo
//     0xFFFF). 그래서 타입으로는 안 잡힌다. 대신 게임이 소유자 판정
//     (RVA 0x209FE40)에서 쓰는 사슬로 **지금 조종 중인 캐릭터 행**을
//     읽는다.
//
//       전역 0x6C29AF8 (또는 0x6C29B00) -> [+0] -> [+8] -> [+0x28]
//         -> +0x100 u16 = 캐릭터 행
//
//  2) 바꿔 탈 수 있는 나머지는 `_mercenaryType == 1`(Mercenary_Main)이다.
//     지금 6행: 데미안(3)·웅카(5)·얀(6·7)·나이라(8)·마녀(9). 이름을
//     박지 않으므로 DLC·모드로 늘어도 따라간다.
// 1.0.0.2850: +0x4150 (2760 까지 0x6C29AF8 / 0x6C29B00). 행이 표 밖이면 무시된다.
inline constexpr std::uint64_t kSessionGlobalRvaA = 0x6C2DC48;
inline constexpr std::uint64_t kSessionGlobalRvaB = 0x6C2DC50;
inline constexpr std::size_t kSessionCharRowOff = 0x100;

// 지금 조종 중인 캐릭터의 행. 못 읽으면 0xFFFF.
std::uint16_t main_character_row(const mem::Reader& reader);

// 주인공 행. 세션 전역 사슬이 2850 갱신에서 끊겨 main_character_row 가 0xFFFF 를 돌려주는
// 동안에도 주인공은 플레이어블이어야 하므로 표의 첫 행으로 본다 - 실측: 사슬이 살아 있던
// 2760 까지 값 0, 2850 에서도 장비 컴포넌트의 캐릭터 사슬로 조각 최다(21) 캐릭터가 행 0
// = Kliff(2026-09-12). 이름을 박는 것이 아니라 표의 첫 행이라는 구조에 기댄다.
inline constexpr std::uint16_t kProtagonistRow = 0;

// 그 캐릭터 행이 플레이어블인가 (주인공이거나 Mercenary_Main). 주인공은 조종 중 사슬의
// 값이거나 kProtagonistRow - 웅카를 조종하는 동안에도 클리프는 플레이어블이다.
bool is_playable_character_row(const mem::Reader& reader, std::uint32_t row);


// 이 모듈이 힙에서 찾는 RTTI 클래스(통과 단위 미리 훑기용, mem/rtti.h prefetch_instances).
std::vector<std::string> roster_scan_classes();

}  // namespace cdtb::game
