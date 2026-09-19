#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 살아 있는 액터(월드에 스폰된 캐릭터·기믹)를 걷고, 각 액터가 어느
// 캐릭터(CharacterInfo)인지 푼다. 전부 읽기다.
//
// 실측 2026-09-05 (specs/2026-09-05-companion-summon-acquire-design.md §9):
//
//   ClientActorManager(RTTI 인스턴스 1개)
//     +0x128 부터 0x10 간격으로 버킷 {포인터 배열 u64, 개수 u32, 용량 u32}.
//     버킷마다 액터 종류가 다르다(개수/용량 예: 130/4000, 40/1000 …).
//     개수 필드(+8)는 살아 있는 수가 아니다(실측: 130 -> 0 으로 바뀌는 동안
//     배열은 212개 그대로). 배열은 빽빽한 포인터 목록이고 끝이 0/쓰레기라,
//     용량까지 힙 포인터로 보이는 동안만 걷는다.
//
//   액터 -> 캐릭터 행 번호 (게임 자신의 접근 함수 RVA 0x17544D0 을 그대로):
//     [[액터 + 0x68] + 0x20] + 0x30  = u16 **CharacterInfoManager 레코드 행 번호**
//     (키가 아니다. 뒤따르는 조회 함수 0x383200 이 이 값을
//     매니저 +0x58 레코드 배열의 인덱스로 쓴다.)
//
//   행 번호 -> 이름·동반자 타입은 roster 의 character_by_row().

struct LiveActor {
    std::uintptr_t actor = 0;
    std::uint32_t handle = 0;     // 액터 핸들. 0 이면 못 찾음
    std::uint16_t row = 0xFFFF;      // 캐릭터 행 번호. 0xFFFF 못 읽음
    std::uint32_t key = 0;           // 캐릭터 키 (row 가 풀렸을 때)
    std::string name;                // 내부 이름
    std::string label;               // 인게임 표시명 (없으면 빈 문자열)
    std::uint16_t merc_row = 0xFFFF; // 동반자 타입 행 번호. 0xFFFF 없음
    bool hirable = false;
    // 이미 이 개체를 고용한 쪽의 핸들. 0 이면 임자가 없다.
    // 0 이 아니면 획득(2338)이 코드 0x97AE29C9 로 거부한다.
    std::uint32_t owner = 0;
    bool is_companion() const { return merc_row != 0xFFFF; }
    // 이미 누군가의 동반자다. 스토리 동료(Damian)와 이미 길들인
    // 전설마가 여기 걸린다.
    bool owned() const { return owner != 0; }
    const std::string& display() const { return label.empty() ? name : label; }
};

inline constexpr std::size_t kActorBucketFirst = 0x128;
inline constexpr std::size_t kActorBucketLast = 0x338;   // 포함
inline constexpr std::size_t kActorBucketStride = 0x10;
inline constexpr std::uint32_t kActorBucketMaxCap = 8192;

// 매니저 후보를 몇 개까지 받아 볼 것인가. **8이면 안 된다** - 후보는 주소
// 순이고, 힙이 위쪽에 잡히는 실행에서는 vtable 값을 우연히 담은 낮은 주소의
// 가짜가 앞자리를 다 차지한다(2026-09-18 실측: 진짜가 11번째였다).
inline constexpr std::size_t kActorManagerCandidates = 256;


// --- 액터 핸들 ---------------------------------------------------------
//
// 핸들은 액터 안에 없다. `ClientActorManager` 의 컨테이너(매니저 +0x08)가
// 핸들 -> 액터 사전을 들고 있다(실측 2026-09-06):
//
//   컨테이너 +0x88 버킷 수 · +0x98 버킷 배열 · +0xA0 노드 포인터 배열
//   버킷 = 0x100 바이트 = { u32 개수, ..., +0x08 부터 {u32 키, u32 색인} 쌍 }
//   노드 = { u32 ?, +0x04 핸들 키, +0x08 액터 포인터 }
//
// 사용자 액터는 0x9010 네임스페이스, 일반 액터는 0xB010 이다.
inline constexpr std::size_t kActorContainerOff = 0x08;
inline constexpr std::size_t kContainerBucketCount = 0x88;
inline constexpr std::size_t kContainerBuckets = 0x98;
inline constexpr std::size_t kContainerNodes = 0xA0;
inline constexpr std::size_t kBucketStride = 0x100;
inline constexpr std::uint32_t kMaxBuckets = 4096;

// 매니저의 핸들 사전을 걸어 {액터 -> 핸들} 을 채운다.
bool read_actor_handles(const mem::Reader& reader, std::uintptr_t manager,
                        std::vector<std::pair<std::uintptr_t, std::uint32_t>>* out);
// 매니저가 진짜인지: 버킷 하나라도 개수 1 이상·용량 상한 이내·첫 포인터가
// 읽히면 된다.
bool looks_like_actor_manager(const mem::Reader& reader, std::uintptr_t manager);

// RTTI 로 ClientActorManager 인스턴스를 찾는다.
bool find_actor_manager(const mem::Reader& reader, const mem::Rtti& rtti,
                        std::uintptr_t* out);

// 버킷을 전부 걸어 액터 포인터를 낸다(중복 제거).
bool walk_actor_pointers(const mem::Reader& reader, std::uintptr_t manager,
                         std::vector<std::uintptr_t>* out);

// 액터의 캐릭터 행 번호. 사슬 어느 단계든 못 읽으면 false.
bool actor_character_row(const mem::Reader& reader, std::uintptr_t actor,
                         std::uint16_t* row_out);

