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
inline constexpr std::size_t kClanRecordOwner = 0x148;  // u16 소유자 캐릭터 행
                                                       // (게임 판정 RVA 0x209FE40)
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

    // 레코드 +0x148 u16 = **소유자의 캐릭터 행 번호**. 0xFFFF 면 없음.
    //
    // 게임 자신의 소유자 판정 함수(RVA 0x209FE40)에서 확정했다
    // (실측 2026-09-09). 그 함수는 종(+0x20)으로 CharacterInfo 를 찾아
    // `_mercenaryInfo`(+0xBE) -> MercenaryInfo 의 `_summonOwnerOption`
    // (+0x5D) 을 보고, 분류에 따라 이 자리를 비교한다.
    //
    //   0  -> 무조건 거짓
    //   1  -> 넘긴 행 == 주인공 행 이어야 하고, 그다음 +0x148 == 주인공 행
    //   2  -> +0x148 == 주인공 행
    //   3  -> +0x148 == 넘긴 행
    //
    // 어느 분류든 **비교 대상은 +0x148 하나**다. 그래서 이 값이
    // 소유자다. 스토리 동료가 명부에 섞여 보이는 이유도 여기서
    // 갈릴 것으로 본다.
    std::uint16_t owner_row = 0xFFFF;
    std::string owner_name;   // owner_row 를 캐릭터 표로 푼 이름
    // 그 소유자가 플레이어블 캐릭터인가 (주인공이거나 Mercenary_Main).
    // 클리프·데미안·웅카가 여기 걸린다(roster.h is_playable_character_row).
    bool owner_playable = false;

    // 여기 "야생 획득분" 표시(레코드 +0x30~+0x4F 가 0 이 아닌가)가
    // 있었다. 2026-09-09 명부 8개를 실측해 보니 그 구간은 출신과
    // 아무 상관이 없었다 - 야생에서 잡은 까마귀·멧새·대형늑대는 전부
    // 0 이고, 야생이 아닌 사자(종 교체분)와 전설마 카모라는 0 이
    // 아니다. 한 필드가 아니라 여러 필드가 섞인 구간이다. 지웠다.


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

// 획득 뒤처리. **그리는 스레드(on_frame)에서 돈다** - 여기서 쓰는 시간이 곧
// 화면 멈춤이다. 할 일이 없으면 바로 나오고, 있을 때도 표를 다시 읽는 것은
// 한 번뿐이어야 한다.
//
// 한때 이 자리에 "매 프레임 불러도 싸다" 고만 적혀 있었다. **조기 반환 갈래만
// 보고 쓴 말이었다** - 일할 때는 명부 항목마다 액터 해시표를 통째로 다시 읽어
// 880개에서 2.9초가 걸렸다(TROUBLESHOOTING 2.16). 여기 손대면 `Slow` 가 찍는
// `느림: 획득 뒤처리` 를 다시 본다.
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
// 아직 못 찾았으면 배경 스레드에서 다시 찾게 한다(그리는 스레드는 멈추지 않는다).
// reader 는 재탐색이 끝날 때까지 살아 있어야 한다(정적 리더). 분석 스레드가 아직
// RTTI 를 넘겨 준 적이 없으면 false.
bool clan_request_discovery(const mem::Reader& reader);

// 캐시가 상했을 때 **배경 스레드**에서 다시 찾게 한다. 모드(DLL)만
// 켠다 - 그리는 스레드가 10초 넘게 멈추면 안 되기 때문이다. probe 는
// 켜지 않는다(짧게 살고, 그 자리에서 훑어도 된다).
void enable_background_clan_rescan();
// 지금 읽는다. 실패하면 false 이고 옛 판을 유지한다.
bool refresh_clan_roster(const mem::Reader& reader);
const std::vector<ClanEntry>& clan_roster();
// discover_clan 이 쓴 RTTI. 준비 전이면 nullptr.
// 오버레이가 resolve_species_write 를 부르려면 이것이 필요하다.
const mem::Rtti* clan_rtti();

// 종을 바꿔 쓴다(클라·서버 양쪽). 주소는 그 자리에서 다시 찾는다 - 들고 있다가
// 쓰면 안 된다(2026-09-09 사고). 쓰기 로그는 여기서 남긴다. msg 에 화면 문구.
// msg 는 널이면 안 된다.
enum class SpeciesApply { Ok, NoRtti, NoTarget, WriteFailed, VerifyMismatch };
SpeciesApply apply_species(const mem::Reader& reader, std::uint64_t merc_no,
                           std::uint16_t row, std::string* msg);

