#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 게임의 아이템 표(`pa::ItemInfoManager`)를 읽는다. 전부 읽기다.
//
// 실측 구조 (docs/superpowers/specs/2026-09-01-item-table.md):
//
//   매니저 +0x28  색인 표 포인터 ((u32 키, u32 용도미상) 쌍)
//          +0x30  u32 개수 / +0x34 u32 용량
//          +0x58  레코드 포인터 배열 (8바이트 항목)
//   레코드 +0x00  u32 키
//          +0x28  u64 이름 현지화 키
//
// 이름 키를 (키 << 32) | 0x70 으로 조립하지 않는다. 레코드가 게임
// 자신의 값을 +0x28 에 그대로 들고 있으므로 그것을 쓴다.

struct ItemEntry {
    std::uint32_t key = 0;
    std::uint64_t name_key = 0;   // 레코드가 들고 있는 현지화 키
    std::uintptr_t record = 0;
};

// 개수가 이보다 크면 매니저를 잘못 집은 것으로 본다. 실측 6,810개다.
inline constexpr std::uint32_t kMaxItemCount = 1u << 20;

// 색인 표와 레코드 배열이 같은 키를 말하는지 본다.
//
// RTTI 후보가 진짜인지 판별하는 용도다. 카메라에서 vtable 값을 우연히
// 담은 메모리를 후보로 집어 11회 어긋난 적이 있어, 구조 자체의
// 앞뒤가 맞는지로 확인한다.
bool looks_like_item_manager(const mem::Reader& reader,
                             std::uintptr_t manager);

// RTTI 로 살아 있는 매니저를 찾는다. looks_like_item_manager 를
// 통과하는 첫 후보를 쓴다.
bool find_item_manager(const mem::Rtti& rtti, const mem::Reader& reader,
                       std::uintptr_t* out);

// 레코드 포인터 배열을 걸어 항목을 모은다. max 가 0 이면 전부.
// 널 슬롯이나 읽기 실패한 레코드는 건너뛴다.
bool read_item_table(const mem::Reader& reader, std::uintptr_t manager,
                     std::vector<ItemEntry>* out, std::size_t max);

}  // namespace cdtb::game
