#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/localization.h"
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
    std::uint8_t grade = 0;       // 0=없음, 1..5 = T1..T5
    std::uint8_t category = 0;    // 74종. 이름은 아직 못 붙였다
};

// 등급 표시 이름. 표 밖의 값은 "?" 다.
const char* grade_label(std::uint8_t grade);

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

// --------------------------------------------------------------- 목록

struct ItemCatalogEntry {
    std::uint32_t key = 0;
    std::uint64_t name_key = 0;
    std::string name;             // 빈 문자열이면 현지화 표에 없는 것
    std::uint8_t grade = 0;
    std::uint8_t category = 0;
};

// 표를 걷고 이름까지 붙인다. sys 가 비어 있으면(valid() 아님) 이름
// 없이 키만 채운다.
//
// 이름이 안 풀린 항목도 목록에서 빼지 않는다. 실측에서 6,810개 중
// 72개가 그랬는데, 키는 있는 아이템이므로 지급 대상이 될 수 있다.
bool build_item_catalog(const mem::Reader& reader, std::uintptr_t manager,
                        const LocSystem& sys,
                        std::vector<ItemCatalogEntry>* out);

// --------------------------------------------------- 모드용 배경 탐색

// 이미 이미지를 읽어 둔 Rtti 로 목록을 만들어 캐시한다. 350MB 이미지
// 읽기와 힙 전수 조사를 두 번 하지 않도록, 이미 그것을 한 배경
// 스레드가 호출자다.
//
// 표가 아직 안 올라왔으면 조용히 false 다. 재시도 루프에서 부르면
// 된다 - 이미 준비됐으면 즉시 true 로 빠진다.
// 목록을 (다시) 만들어야 하는가.
//
// 게임은 아이템 표를 현지화보다 먼저 올린다. 그 순간에 만들고 굳으면
// 이름이 영영 빈다 - 실제로 그렇게 났다. 현지화가 올라온 뒤 한 번
// 더 만들어야 한다.
bool should_rebuild_catalog(bool have_catalog, bool names_resolved,
                            bool loc_available);

// 이름까지 풀린 목록을 만들었으면 true. 아직이면 다시 부르면 된다.
bool discover_items(const mem::Rtti& rtti, const mem::Reader& reader);

// 이름이 실제로 풀렸는가.
bool items_named();

// 캐시가 준비됐는가. 준비된 뒤에는 목록이 다시 바뀌지 않는다.
bool items_ready();

// 준비되기 전에 부르면 빈 목록이다.
const std::vector<ItemCatalogEntry>& item_catalog();

}  // namespace cdtb::game
