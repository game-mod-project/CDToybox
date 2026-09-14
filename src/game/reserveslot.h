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

}  // namespace cdtb::game
