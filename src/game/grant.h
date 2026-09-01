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

// 마지막으로 본 액터. 아직 못 봤으면 0.
std::uintptr_t last_actor();

}  // namespace cdtb::game
