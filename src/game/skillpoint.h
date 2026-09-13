#pragma once

#include <cstdint>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 스킬 포인트(어비스 결속).
//
// **이 빌드의 스킬 트리는 "스킬 포인트" 가 아니라 어비스 결속으로 돈다.** 엔진에
// `CharacterStatusSaveData::_remainSkillPoint`(+0x48 i16)와 `SkillPointSaveData` 가
// 실재하지만 출시 빌드에서는 안 쓰이는 잔재다(소비자 0곳). 화면이 쓰는 것은 이쪽이다.
// 아이템도 아니다 - 아이템 표 6813개에 "결속" 이 0건이라 지급으로는 못 늘린다.
//
// 실측 경로(2026-09-13, 사용자가 결속 하나를 쓰는 순간 전후 비교로 확정):
//   Client/ServerKnowledgeActorComponent +0xC8 -> 구조체
//     +0x00 u16  보유 (우상단 카운터 넷 중 **넷째**)
//     +0x02 u16  총합(누적 획득)
//     +0x08 u16  총합 사본?
//     +0x0C u16  별개 카운터 - 스킬을 배울 때 같이 1 줄었다(정체 미상, 안 건드린다)
//     +0x0E u16  총합 사본?
//   두 realm 의 이 16바이트가 **바이트까지 같았다.**
//
// **좌하단 "사용 어비스 결속" 은 저장된 값이 아니다.** 화면이 `총합 - 보유` 로
// 계산해 그린다(실측: 보유 1->0 인데 저장된 151 셋은 그대로였고, 화면은 150->151).
// 그래서 보유만 올리면 "사용" 이 줄어 쓰지도 않은 것을 되돌려받은 것처럼 보인다 -
// 총합도 같이 올려야 앞뒤가 맞는다.
inline constexpr std::size_t kBondHave = 0x00;
inline constexpr std::size_t kBondTotal = 0x02;
inline constexpr std::size_t kBondTotal2 = 0x08;
inline constexpr std::size_t kBondOther = 0x0C;   // 안 건드린다
inline constexpr std::size_t kBondTotal3 = 0x0E;

// 한 번에 더할 수 있는 최대와, 결과값의 천장.
// **상류 근거가 없다.** 참고 모드(CT·ASI) 둘 다 스킬 포인트 기능이 없어 빌려 올
// 숫자가 없다. u16 칸이라 65535 가 구조적 한계이고, 그 한참 아래의 보수적인 선을
// 고른 것이다 - 가방의 732 처럼 "엔진이 깨지는 선" 을 아는 것이 아니다.
inline constexpr int kBondAddMax = 100;
inline constexpr int kBondCeiling = 9999;

// 지금 읽은 값. address 가 0 이면 못 읽었다.
struct BondState {
    std::uintptr_t address = 0;
    int have = 0;
    int total = 0;
    int total2 = 0;
    int other = 0;
    int total3 = 0;
};

// 무엇을 쓸지. **순수 계산**이라 시험할 수 있다.
struct BondPlan {
    bool apply = false;
    const char* skip = "";
    int have = 0;     // +0x00 에 쓸 값
    int total = 0;    // 총합 칸에 쓸 값(also_total 이 거짓이면 지금 값 그대로)
};

// also_total 이 거짓이면 보유만 올린다 - 화면의 "사용" 이 그만큼 줄어든다.
// 어느 총합 사본을 화면이 읽는지 아직 모르므로, 켜고 끄며 로그로 가리라고 남겨 둔다.
BondPlan plan_bond_add(int have, int total, int add, bool also_total);

// 지식 컴포넌트를 찾는다(RTTI 한 번). 힙 전수 스캔은 못 찾았을 때만 돈다.
bool discover_knowledge(const mem::Rtti& rtti, const mem::Reader& reader);
bool knowledge_ready();
std::uintptr_t knowledge_component();          // 서버
std::uintptr_t knowledge_component_client();
void forget_knowledge();

// 지금 값을 읽는다. realm 0 = 서버, 1 = 클라.
BondState bond_read(const mem::Reader& reader, int realm);

struct BondResult {
    int changed = 0;    // 실제로 쓴 realm 수
    int skip = 0;
    int fail = 0;
    const char* last_skip = "";
};

// 두 realm 에 같이 쓴다. **모드(주입 DLL)에서만** 부른다.
BondResult bond_add(const mem::Reader& reader, int add, bool also_total);
// 이번 실행에서 처음 본 값으로 되돌린다.
BondResult bond_restore(const mem::Reader& reader);
bool bond_has_backup();

}  // namespace cdtb::game
