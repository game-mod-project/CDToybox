#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "game/localization.h"
#include "game/stat_names.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 아이템 툴팁의 **효과 줄**을 정적 표만 걸어 미리 조립한다. 전부 읽기다 -
// 게임 메모리에 쓰지 않고 게임 함수도 부르지 않는다.
//
// 실행 중 상태(플레이어·인벤토리)는 보지 않으므로 결과는 불변 스냅샷이다.
// 한 번 걷는 데 6816 아이템 × 사슬이라 **배경 스레드에서** 부른다(§4.5).
//
// 근거: docs/superpowers/specs/2026-09-22-item-description-effects-design.md
// §4.7 전체(A~E · H' · H'' · H'''). 아래 오프셋은 전부 거기 실측값이다.
//
// 효과의 입구가 둘이다 (§4.7-A)
// ------------------------------
//   소모품(요리·연금술) `_itemUseInfoList` +0x80 (u32 행 배열, +0x88 개수)
//     -> ItemUseInfoManager.records[행] +0x18 = ItemUseData_*
//        +0x18 종류(Skill=0x00) · +0x19 문맥(**0x00 만 툴팁 줄이다**)
//        +0x30 {u32 스킬행, u32 레벨(1-기반)} 배열 · +0x38 개수
//     -> SkillInfo.records[스킬행] +0x18 레벨 목록(16바이트 칸) -> BuffData* 배열
//   어비스 기어 `_enchantDataList` +0x248 (EnchantData **0x70 인라인** 배열)
//     -> EnchantData +0x58 `_equipBuffs`(0x20 스트라이드) · +0x60 개수
//        항목 +0x00 하위 u16 = BuffInfo 행 · +0x04 i32 = 레벨
//     -> BuffInfo 레벨 목록 -> BuffData
//
// BuffData 한 칸에서 한 줄 (§4.7-D · E)
// --------------------------------------
//   `+0x38` 하위 u16 이 PatternDescriptionInfo 행이면 거기서 멈추고 그 패턴의
//   형식 문자열에 값을 끼운다. 0xFFFF 면 `ChangeBuffLevel` 링크라 `+0x90`/`+0x94`
//   로 BuffInfo 레벨을 한 칸 더 따라간다(실측 최대 2단).
//   **지속시간은 바깥쪽**(스킬 레벨·인챈트에 직접 달린) BuffData 의 `+0x18` 이다 -
//   사슬 끝의 것이 아니다. 값과 패턴만 사슬 끝에서 온다.
//
// 값은 표로 나눈다 (§4.7-H'' · H''')
// -----------------------------------
//   `buff_param_rule(vtable, 파라미터 종류, BuffData +0x3A)` 가 `{칸, 나누는 수}`
//   를 준다. **규칙이 없으면 그 줄을 버리고 `unresolved` 를 센다** - 틀린 숫자를
//   보이는 것보다 낫다. 종류 8(`{RepeatTick}`)만 표 밖이다(`+0x28` / 1000).

struct ItemEffect {
    std::string text;               // 완성된 한 줄
    std::uint32_t duration_ms = 0;  // 바깥쪽 BuffData 의 지속 ms (0 = 즉발)
};

struct ItemEffects {
    std::vector<ItemEffect> lines;
    // 해석 못 한 효과 수. 게임은 줄을 보이는데 우리는 값을 못 채운 것들이다
    // (배율 모르는 클래스 · 안 풀린 형식 문자열 · 못 따라간 링크).
    int unresolved = 0;

    bool empty() const { return lines.empty() && unresolved == 0; }
};

// 걷기에 필요한 매니저 다섯. 시험용 오버로드가 그대로 받는다
// (stat_names.h `build_from_managers` 와 같은 이유 - RTTI·전역 없이 돌려
// 가짜 메모리로 시험한다).
struct EffectManagers {
    std::uintptr_t item = 0;      // ItemInfoManager
    std::uintptr_t item_use = 0;  // ItemUseInfoManager
    std::uintptr_t skill = 0;     // SkillInfoManager
    std::uintptr_t buff = 0;      // BuffInfoManager
    std::uintptr_t pattern = 0;   // PatternDescriptionInfoManager

