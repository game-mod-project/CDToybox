#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 게임의 현지화 표를 읽어 이름을 푼다.
//
// 게임 자신의 조회 함수(0x1410d7230)를 부르지 않는다. 그 함수는
// [시스템+0x18] 잠금을 잡고, 키를 못 찾으면 항목을 새로 만들어 표에
// 끼워 넣는다 - 읽기인 줄 알고 부른 것이 게임 상태를 바꾼다.
// 여기서는 같은 자료 구조를 읽기만 하며 걷는다.
//
// 근거: docs/superpowers/specs/2026-09-01-localization-lookup.md

// 64비트 현지화 키 = (엔티티키 << 32) | 필드번호.
//
// 아직 가설이다. 표본 3개(아이템 2200/50001)에서 얻었고 화면과
// 대조하지 않았다. 위 문서 6절을 볼 것.
constexpr std::uint64_t loc_key(std::uint32_t entity, std::uint32_t field) {
    return (static_cast<std::uint64_t>(entity) << 32) | field;
}
constexpr std::uint32_t loc_key_entity(std::uint64_t key) {
    return static_cast<std::uint32_t>(key >> 32);
}
constexpr std::uint32_t loc_key_field(std::uint64_t key) {
    return static_cast<std::uint32_t>(key & 0xFFFFFFFFull);
}

// 이름으로 추정하는 필드 번호. 0x71 은 그 다음 칸(설명으로 추정).
inline constexpr std::uint32_t kLocFieldName = 0x70;

// 게임이 카테고리를 0x36 미만으로 검사한다(cmp al, 0x36).
inline constexpr int kLocCategoryCount = 0x36;

struct LocSystem {
    std::uintptr_t object = 0;      // 현지화 시스템 객체
    std::uintptr_t global = 0;      // 그것을 담은 전역 슬롯 (진단용)
    std::uintptr_t pool = 0;        // 문자열 풀 기준 (시스템+0x58)
    std::uint32_t pool_size = 0;    // 풀 크기 (시스템+0x60)

    bool valid() const { return object != 0 && pool != 0 && pool_size != 0; }
};

// 모듈 이미지에서 현지화 전역의 RVA 를 찾는다.
//
// LocalizationStringBase::str() 본문 28바이트를 패턴으로 쓴다. RVA 를
// 코드에 박으면 게임이 갱신될 때 깨지므로 매번 찾는다. 일치가 두 곳
// 이상이면 무엇을 고를지 알 수 없으므로 실패로 친다.
bool find_loc_global_rva(const std::vector<std::uint8_t>& image,
                         std::uint64_t* rva_out);

// 전역을 따라가 살아 있는 시스템 객체와 문자열 풀을 얻는다.
bool find_loc_system(const mem::Rtti& rtti, const mem::Reader& reader,
                     LocSystem* out);

struct LocCategory {
    std::uintptr_t array = 0;   // 포인터 배열 (키 오름차순)
    std::uint32_t count = 0;
};

// 카테고리 표를 통째로 읽는다. 성공하면 항상 kLocCategoryCount 개다.
// 어느 카테고리에 무엇이 들었는지 보는 진단용.
bool loc_categories(const mem::Reader& reader, const LocSystem& sys,
                    std::vector<LocCategory>* out);

// 카테고리 0..kLocCategoryCount-1 을 훑어 키를 이분 탐색하고 문자열을
// 읽는다. category_out 은 널이어도 된다.
bool resolve(const mem::Reader& reader, const LocSystem& sys,
             std::uint64_t key, std::string* text_out, int* category_out);

}  // namespace cdtb::game
