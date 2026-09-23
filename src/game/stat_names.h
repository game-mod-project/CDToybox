#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "game/localization.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 효과 문구의 `{Staticinfo:SubLevel:Hp}` 를 `생명` 으로 바꿀 이름표.
//
// 치환 사슬(명세 §4.7-H' 1번, 2026-09-23 실측 확정):
//
//   {Staticinfo:SubLevel:<키>} -> SubLevelInfo(40행)에서 `_stringKey == <키>` 인 행
//                              -> 그 행 `_knowledgeInfo`(+0x46) = 지식 행 번호
//   {Staticinfo:Status:<키>}   -> StatusInfo(84행)에서 같은 방식,
//                                 지식은 `_activeKnowledgeInfo`(+0x34)
//   지식 행 -> KnowledgeInfo `+0x08` = 엔진 문자열 객체 {char*, u32 길이, u32 해시}
//           -> 그 **해시**를 엔티티로 현지화 필드 0x490(cat 9) = 표시 이름
//
// 실측 표본: Hp->생명 · Mp->용기 · Stamina->기력 · AttackSpeedRate->공격 속도 ·
// CriticalRate->치명타 확률 · IceResistance->냉기 저항.
//
// **지식 행 0 은 "없음" 이 아니라 유효한 행이다**(Knowledge_Hp). 없음은 0xFFFF 다.
//
// 읽기만 한다 - 게임 메모리에 쓰지 않고, 게임 함수도 부르지 않는다
// (현지화 조회 함수를 부르면 안 되는 이유는 localization.h 머리 주석).
//
// **스레드:** `build` 는 표를 통째로 갈아 끼우므로 다른 스레드가 `name_of` 를
// 부르는 동안 부르면 안 된다. 배경 스레드에서 만든 뒤 **게시**하고(부르는 쪽이
// 잠금이나 포인터 교체로), 그 뒤로는 const 조회만 하면 여럿이 같이 읽어도 된다.
class StatNames {
public:
    // 두 표를 걸어 이름표를 채운다. 매니저 · 지식 매니저 · 현지화 중 하나라도
    // 없으면 **조용히** 표를 그대로 두고 false 를 돌린다 - 현지화 풀은 카탈로그보다
    // 늦게 차므로 부르는 쪽이 재시도한다(staged-data-load-retry 함정).
    //
    // 개별 행의 실패(지식 행 0xFFFF · 이름이 안 풀림)는 그 행만 건너뛴다.
    bool build(const mem::Rtti& rtti, const mem::Reader& reader,
               const LocSystem& loc);

    // 같은 일을 매니저 주소를 알 때 한다. RTTI 도 모듈 전역도 안 보므로
    // 가짜 메모리로 시험하기 쉽다(roster.h 의 `build_catalog_from_manager` 와 같은
    // 이유). `knowledge_mgr` 는 KnowledgeInfoManager 인스턴스다.
    bool build_from_managers(const mem::Reader& reader, const LocSystem& loc,
                             std::uintptr_t sub_level_mgr,
                             std::uintptr_t status_mgr,
                             std::uintptr_t knowledge_mgr);

    // 표 이름("SubLevel" · "Status")과 내부 키("Hp")로 표시 이름을 찾는다.
    // 없으면 빈 문자열 - 그러면 효과 문구는 토큰을 그대로 보인다(effect_format.h).
    std::string name_of(std::string_view table, std::string_view key) const;

    std::size_t size() const { return map_.size(); }

private:
    std::unordered_map<std::string, std::string> map_;   // "SubLevel:Hp" -> "생명"
};

// --- 실측 상수 (명세 §4.7-H' 1번) ---

// 토큰이 쓰는 표 이름. `{Staticinfo:<표>:<키>}` 의 가운데 토막이다.
inline constexpr std::string_view kStatTableSubLevel = "SubLevel";
inline constexpr std::string_view kStatTableStatus = "Status";

// 매니저 클래스. `find_static_manager` 에 그대로 넘긴다 - 부분 일치로 주면
// 엉뚱한 클래스를 잡으므로 장식된 이름 전체를 쓴다.
inline constexpr const char* kSubLevelManagerClass = ".?AVSubLevelInfoManager@pa@@";
inline constexpr const char* kStatusManagerClass = ".?AVStatusInfoManager@pa@@";

// SubLevelInfo · StatusInfo 레코드. 매니저 배치(+0x30 개수 · +0x58 레코드 배열)는
// 아이템 표와 같아 `roster_header` 가 그대로 읽는다.
inline constexpr std::size_t kStatRecStringKey = 0x08;  // 엔진 문자열 객체 포인터
inline constexpr std::size_t kSubLevelKnowRow = 0x46;   // u16 _knowledgeInfo
inline constexpr std::size_t kStatusKnowRow = 0x34;     // u16 _activeKnowledgeInfo
inline constexpr std::uint16_t kNoKnowRow = 0xFFFF;     // 지식이 없는 스탯

// 엔진 문자열 객체 {char* +0x00, u32 길이 +0x08, u32 해시 +0x0C}.
// 이름을 읽는 쪽(`read_engine_string`)은 앞 둘만 쓴다 - 우리는 해시가 필요하다.
inline constexpr std::size_t kEngineStringHash = 0x0C;

// 지식 표시 이름의 현지화 필드(cat 9). 엔티티는 위 해시다.
inline constexpr std::uint32_t kKnowledgeNameField = 0x490;

}  // namespace cdtb::game
