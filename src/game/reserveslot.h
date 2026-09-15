#pragma once

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"

namespace cdtb::game {

// 예약 슬롯(원형 휠) 진단. **읽기만 한다.**
//
// 배경: 지식 레벨을 써도, 스킬 등록 함수를 불러도 원소 휠 칸이 안 켜졌다(실측
// 2026-09-14). 원소 지식 여섯은 `_learnApplySkillInfo` 가 0xFFFF 라 붙는 스킬이 아예
// 없다. 그래서 휠을 정하는 것은 지식이 아니라 **예약 슬롯** 쪽이다.
//
// 조사(2026-09-14)가 밝힌 구조:
//
//   slotComp = *(u64*)( *(u64*)(액터 + 0x68) + 0x38 )   // EquipSlotActorComponent
//   배열     = *(u64*)(slotComp + 0x58), 개수 = *(u32*)(slotComp + 0x60)
//   엔트리   = 배열 + i*0x110,  { u16 ReserveSlotKey @+0x00, 레코드 @+0x08 }
//
//   ReserveSlotInfoManager = *(u64*)(이미지 + 0x06C2E300)
//     +0x08 개수 · +0x58 `ReserveSlotInfo*` 배열(키로 색인)
//     +0xA0.. 잘 알려진 키 여덟(+0xAA 가 **원소 선택 슬롯**)
//
//   ConditionInfoManager   = *(u64*)(이미지 + 0x06C2F260)   // 같은 모양
//
// 휠 칸의 후보는 `ReserveSlotInfo._enableSpecialNameHashList`(+0x68 데이터 / +0x70 개수)
// 이고 원소는 **4바이트 `{u16 SpecialName, u16 ConditionInfoKey}`** 다. UI 가 원소마다
// 그 조건식을 평가해 칸을 넣거나 뺀다.
//
// **여기서는 아무것도 안 부르고 안 쓴다.** 조회 함수(RVA 0x0039A830 / 0x003C1C70)가
// 쓰는 전역을 그대로 읽으므로 게임 함수 호출이 필요 없다 - 렌더 스레드에서 게임
// 함수를 불러 죽었던 전례가 있다(TROUBLESHOOTING 1.8/1.13).

// 두 매니저는 배치가 같다.
inline constexpr std::uintptr_t kSlotMgrGlobalRva = 0x06C2E300;
inline constexpr std::uintptr_t kCondMgrGlobalRva = 0x06C2F260;
inline constexpr std::size_t kMgrCount = 0x08;
inline constexpr std::size_t kMgrArray = 0x58;
inline constexpr std::size_t kMgrWellKnown = 0xA0;   // u16 여덟
inline constexpr int kWellKnownCount = 8;
inline constexpr int kElemSlotIndex = 5;             // +0xAA = ElementalSelectSlot

// ReserveSlotInfo
inline constexpr std::size_t kRsKey = 0x00;
inline constexpr std::size_t kRsBlocked = 0x10;
inline constexpr std::size_t kRsFillData = 0x28;
inline constexpr std::size_t kRsFillCount = 0x30;
inline constexpr std::size_t kRsType = 0x40;
inline constexpr std::size_t kRsUsingType = 0x41;
inline constexpr std::size_t kRsNameHash = 0x68;     // {u16 SpecialName, u16 조건키}
inline constexpr std::size_t kRsNameHashCount = 0x70;
inline constexpr std::size_t kRsTargetList = 0x98;
inline constexpr std::size_t kRsTargetCount = 0xA0;  // 추측
inline constexpr std::size_t kRsFlags = 0xAC;        // 4바이트

// ConditionInfo
inline constexpr std::size_t kCiBlocked = 0x10;
inline constexpr std::size_t kCiCondition = 0x18;
inline constexpr std::size_t kCiOriginal = 0x28;
inline constexpr std::size_t kCiParser = 0x30;

// EquipSlotActorComponent
inline constexpr std::size_t kActorSub = 0x68;
inline constexpr std::size_t kSubSlotComp = 0x38;
inline constexpr std::size_t kScOwner = 0x08;
inline constexpr std::size_t kScData = 0x58;
inline constexpr std::size_t kScCount = 0x60;
inline constexpr std::size_t kScStride = 0x110;
inline constexpr std::size_t kScRec = 0x08;

inline constexpr int kSlotMaxCount = 20000;
inline constexpr int kSlotDumpMax = 64;

// **읽기만 한다.** 조사가 적어 준 자리를 한 번에 다 찍는다 - 배포마다 게임을 꺼야
// 하므로 진단은 몰아서 넣는다. player_actor 가 0 이면 런타임 컨테이너는 건너뛴다.
void reserveslot_diagnose(const mem::Reader& reader, std::uintptr_t player_actor);

// 원소 조건 넷(9198~9201)의 정체를 한 번에 밝히는 진단. **읽기만 한다.**
//
// 조사(2026-09-15)가 해독 경로를 확정했다:
//   ConditionInfo(0x40): +0x00 _key · +0x08 _stringKey(문자열 객체) · +0x10 _isBlocked
//                        +0x18 **GameCondition*** · +0x28 _originalString · +0x30 _parserType
//   문자열 객체:        +0x00 char*(UTF-8) · +0x08 u32 길이 · +0x10 i32 refcount
//                        공유 빈 문자열 = 이미지 + 0x0692E4C0
//   GameCondition+0x08 u16 = **466개 조건 함수 이름표**(이미지 + 0x0584FA10)의 색인
//     386 CheckGamePlayVariable (인자 u16 @ +0x18)
//     298 CheckReserveSlot · 35 CheckKnowledge · 69/71/74 CompleteQuest/Mission/Stage
//
// 한때 이 조건들의 문자열을 읽었다고 적었다가 틀렸다 - ConditionInfo 가 0x40 인데
// 0x60 을 떠서 이웃 객체를 읽은 것이었다. 그래서 여기서는 **0x40 을 넘지 않는다.**
void element_diagnose(const mem::Reader& reader, std::uintptr_t player_actor);

// ------------------------------------------------ 원소 습득 (2026-09-15 확정)
//
// 휠 칸의 조건이 평문으로 나왔다(`ConditionInfo._stringKey`):
//
//   9198  CheckEquipSlotName(Bracelet) && CheckKnowledge(Knowledge_MpFire)       화염
//   9199  ...(Knowledge_MpIce)        냉기
//   9200  ...(Knowledge_MpLightning)  벼락
//   9201  ...(Knowledge_MpWind)       바람
//
// **조건은 둘이다** - 팔찌 장착 + 그 지식. 그리고 조건이 보는 것은 **접두사 없는**
// 이름이다. 캐릭터별 이름(`Knowledge_Kliff_MpFire` 4706, `_Damian_` 4793,
// `_Oongka_` 4879 …)은 스킬 트리 노드이고 **휠과 무관하다** - 그것들을 쓰느라
// 여러 번 헛돌았다.
//
// **번호를 코드에 안 박는다.** 게임이 갱신되면 지식 번호가 밀리므로
// `KnowledgeInfo +0x08` 의 내부 이름으로 매번 찾는다(실측: +0x08 이 내부 이름이다 -
// "Knowledge_Hp", "Knowledge_Fatal" …).
inline constexpr int kElementCount = 4;

struct ElementKnow {
    const char* label = "";      // 화염·냉기·벼락·바람
    const char* internal = "";   // Knowledge_MpFire …
    int number = -1;             // 지식 번호. -1 이면 못 찾았다
    int level = 0;               // 지금 레벨(0 = 미습득)
};

// 넷을 이름으로 찾고 지금 레벨까지 채운다. **읽기만 한다.**
bool element_knowledge(const mem::Reader& reader,
                       ElementKnow out[kElementCount]);

// ------------------------------------------- 탈것 휠 해금 (2026-09-15)
//
// 게임 데이터(`0008` 그룹의 `gamedata/reserveslot`)를 직접 꺼내 읽어 확정했다.
// 탈것 휠은 슬롯이 **셋**이고 각자 허용 카테고리를 들고 있다.
//
//   1000006 VehicleSlot           [0x4E 일반 탈것, 0x51 지상 차량]   <- 메인 휠
//   1000019 VehicleSlot_Mechanic  [0x50 ATAG, 0x52 기계]
//   1000020 VehicleSlot_Dragon    [0x4F 드래곤]                      <- 전용 슬롯
//
// 즉 **드래곤·ATAG 는 메인 휠의 허용 목록에서 빠져 있고** 자기 전용 슬롯에만
// 들어간다. 그 전용 슬롯이 스토리로 채워지는 자리다(획득 전에는 회색 안장).
// 메인 휠의 목록에 드래곤·ATAG 카테고리를 얹으면 전용 슬롯을 우회할 수 있다.
//
// 앞선 조사가 "소유 타입 레지스트리"라 부르며 그룹 24/25/26 으로 읽은 배열이
// 바로 이 목록이다(같은 전역 0x6C2E300, 같은 +0x58/+0x60). 그때는 "드래곤이
// 이미 그룹 26 에 있으니 건드릴 필요 없다"고 닫았는데, 넣어야 할 곳이 26 이
// 아니라 **메인 휠인 그쪽**이었다.
//
// 메모리 배치는 `_enableMercenaryList` - +0x58 포인터 / +0x60 개수 / +0x64 용량
// 이고 항목은 **u16** 이다(게이트 0x2ACA250 이 `cmp bx,[r8+rax*2]` 로 읽는다).
// 데이터의 1바이트 카테고리는 로드 때 타입 행으로 옮겨진다(실측 대조:
// 0x4E->1 · 0x4F->2 · 0x50->3 · 0x51->5 · 0x52->4).
//
// **번호를 코드에 안 박는다.** 무엇을 더할지는 드래곤·메카닉 슬롯이 지금 들고
// 있는 값을 그대로 베껴 온다 - 게임이 갱신돼 타입 행이 밀려도 따라간다.
//
// 정적 표라 **세이브에 안 남는다.** 게임을 끄면 원복된다(소켓 상한과 같은 성질).
inline constexpr int kVehSlotKey = 1000006;
inline constexpr int kMechSlotKey = 1000019;
inline constexpr int kDragonSlotKey = 1000020;

inline constexpr std::size_t kRsMercList = 0x58;   // _enableMercenaryList
inline constexpr std::size_t kRsMercCount = 0x60;
inline constexpr std::size_t kRsMercCap = 0x64;

inline constexpr int kWheelMaxCats = 16;

struct WheelSlot {
    std::uintptr_t info = 0;
    int key = 0;
    int count = 0;
    int cats[kWheelMaxCats] = {};
};

struct WheelState {
    bool ready = false;                 // 슬롯 셋을 다 잡았는가
    bool on = false;                    // 지금 우리 값이 걸려 있는가
    WheelSlot main_slot;                // VehicleSlot
    WheelSlot dragon;
    WheelSlot mech;
    int want[kWheelMaxCats] = {};       // 걸면 메인 휠이 이렇게 된다
    int want_count = 0;
    char note[96] = {};                 // 못 잡았으면 그 이유
};

// **순수 함수.** base 에 없는 것만 뒤에 붙인다. out 이 모자라면 들어가는 만큼만
// 넣고 그 개수를 돌려준다(자르되 거짓말하지 않는다).
int wheel_merge(const int* base, int base_n, const int* add, int add_n,
                int* out, int out_cap);

// 읽기만 한다. 화면이 매 프레임 부르므로 로그를 안 남긴다.
WheelState wheel_state(const mem::Reader& reader);

// 메인 휠 목록을 늘리거나(on) 원래대로 되돌린다(off). 정적 표에 쓴다.
bool wheel_unlock(const mem::Reader& reader, bool on);

// 모드를 내릴 때 우리가 건 것을 되돌린다. 안 걸려 있으면 아무것도 안 한다.
void wheel_teardown();

}  // namespace cdtb::game