// 클라이언트 쪽 명부를 걷는다. 서버와 따로 가지고 있으므로
// 둘을 비교하면 ‘서버에는 들어갔는데 클라가 모른다’ 를 잡을 수 있다.
// 획득 직후 소환이 먹통이 되는 증상의 유력 후보다(조사 2026-09-09).
bool find_clan_component_client(const mem::Reader& reader, const mem::Rtti& rtti,
                                std::uintptr_t* out);


// 캐시된 용병단 컴포넌트. **값싼 길이다** - `find_clan_component` 는 RTTI 힙
// 스캔(10초대)이라 렌더 스레드에서 부르면 화면이 그만큼 멈춘다. 캐시가 비어
// 있으면 배경 재탐색을 걸고 false 를 준다(이번 프레임은 포기).
bool clan_component_fast(const mem::Reader& reader, const mem::Rtti& rtti,
                         bool client, std::uintptr_t* out);

// ------------------------------------------- 휠 색인 고치기 (2026-09-15)
//
// **종 교체분이 휠에서만 안 불린다**(사용자 실측 2026-09-15). 목록은 번호로 가니
// 정상인데, 휠은 **종류별 색인**(용병단 안의 해시)을 훑어 목록을 만들기 때문이다.
// 그 색인의 버킷 키는 **명부에 넣을 때의 종**에서 파생되므로, 종만 제자리에서
// 바꾸면 개체가 **옛 타입 벡터에 남는다.**
//
// 실측으로 눈에 보였다(`probe clanindex`):
//
//   타입키 9(펫)  개수 7/용량 8   [3] 번호 1000595 종행 6810 **타입행 5** 붉은깃 랩터
//   타입키 5(특수) 개수 13/용량 18                                       <- 여기 있어야 한다
//
// 지금까지의 해결책은 "저장하고 다시 불러오기"(명부 재구축)였다. 그러지 않고
// **제자리로 옮긴다.** 벡터에 여유 칸이 있으면 재할당도 게임 함수 호출도 필요 없다.
//
// 배치(실측 2026-09-12·09-15):
//   M = clan + 0x18
//   M+0x30 u32 버킷 수 · M+0x40 버킷표(버킷 0x100 간격) · M+0x48 슬롯 배열
//   버킷: [0] u32 개수, +8 부터 {u32 타입키, u32 슬롯색인}
//   슬롯 원소 +8 = 벡터 {begin, u32 개수, u32 용량}, 원소는 `record*`
inline constexpr std::size_t kClanIndexObj = 0x18;
inline constexpr std::size_t kClanIdxBuckets = 0x30;   // M 기준
inline constexpr std::size_t kClanIdxTable = 0x40;
inline constexpr std::size_t kClanIdxSlots = 0x48;
inline constexpr std::size_t kClanBucketStride = 0x100;
inline constexpr std::uint32_t kClanBucketMax = 31;
inline constexpr std::uint32_t kClanIdxMaxBuckets = 4096;
inline constexpr std::size_t kClanVecOff = 0x08;       // 원소 기준
inline constexpr std::uint32_t kClanVecMax = 256;

struct ReindexResult {
    int realms = 0;    // 손댄 realm 수
    int wrong = 0;     // 자리가 틀린 개체 수
    int moved = 0;     // 실제로 옮긴 수
    int no_room = 0;   // 갈 벡터에 여유가 없어 못 옮긴 수
    // **한쪽 realm 을 못 봤다.** 이때 `wrong 0` 은 "어긋난 것이 없다" 가 아니라
    // "거기는 안 봤다" 다. 캐시에 없는 명부는 배경 탐색을 걸고 건너뛰므로
    // (렌더 스레드를 10초 멈추지 않으려고) 흔히 일어난다 - 2026-09-15 에
    // 드래곤 휠 칸을 바꾼 직후가 정확히 그랬고, 깨끗한 0 으로 보고됐다.
    bool partial = false;
    char note[128] = {};
};

// **순수 함수.** 여유가 있어야 옮긴다. 용량이 개수보다 커야 한 칸 들어간다.
bool reindex_has_room(std::uint32_t count, std::uint32_t cap);

// 종류별 색인에서 **타입행과 자리가 어긋난 개체**를 제자리로 옮긴다.
// 서버·클라 양쪽에 건다. 읽기만 하려면 `dry` 를 참으로 준다.
ReindexResult clan_reindex(const mem::Reader& reader, const mem::Rtti& rtti,
                           bool dry);

// 이 모듈이 힙에서 찾는 RTTI 클래스(통과 단위 미리 훑기용, mem/rtti.h prefetch_instances).
std::vector<std::string> clan_scan_classes();

}  // namespace cdtb::game
