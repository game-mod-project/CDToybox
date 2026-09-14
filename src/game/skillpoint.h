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
// spin 은 분석 루프의 바퀴 수다. 못 찾은 동안 매 바퀴 훑으면 2초마다 힙 전수를
// 읽게 되므로 5바퀴(약 10초)에 한 번만 돌고, 12번 해 보고 그만둔다.
// player_actor 를 주면 **힙 스캔 없이** 그 액터에서 내려가 서버 컴포넌트를 잡는다
// (comp = *(u64*)( *(u64*)(actor+0x68) + 0x150 ) - 게임 코드 세 곳이 쓰는 사슬이고
// 신원이 확실하다). 0 이면 RTTI 탐색만 쓴다 - 그쪽은 vtable 값이 들어 있는 표까지
// 잡으므로(실측 2026-09-14) 구조 검사로 거른다.
bool discover_knowledge(const mem::Rtti& rtti, const mem::Reader& reader,
                        int spin, std::uintptr_t player_actor = 0);
bool knowledge_ready();
std::uintptr_t knowledge_component();          // 서버
std::uintptr_t knowledge_component_client();
void forget_knowledge();

// 지금 값을 읽는다. realm 0 = 서버, 1 = 클라.
BondState bond_read(const mem::Reader& reader, int realm);

// 캐시한 컴포넌트가 아직 살아 있는지 본다. 죽었으면 버리고 다시 찾게 한다.
// 게임은 세이브를 불러올 때 컴포넌트를 새로 만든다(인벤토리에서 실측했다) -
// 그때 캐시가 죽은 포인터를 든 채 남으면 화면이 낡은 값을 보이고 쓰기가 조용히
// 남의 자리로 간다. 로딩 화면의 순간적인 실패와 가르려고 경과 시간을 센다.
void knowledge_check_alive(const mem::Reader& reader);

// --------------------------------------------------- 되돌리기 기록(순수 부분)

// 이번 실행에서 처음 본 값 + **우리가 써 놓은 값**.
struct BondBackup {
    int realm = 0;
    std::uint16_t have = 0, total = 0, total2 = 0, total3 = 0;   // **최초** 원본
    // 우리가 마지막으로 만들어 놓은 값. 되돌리기 직전에 지금 값과 대조한다 -
    // 다르면 그 사이 게임이 결속을 주거나 사용자가 썼다는 뜻이므로 되돌리지 않는다.
    // 가방에서 같은 관문이 없어 "정당하게 산 칸을 지우는" 길이 열렸었다.
    std::uint16_t wrote_have = 0, wrote_total = 0;
};

// nullptr 이면 되돌려도 된다. 아니면 막는 이유(화면·로그에 그대로 낸다).
const char* bond_restore_blocked(const BondBackup& s, int now_have,
                                 int now_total);

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
