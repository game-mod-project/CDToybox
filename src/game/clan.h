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

    // 레코드 +0x30~+0x4F 에 값이 남아 있는가.
    //
    // 근처 획득(2338)으로 야생 개체를 들이면 그 개체의 값(float 여럿)이
    // 여기 남는다. 부적·정상 경로로 들어온 레코드는 전부 0 이다.
    //
    // **종을 바꿀 때 이게 차있으면 소환이 게임을 죽인다** - 실측
    // 2026-09-09: 야생 성체 와이번을 획득해 Riding_Wyvern_1000 으로
    // 바꾸니 목록에는 떴는데 고르자 팀겼고, 같은 종으로 바꾸더라도
    // 부적로 얻은 Riding_Bear_1001(이 구간이 0)은 소환·탑승까지 잘 됐다.
    bool wild_origin = false;


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


// --- 종 바꿔 쓰기 (자리 해석까지만) ------------------------------
//
// **명부 레코드 주소는 휘발성이다.** 동반자가 하나라도 늘거나
// 줄면 게임이 용병단 컴포넌트와 레코드를 통째로 새로 만든다 -
// 실측 2026-09-09: 작업 중에 사용자가 용병 네을 고용하자 22 -> 26 이
// 되면서 컴포넌트 주소까지 바뀜고(0x58172223480 -> 0x581C51A9300),
// 옆던 레코드 자리는 `GameData_GimmickPointData` 가 차지했다. 그것을
// 모르고 예전 주소에 되돌리기를 써서 **남의 객체에 2바이트를
// 썼다.**
//
// 그래서 이 모듈은 주소를 돌려주기만 하고, 부를 때마다 **번호로
// 다시 찾아** 표식(+0x22 == 0xFFFF)과 번호(+0x28)를 확인한다. 불러 두고
// 나중에 쓰면 같은 사고가 다시 난다 - 쓰기 직전에 부를 것.
//
// 쓰기 자체는 호출자가 한다. DLL 은 제 주소 공간이고 probe 는
// WriteProcessMemory 라 공통 추상화가 없기 때문이다.
struct SpeciesWriteTarget {
    std::uintptr_t server = 0;      // 서버 레코드의 +0x20 주소. 0 이면 못 찾음
    std::uintptr_t client = 0;      // 클라 레코드의 +0x20 주소
    std::uint16_t server_row = 0xFFFF;
    std::uint16_t client_row = 0xFFFF;
    bool ok() const { return server != 0 && client != 0; }
};

// 번호로 두 세계의 종 필드 주소를 그 자리에서 찾는다.
//
// 클라·서버 양쪽에 써야 한다. 서버 쪽만 바꾸면 우리 눈에는 바뀌어
// 보이지만 게임이 보는 사본은 그대로다(실측 2026-09-09).
bool resolve_species_write(const mem::Rtti& rtti, const mem::Reader& reader,
                           std::uint64_t merc_no, SpeciesWriteTarget* out);

// --- 소환 판정 바로잡기 --------------------------------------------
//
// 레코드 `+0x50` 은 이 동반자가 지금 쓰고 있는 액터 핸들이고,
// 게임은 **그 값이 0 이 아니면 ‘이미 소환됨’ 으로 본다.**
//
// 근처 획득(2338)은 월드에 서 있던 야생 개체를 그대로 등록하므로
// 그 순간의 핸들이 여기 박힌다. 그 액터가 사라진 뒤에도 값은 남아
// **실제로는 없는데 있는 것으로 판정된다** - 그래서 소환도 해제도
// 먹지 않는다. 지역 이동·세이브 로드가 풀어 주던 것이 이것이다
// (사용자 증상 + 실측 2026-09-09: 명부는 [월드] 인데 살아있는 액터
//  250개 어디에도 그 개체가 없었다).
struct SpawnFlagTarget {
    std::uintptr_t server = 0;      // 서버 레코드의 +0x50 주소
    std::uintptr_t client = 0;      // 클라 레코드의 +0x50 주소
    std::uint32_t server_handle = 0;
    std::uint32_t client_handle = 0;
    bool ok() const { return server != 0 && client != 0; }
};

// 번호로 두 세계의 핸들 자리를 그 자리에서 찾는다. 규칙은
// resolve_species_write 와 같다 - 주소를 들고 있다가 쓰지 않는다.
bool resolve_spawn_flag(const mem::Rtti& rtti, const mem::Reader& reader,
                        std::uint64_t merc_no, SpawnFlagTarget* out);

// 획득 뒤처리. 매 프레임 불러도 싼다 - 할 일이 없으면 바로 나온다.
//
// 2338 획득이 남긴 죽은 액터 핸들을 지운다. 살아있는 핸들은 건드리지
// 않는다 - 진짜로 나와 있는 개체의 판정을 지우면 중복 소환이 된다.
// 액터 목록을 모를 때도 건드리지 않는다.
//
// 핸들이 사라지는 데 시간이 걸리므로 응답 뒤 잠시 기다렸다 본다.
void tick_hire_cleanup(const mem::Rtti& rtti, const mem::Reader& reader);
// 응답 뒤 이만큼 지나야 본다.
inline constexpr unsigned long long kHireCleanupDelayMs = 3000;

// --- 모드용 캐시 --------------------------------------------------------
// 액터 매니저와 같은 방식이다. 컴포넌트를 한 번 찾아 두고, 요청이
// 있을 때만 다시 읽는다. 월드를 나가면 컴포넌트가 바뀔 수 있으므로
// 읽기가 실패하면 다시 찾는다.
bool discover_clan(const mem::Rtti& rtti, const mem::Reader& reader);
bool clan_ready();
// 지금 읽는다. 실패하면 false 이고 옛 판을 유지한다.
bool refresh_clan_roster(const mem::Reader& reader);
const std::vector<ClanEntry>& clan_roster();
// discover_clan 이 쓴 RTTI. 준비 전이면 nullptr.
// 오버레이가 resolve_species_write 를 부르려면 이것이 필요하다.
const mem::Rtti* clan_rtti();

// 클라이언트 쪽 명부를 걷는다. 서버와 따로 가지고 있으므로
// 둘을 비교하면 ‘서버에는 들어갔는데 클라가 모른다’ 를 잡을 수 있다.
// 획득 직후 소환이 먹통이 되는 증상의 유력 후보다(조사 2026-09-09).
bool find_clan_component_client(const mem::Reader& reader, const mem::Rtti& rtti,
                                std::uintptr_t* out);

}  // namespace cdtb::game
