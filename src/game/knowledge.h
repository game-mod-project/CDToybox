#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 지식(스킬 트리 노드) 표.
//
// 화면의 `[깨달음] 필요` / `[원소: …] 중 하나` 는 **둘 다 선행 지식**이다. 문자열 키가
// `UI_Skill_RequiredKnowledgeNotLearned` / `UI_Skill_RequiredAnyKnowledgeNotLearned` 한 쌍이고,
// 둘을 고르는 코드가 같은 목록·같은 개수를 쓰고 메시지만 갈아 끼운다(2026-09-13 실측).
//
// 판정은 `CheckLearnOrLevelUp`(RVA 0x0208B070) 안의 루프 0x0208BB7D~0x0208BCD9 이고,
// 하는 일은 **한 줄**이다(RVA 0x0208BC20 `cmp dword [r10+rcx*8], eax`):
//
//     배운지식표[선행지식번호].레벨 >= 필요레벨
//
// 그 루프는 **쓰기가 하나도 없다** - 툴팁 목록은 별개 루프(아이템·재화, 0x0208B669~)가
// 만든다. 그래서 "배운 것으로 만들기" 만으로 조건이 그대로 풀린다.
//
// 그리고 지식을 배우면 휠도 열린다(정적 표 사슬):
//     KnowledgeInfo._learnApplySkillInfo(+0x104) -> SkillInfo._reserveSlotInfoList(+0xE8)
// 판정만 우회하는 코드 패치로는 휠이 안 열린다 - 그래서 데이터 쓰기가 정공법이다.

// ------------------------------------------------------- 실측 오프셋 (2026-09-13)

// 매니저 전역. 조회 함수 RVA 0x003C1C70 이 그대로 보여 준다:
//   rbx = *(전역);  if (번호 >= [rbx+8]) 실패;  info = *(u64*)([rbx+0x58] + 번호*8)
// **RVA 를 박아 두는 것이라 게임이 갱신되면 밀린다**(game-update-rva-drift).
// 그래서 읽은 값이 말이 되는지, 그리고 컴포넌트의 레코드 수와 같은지 반드시 대조한다
// (`Initialize` 가 매니저의 개수로 표를 잡으므로 두 값은 같아야 한다).
inline constexpr std::uintptr_t kKnowMgrGlobalRva = 0x06C2E2D8;
inline constexpr std::size_t kKnowMgrCount = 0x08;   // i32 전체 지식 수
inline constexpr std::size_t kKnowMgrArray = 0x58;   // KnowledgeInfo* 배열(stride 8)

// KnowledgeActorComponent 의 배운 지식 표. `Initialize`(RVA 0x0E09C4F0)가
// 전체 지식 수만큼 **한 번에** 잡는 밀집 배열이라, 끼워 넣을 것이 없다.
inline constexpr std::size_t kKnowCompData = 0x18;
inline constexpr std::size_t kKnowCompCount = 0x20;
inline constexpr std::size_t kKnowCompCap = 0x24;
inline constexpr std::size_t kKnowRecStride = 24;
inline constexpr std::size_t kKnowRecLevel = 0x00;   // i32, 0 = 미습득
inline constexpr std::size_t kKnowRecObj = 0x08;     // 지연 생성 객체 - 건드리지 않는다
inline constexpr std::size_t kKnowRecFlag = 0x10;    // u8, 게임도 1 을 쓴다

// KnowledgeInfo / KnowledgeLevelData / learnFrom
inline constexpr std::size_t kInfoLevels = 0x88;        // KnowledgeLevelData 배열
inline constexpr std::size_t kInfoLevelCount = 0x90;    // i32
inline constexpr std::size_t kLevelStride = 0xE8;
inline constexpr std::size_t kLevelName = 0xA8;         // u64 현지화 키로 추정
inline constexpr std::size_t kLevelLearnFrom = 0x78;
inline constexpr std::size_t kLevelLearnFromCount = 0x80;
inline constexpr std::size_t kLearnFromStride = 0x58;
inline constexpr std::size_t kLearnFromNeed = 0x20;       // 선행 지식 목록
inline constexpr std::size_t kLearnFromNeedCount = 0x28;
inline constexpr std::size_t kLearnFromTag = 0x44;        // u8, 3 = 스킬 트리 경로
inline constexpr std::size_t kLearnFromMode = 0x45;       // u8, 0 = 전부, 1 = 중 하나
inline constexpr std::size_t kNeedStride = 8;             // {u16 번호 @0, i32 레벨 @4}
inline constexpr std::uint8_t kTagSkillTree = 3;
inline constexpr std::uint8_t kModeAnyOf = 1;