// 액터의 고용주 핸들. 임자가 없으면 0 을 넣고 true.
// 사슬 어느 단계든 못 읽으면 false.
//
//   [[액터 + 0x68] + 0x118] + 0x18 = u32 고용주 핸들
//
// 가운데 객체는 `ClientMercenaryActorComponent` 다(RTTI 확인
// 2026-09-09). 배치는 이렇다.
//
//   +0x10 u32 상태. 0x7F010001 고용됨 / 0x00010001 미고용
//   +0x18 u32 고용주 핸들 (플레이어 쪽은 0xA0100001)
//   +0x20 u32 번호. 미고용은 0xFFFFFFFF
//
// 획득 작업 함수(RVA 0x2ADE280)가 0x2ADE601 에서 이 값을 읽어
// **0 이 아니면 거부**한다 (코드 = 전역 0x6BB8A18 = 0x97AE29C9).
//
// 실측 2026-09-09: 까마귀는 0 으로 통과, 이미 길들인 전설마
// 흑마와 스토리 동료 Damian 은 0xA0100001 로 거부됐다. 앞서
// "고용 불가 유형"으로 적어 둔 것은 틀렸다 - **이미 소유한
// 개체**였다. 사용자가 흑마를 이미 가지고 있다고 확인해 줬다.
bool actor_owner_handle(const mem::Reader& reader, std::uintptr_t actor,
                        std::uint32_t* owner_out);

// 매니저를 걷고 행 번호를 roster 로 풀어 목록을 만든다. roster 가
// 준비되지 않았으면 이름 없이 행 번호만 채운다.
bool snapshot_live_actors(const mem::Reader& reader, std::uintptr_t manager,
                          std::vector<LiveActor>* out);

// --- 모드용 캐시 --------------------------------------------------------
// 매니저를 한 번 찾아 두고(discover), 요청이 있을 때만 다시 걷는다.
bool discover_actor_manager(const mem::Rtti& rtti, const mem::Reader& reader);
bool actor_manager_ready();
// 그 핸들이 지금 살아있는 액터를 가리키는가.
// 매니저를 못 잡았으면 판단하지 않고 false 를 낸다 - 이 함수로
// "죽은 핸들" 을 판정해 지우므로, 모를 때는 건드리지 않는 쪽이 안전하다.
bool actor_handle_alive(const mem::Reader& reader, std::uint32_t handle,
                        bool* known_out);

// 살아 있는 핸들을 **한 번에** 모은 것.
//
// `actor_handle_alive` 는 부를 때마다 액터 해시표를 통째로 다시 읽는다.
// 한 번 물을 때는 그래도 되지만 **여러 번 물어야 하면 안 된다** - 명부의
// 죽은 핸들 정리가 항목마다 그것을 불렀다(실측 2026-09-19: 핸들 달린 항목이
// 4개라 한 바퀴에 표를 네 번 다시 읽었다). 여러 번 물 자리에서는 이것으로
// 한 번 모아 두고 `actor_handle_in_set` 으로 본다.
//
// **이것이 그때 본 2.9초 멈춤의 원인이라는 근거는 없다** - 네 번으로는 규모가
// 안 맞는다. 구조가 틀려서 고친 것이고, 원인은 아직 열려 있다
// (`TROUBLESHOOTING.md` 2.16 의 정정).
struct ActorHandleSet {
    // 오름차순·중복 없음. 이분 탐색으로 본다.
    std::vector<std::uint32_t> handles;
    // 표를 실제로 읽었는가. **거짓이면 아무것도 판정하지 않는다** - 이 값으로
    // 죽은 핸들을 지우므로, 모를 때 "죽었다" 로 읽으면 살아 있는 개체의 소환
    // 판정을 지워 중복 소환이 된다(`actor_handle_alive` 의 `known_out` 계약).
    bool known = false;
};
// 매니저를 직접 주고 모은다(시험·프로브용).
bool actor_handle_set(const mem::Reader& reader, std::uintptr_t manager,
                      ActorHandleSet* out);
// 캐시된 매니저로 모은다. 매니저를 못 잡았으면 `known = false`.
bool actor_handle_set_cached(const mem::Reader& reader, ActorHandleSet* out);
// 모은 것 안에서 본다. **읽기가 없다.**
bool actor_handle_in_set(const ActorHandleSet& set, std::uint32_t handle);
// 지금 걷는다. **그리는 스레드만** 부른다(버튼/주기/아래 tick). 실패하면 false
// 이고 옛 판 유지. 다른 스레드가 갈아 끼우면 그리는 쪽이 쥔 원소 포인터가
// 매달린다(Codex 지적 2026-09-11).
bool refresh_live_actors(const mem::Reader& reader);
// 그리는 스레드용 참조. 다음 refresh 까지만 유효하다.
const std::vector<LiveActor>& live_actors();
// 갱신 세대. refresh 가 성공할 때마다 1 오른다 - 뷰 캐시 키로 쓴다.
std::uint64_t live_actors_generation();
// 다른 스레드(명령 파일)는 직접 걷지 않고 갱신을 부탁한다. 렌더 스레드가 다음
// 프레임의 live_actors_tick 에서 걷고 세대를 올린다.
void live_actors_request_refresh();
void live_actors_tick(const mem::Reader& reader);
// 다른 스레드용 복사본(뮤텍스 아래에서 복사).
std::vector<LiveActor> live_actors_copy();


// 이 모듈이 힙에서 찾는 RTTI 클래스(통과 단위 미리 훑기용, mem/rtti.h prefetch_instances).
std::vector<std::string> actors_scan_classes();

}  // namespace cdtb::game