    bool valid() const {
        return item != 0 && item_use != 0 && skill != 0 && buff != 0 &&
               pattern != 0;
    }
};

// 표를 통째로 한 번 걷는다. `out` 의 **색인은 아이템 행 번호**다(ItemInfoManager
// 레코드 배열에서의 위치). 효과가 없는 행은 빈 항목으로 남는다.
//
// 매니저 다섯을 RTTI 로 찾는다. 하나라도 없거나 현지화가 아직 없으면 조용히
// false - 부르는 쪽이 재시도한다(staged-data-load-retry 함정).
bool build_item_effects(const mem::Rtti& rtti, const mem::Reader& reader,
                        const LocSystem& loc, const StatNames& names,
                        std::vector<ItemEffects>* out);

// 같은 일을 매니저 주소를 알 때 한다.
bool build_item_effects_from_managers(const mem::Reader& reader,
                                      const LocSystem& loc,
                                      const StatNames& names,
                                      const EffectManagers& mgr,
                                      std::vector<ItemEffects>* out);

// 매니저 다섯을 RTTI 로 찾는다. 하나라도 못 찾으면 false.
bool find_effect_managers(const mem::Rtti& rtti, const mem::Reader& reader,
                          EffectManagers* out);

// --- 실측 상수 (명세 §4.7) ---

// 매니저 클래스. `find_static_manager` 에 장식된 이름 전체를 준다.
inline constexpr const char* kItemUseManagerClass = ".?AVItemUseInfoManager@pa@@";
inline constexpr const char* kSkillManagerClass = ".?AVSkillInfoManager@pa@@";
inline constexpr const char* kBuffManagerClass = ".?AVBuffInfoManager@pa@@";
inline constexpr const char* kPatternManagerClass =
    ".?AVPatternDescriptionInfoManager@pa@@";

// 효과 매니저 넷의 머리. 아이템 매니저(+0x30 개수)와 달리 개수가 `+0x08` 이다.
inline constexpr std::size_t kEffectMgrCount = 0x08;
inline constexpr std::size_t kEffectMgrRecords = 0x58;
// 아이템 매니저 머리(items.h 와 같은 자리).
inline constexpr std::size_t kItemMgrCount = 0x30;
inline constexpr std::size_t kItemMgrRecords = 0x58;

// ItemInfo 레코드
inline constexpr std::size_t kItemUseList = 0x80;    // u32 행 배열 포인터
inline constexpr std::size_t kItemUseCount = 0x88;   // 하위 u32 가 개수(상위는 용량)
inline constexpr std::size_t kItemEnchantList = 0x248;   // EnchantData 인라인 배열
inline constexpr std::size_t kItemEnchantCount = 0x250;  // u32 개수

// ItemUseInfo 레코드(0x20) -> ItemUseData_*
inline constexpr std::size_t kUseRecData = 0x18;
inline constexpr std::size_t kUseDataKind = 0x18;     // u8 0x00=Skill
inline constexpr std::size_t kUseDataContext = 0x19;  // u8 0x00 만 툴팁 줄
inline constexpr std::size_t kUseDataPairs = 0x30;    // {u32 스킬행, u32 레벨}
inline constexpr std::size_t kUseDataPairCount = 0x38;
inline constexpr std::uint8_t kUseKindSkill = 0x00;
inline constexpr std::uint8_t kUseContextTooltip = 0x00;

// SkillInfo: 레벨 목록(16바이트 칸 {BuffData** 배열, u32 개수, u32 채움})
inline constexpr std::size_t kSkillLevels = 0x18;
inline constexpr std::size_t kSkillLevelCount = 0x20;
inline constexpr std::size_t kSkillLevelStride = 0x10;

// BuffInfo: 레벨 목록(16바이트 칸 {i32 레벨, u32 잔여물, BuffData*}).
// **레벨이 음수일 수 있어 첨자로 찍지 않고 값으로 찾는다.**
inline constexpr std::size_t kBuffLevels = 0x18;
inline constexpr std::size_t kBuffLevelCount = 0x20;
inline constexpr std::size_t kBuffLevelStride = 0x10;
inline constexpr std::size_t kBuffLevelData = 0x08;   // 칸 안의 BuffData*

