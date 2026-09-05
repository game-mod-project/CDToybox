#pragma once

#include <cstdint>
#include <string>
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
    std::uint16_t row = 0xFFFF;      // 캐릭터 행 번호. 0xFFFF 못 읽음
    std::uint32_t key = 0;           // 캐릭터 키 (row 가 풀렸을 때)
    std::string name;                // 내부 이름
    std::uint16_t merc_row = 0xFFFF; // 동반자 타입 행 번호. 0xFFFF 없음
    bool hirable = false;
    bool is_companion() const { return merc_row != 0xFFFF; }
};

inline constexpr std::size_t kActorBucketFirst = 0x128;
inline constexpr std::size_t kActorBucketLast = 0x338;   // 포함
inline constexpr std::size_t kActorBucketStride = 0x10;
inline constexpr std::uint32_t kActorBucketMaxCap = 8192;

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

// 매니저를 걷고 행 번호를 roster 로 풀어 목록을 만든다. roster 가
// 준비되지 않았으면 이름 없이 행 번호만 채운다.
bool snapshot_live_actors(const mem::Reader& reader, std::uintptr_t manager,
                          std::vector<LiveActor>* out);

// --- 모드용 캐시 --------------------------------------------------------
// 매니저를 한 번 찾아 두고(discover), 요청이 있을 때만 다시 걷는다.
bool discover_actor_manager(const mem::Rtti& rtti, const mem::Reader& reader);
bool actor_manager_ready();
// 지금 걷는다. 그리는 쪽이 버튼/주기로 부른다. 실패하면 false 이고 옛 판 유지.
bool refresh_live_actors(const mem::Reader& reader);
const std::vector<LiveActor>& live_actors();

}  // namespace cdtb::game
