#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game/localization.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 아이템의 `_equipAbleHash` 로 걸리는 `EquipTypeInfo` 행들의 부위 이름.
//
// 사슬(명세 §4.7-F, 2026-09-23 R2 실측 확정):
//
//   ItemInfo._equipAbleHash (레코드 +0x68, u32)
//     -> EquipTypeInfoManager(117행) 전 행을 돌며
//        EquipTypeInfo._equipAbleHashList (+0x48, 벡터 {ptr, u32 개수, u32 용량})
//        에 그 해시가 들어 있으면 그 행이 대상이다.
//     -> EquipTypeInfo._equipTypeName (+0x58, LocalString 객체) -
//        **+0x60 에 이미 완성된 현지화 키**가 있다((엔티티 << 32) | 0x2E0,
//        cat 46 · 117개 = 표 행 수와 일치). 엔티티를 따로 뽑을 필요가 없다.
//
// 실측: 관통 I(0xBB5411B9) -> 35행(무기 33종 + 장갑 + 신발),
//       신속 I(0xB1DDF576) -> 4행(투구 · 갑옷 · 장갑 · 신발).
//
// 목록의 [1]번째 항목은 그 행 **자신의 이름 엔티티**다(그룹 해시가 아니다).
// 걸러내지 않는다 - 그 자리를 건너뛰어야 할 만큼의 근거(어떤 아이템의
// `_equipAbleHash` 가 어느 행의 이름 엔티티와 겹치는 사례)를 **실측한 적이
// 없기 때문**이다. 겹치면 엉뚱한 부위가 한 줄 붙는다 - 부위가 이상하게 나오면
// 여기를 먼저 의심하고, 전수로 겹침을 세어 본 뒤에 거른다.
//
// 게임 툴팁은 이 행들을 "무기 · 장갑 · 신발" 세 줄로 접어 보이는데, 접는 규칙은
// 데이터가 아니라 UI 코드에 있어 아직 못 찾았다(§4.7-H 5번). 그래서 우리는
// **접지 않고** 풀린 행 이름을 표 순서 그대로 낸다 - 접힌 라벨 대신 개수가 많으면
// `equip_types_line` 의 "외 N종" 으로 자른다.
//
// 읽기만 한다 - 게임 메모리에 쓰지 않고, 게임 함수도 부르지 않는다.

// 해시 0 이면 빈 목록(장착 제한이 없는 아이템). 매니저 · 현지화가 아직 없으면
// 조용히 빈 목록 - 부르는 쪽이 재시도한다(staged-data-load-retry 함정, 다른
// 모듈과 같다). 개별 행의 실패(이름이 안 풀림)는 그 행만 건너뛴다.
std::vector<std::string> equip_type_names(const mem::Rtti& rtti,
                                          const mem::Reader& reader,
                                          const LocSystem& loc,
                                          std::uint64_t equip_able_hash);

// 같은 일을 매니저 주소를 알 때 한다. RTTI 도 모듈 전역도 안 보므로 가짜
// 메모리로 시험하기 쉽다(`stat_names.h` 의 `build_from_managers` 와 같은 이유).
std::vector<std::string> equip_type_names_from_manager(
    const mem::Reader& reader, const LocSystem& loc, std::uintptr_t manager,
    std::uint64_t equip_able_hash);

// 이름들을 사람이 읽는 한 줄로 붙인다. 구분자는 " · ". 개수가 `max_shown` 을
// 넘으면 처음 `max_shown` 개만 보이고 " 외 N종" 을 붙인다(N = 전체 - max_shown,
// 정확히 `max_shown` 개면 붙지 않는다). `max_shown == 0` 이면 이름 없이
// "N종" 만 낸다. 빈 목록이면 빈 문자열.
std::string equip_types_line(const std::vector<std::string>& names,
                             std::size_t max_shown = 4);

// --- 실측 상수 (명세 §4.7-F) ---

// 매니저 클래스. `find_static_manager` 에 장식된 이름 전체를 준다(부분 일치로
// 주면 엉뚱한 클래스를 잡는다 - stat_names.h 와 같은 이유).
inline constexpr const char* kEquipTypeManagerClass =
    ".?AVEquipTypeInfoManager@pa@@";

// 매니저 머리. 효과 매니저 넷(item_effects.h)과 같은 배치다 - 개수는 `+0x08`,
// 레코드 포인터 배열은 `+0x58`(아이템 매니저의 `+0x30` 개수와는 다르다).
inline constexpr std::size_t kEquipTypeMgrCount = 0x08;
inline constexpr std::size_t kEquipTypeMgrRecords = 0x58;

// EquipTypeInfo 레코드의 `_equipAbleHashList` 벡터 {ptr, u32 개수, u32 용량}.
// 항목은 **u32 해시**(스트라이드 4).
inline constexpr std::size_t kEquipAbleHashList = 0x48;    // 포인터
inline constexpr std::size_t kEquipAbleHashCount = 0x50;   // u32 개수
inline constexpr std::size_t kEquipAbleHashStride = 0x04;  // 항목 = u32 해시

// `_equipTypeName`(+0x58 LocalString 객체)의 **완성된 현지화 키**. 실측
// (명세 §4.7-F): 레코드 +0x60 에 이미 `(엔티티 << 32) | 0x2E0` 값이 들어
// 있어 `loc_key` 로 다시 조립할 필요가 없다.
inline constexpr std::size_t kEquipTypeNameKey = 0x60;

// 위 키가 가리키는 현지화 필드(cat 46, 117개 = 표 행 수). 코드는 완성된 키를
// 그대로 읽으므로 이 값을 조합하는 데 쓰지는 않는다 - 근거를 남겨 두는 문서용.
inline constexpr std::uint32_t kEquipTypeNameField = 0x2E0;

// 해시 목록 길이 상한(실측 최대 40 안팎). 매니저를 잘못 집으면 길이 칸이
// 쓰레기값이므로 그 행만 버리는 안전장치다.
inline constexpr std::uint32_t kMaxEquipHashListLen = 4096;
// 매니저 행 수 상한(실측 117). 마찬가지로 잘못 집었을 때의 안전장치.
inline constexpr std::uint32_t kMaxEquipTypeRows = 1u << 20;

}  // namespace cdtb::game