// EnchantData(0x70 스트라이드, ItemInfo 안에 인라인)
inline constexpr std::size_t kEnchantStride = 0x70;
inline constexpr std::size_t kEnchantBuffs = 0x58;       // _equipBuffs 배열
inline constexpr std::size_t kEnchantBuffCount = 0x60;
inline constexpr std::size_t kEnchantBuffStride = 0x20;
inline constexpr std::size_t kEnchantBuffRow = 0x00;     // 하위 u16 = BuffInfo 행
inline constexpr std::size_t kEnchantBuffLevel = 0x04;   // i32

// BuffData
inline constexpr std::size_t kBuffDataVtable = 0x00;
inline constexpr std::size_t kBuffDataDuration = 0x18;  // u32 지속 ms
inline constexpr std::size_t kBuffDataTick = 0x28;      // u32 반복 틱 ms (종류 8)
inline constexpr std::size_t kBuffDataPattern = 0x38;   // 하위 u16 = 패턴 행
inline constexpr std::size_t kBuffDataFlag3A = 0x3A;    // u8 배율 분기 선택자
inline constexpr std::size_t kBuffDataLinkRow = 0x90;   // 하위 u16 = BuffInfo 행
inline constexpr std::size_t kBuffDataLinkLevel = 0x94; // i32 대상 레벨
inline constexpr std::uint16_t kNoPatternRow = 0xFFFF;
// "BuffInfo 행이 없다" 의 표식. 값은 위와 같은 0xFFFF 지만 **뜻이 다르다**
// (패턴 표가 아니라 버프 표를 가리킨다) - 한 상수를 두 뜻으로 쓰면 나중에
// 한쪽 값만 바뀔 때 조용히 어긋난다.
inline constexpr std::uint16_t kNoBuffRow = 0xFFFF;

// PatternDescriptionInfo 레코드(0x60)
inline constexpr std::size_t kPatternEntity = 0x00;     // u32 현지화 엔티티
inline constexpr std::size_t kPatternParams = 0x48;     // 2바이트 항목 배열
inline constexpr std::size_t kPatternParamCount = 0x50;
inline constexpr std::size_t kPatternParamStride = 0x02;
// `_stringFormat` 의 현지화 필드(cat 15).
inline constexpr std::uint32_t kPatternFormatField = 0xF0;

// `{RepeatTick}`(종류 8)은 배율 표 밖이다 - 호출부가 `+0x28`(ms)을 1000.0 으로
// 나눠 직접 만든다(§4.7-H'' RVA 0x1F29A51).
inline constexpr std::uint8_t kRepeatTickParamType = 8;
inline constexpr double kRepeatTickDivisor = 1000.0;

// `ChangeBuffLevelBuffData` 의 vtable(모듈 고정 VA, §4.7-H' 2번). `+0x90`/`+0x94`
// 가 "대상 BuffInfo 행 · 대상 레벨" 인 것은 **이 클래스일 때뿐**이다 - 다른
// 클래스에서 `+0x90` 은 StatusInfo 행이거나 잔여물이라(§4.7-D) 따라가면 엉뚱한
// 줄이 나온다. 게임이 갱신되면 이 VA 가 밀려 링크를 못 따라가는데, 그때는 줄이
// 빠지고 `unresolved` 가 늘 뿐 **틀린 줄은 안 나온다**(안전한 쪽으로 깨진다).
inline constexpr std::uint64_t kChangeBuffLevelVtable = 0x1458FD040ull;

// 사슬 길이 상한(바깥쪽 BuffData 를 1단으로 센다). 실측 최대 2단이고, 3단을
// 넘으면 포기한다 - 순환하는 자료를 만나도 여기서 멈춘다.
inline constexpr int kMaxChainDepth = 3;

// 매니저 개수·배열 길이의 상한. 후보를 잘못 집으면 개수가 쓰레기값이라
// 그대로 믿고 돌면 멈추지 않는다.
inline constexpr std::uint32_t kMaxEffectRows = 1u << 20;
inline constexpr std::uint32_t kMaxEffectListLen = 4096;

}  // namespace cdtb::game
