#pragma once

#include <cstdint>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 출시 빌드에 남은 개발자 치트 경로를 쓴다. 인벤토리 구조를 몰라도
// 되고 서버가 자기 경로로 처리하므로 슬롯 배치·저장·UI 갱신이 전부
// 딸려 온다. 자세한 추적 과정은
// docs/superpowers/specs/2026-09-01-cheat-messages.md 참조.

// 세션에서 플레이어 액터를 꺼내는 게임 함수를 찾는다. 이미지 안에서
// 유일하게 맞아야 한다 - 두 곳에서 맞으면 실패로 다룬다. 엉뚱한
// 함수를 후킹하면 게임이 죽는다.
bool find_actor_getter_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out);

// 바닥에 아이템을 떨구는 실제 작업 함수를 찾는다.
bool find_spawn_ground_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out);

// 액터 조회 함수를 후킹한다. 그 함수는 게임 안에서 647곳이 부르므로
// 가만 두어도 곧 값이 들어온다 - 우리가 세션에서 액터를 꺼내는
// 복잡한 경로를 흉내 낼 필요가 없다. 훅은 값을 적어 두기만 하고
// 원본을 그대로 부른다.
bool actor_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
void actor_hook_remove();
bool actor_hook_installed();

// 본 적 있으면 그 자리의 횟수를 늘리고, 처음이면 자리를 잡는다.
// 자리가 없으면 그대로 둔다. 돌려주는 것은 개수다.
int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value);

// 조회 함수가 지금까지 돌려준 서로 다른 값들과 각각의 호출 횟수.
// 클라이언트 쪽과 서버 쪽 인벤토리 컴포넌트가 둘 다 나오고, 서버
// 쪽만 해도 여럿이다(NPC·상자 등). 플레이어 것은 게임플레이 코드가
// 계속 부르므로 횟수가 압도적으로 많다 - 그걸로 가린다.
int seen_actors(std::uintptr_t* out, std::uint32_t* hits_out, int cap);

// 마지막으로 본 액터. 아직 못 봤으면 0.
std::uintptr_t last_actor();

// 부르기 전에 게임이 하는 검사를 우리도 한다. 게임 코드에 그대로
// 있다 - 키가 0이거나 개수가 0 이하면 게임이 실패로 돌려준다.
bool spawn_args_ok(std::uint32_t item_key, std::int64_t count);

// 아이템을 발밑 바닥에 떨군다. 인벤토리에서 버리기와 같은 루틴이라
// 게임이 평소에도 도는 경로다.
//
// **반드시 게임 스레드에서 불러야 한다.** 렌더 훅이 그 스레드다.
//
// actor 가 0 이면 마지막으로 본 액터를 쓴다. result_out 에는 게임이
// 낸 코드가 들어간다 - 0 이면 성공이다. 부르지 못했으면 false.
bool spawn_item_to_ground(std::uintptr_t actor, std::uint32_t item_key,
                          std::int64_t count, const float pos[3],
                          std::uint32_t* result_out);

// 스폰 함수를 찾아 둔다. 못 찾으면 spawn_item_to_ground 는 항상
// false 를 돌려준다.
bool spawn_resolve(const mem::Rtti& rtti, const mem::Reader& reader);

}  // namespace cdtb::game