// 훑기 한도. 표가 말이 안 되면 거기서 멈춘다 - 쓰레기 포인터를 따라가지 않는다.
inline constexpr int kKnowMaxCount = 20000;
inline constexpr int kKnowMaxLevels = 64;
inline constexpr int kKnowMaxLearnFrom = 32;
inline constexpr int kKnowMaxNeeds = 64;

// ------------------------------------------------------------------ 자료형

struct KnowMgr {
    std::uintptr_t object = 0;
    std::uintptr_t array = 0;
    int count = 0;
};

struct KnowTable {
    std::uintptr_t comp = 0;
    std::uintptr_t data = 0;
    int count = 0;
    int cap = 0;
};

// 어딘가의 선행 조건으로 요구되는 지식 하나.
struct KnowNeed {
    int number = 0;        // 지식 짧은 번호(표의 색인)
    int need_level = 0;    // 요구 레벨 중 가장 높은 것
    int have_level = 0;    // 지금 레벨
    int wanted_by = 0;     // 이것을 요구하는 (지식, 레벨) 자리의 수
    bool any_of = false;   // "중 하나" 목록에 든 적이 있나
    std::string name;      // 현지화 이름(못 풀면 빈 문자열)
};

struct KnowScan {
    bool ok = false;
    const char* skip = "";
    int realm = 0;
    int knowledge = 0;   // 매니저가 든 지식 수
    int learned = 0;     // 레벨 >= 1 인 것
    int walked = 0;      // 실제로 훑은 지식 수
    std::vector<KnowNeed> needs;   // **모자란 것만**, 요구 많은 순
};

// ------------------------------------------------------------ 순수 부분(시험용)

std::uintptr_t know_record(std::uintptr_t data, int n);
// 매니저가 말이 되는 값인가. 게임 갱신으로 RVA 가 밀리면 여기서 걸린다.
bool know_mgr_sane(int count, std::uintptr_t array);
// 같은 번호가 여러 곳에서 요구되면 **가장 높은 요구 레벨**로 합치고 수를 센다.
void know_need_add(std::vector<KnowNeed>* v, int number, int need, bool any_of);
// 요구 많은 순 -> 번호 순. 화면이 흔들리지 않게 완전 순서를 준다.
void know_need_sort(std::vector<KnowNeed>* v);
// 레벨이 충분한 것을 뺀다. 남은 것이 "아직 못 배운 선행 조건" 이다.
void know_need_drop_satisfied(std::vector<KnowNeed>* v);

// ------------------------------------------------------------------ 읽기

bool know_manager(const mem::Reader& reader, KnowMgr* out);
// realm 0 = 서버, 1 = 클라. skillpoint 의 탐색 결과를 그대로 쓴다.
bool know_table(const mem::Reader& reader, int realm, KnowTable* out);
int know_level(const mem::Reader& reader, const KnowTable& t, int n);

// 정적 표를 통째로 훑어 "선행 조건으로 요구되는데 내 레벨이 모자란 지식" 을 모은다.
// rtti 가 널이 아니면 이름도 푼다(현지화). **쓰기 없음.**
KnowScan know_scan(const mem::Rtti* rtti, const mem::Reader& reader, int realm);

// ------------------------------------------------------------------ 쓰기

struct KnowWrite {
    int changed = 0;   // 실제로 쓴 realm 수
    int skip = 0;
    int fail = 0;
    const char* last_skip = "";
};

// 지식 하나를 그 레벨로 만든다(두 realm). 게임이 지식을 지급할 때 하는 쓰기와 같다
// (`ServerKnowledgeActorComponent::Initialize` RVA 0x02AA620C/0x02AA6224):
// 레코드 `+0x00` 에 레벨, `+0x10` 에 1. **`+0x08` 은 건드리지 않는다** - 게임이
// 필요할 때 만들고, 읽는 쪽에 널 가드가 있다.
KnowWrite know_learn(const mem::Reader& reader, int number, int level);

}  // namespace cdtb::game
