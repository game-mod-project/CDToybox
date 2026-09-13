#pragma once

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"

namespace cdtb::game {

// 스킬 강화 조건 관문 우회.
//
// 관문 본체는 `KnowledgeActorComponent::CheckLearnOrLevelUp`(RVA 0x0208B070, 약 4.3KB)
// 이고, 그 안에 관문이 여덟 단계 순서대로 있다(2026-09-13 조사). 여기서 푸는 것은 둘이다.
//
//   1. **습득 경로**(`eErrNoCannotLearnKnowledgeByFromType`) - 화면의
//      "특정 조건을 통해 배울 수 있습니다".
//      판정 함수 `CanLearnByFromType` 는 썽크 0x0208AF80 -> 본체 **0x0E0AB860** 이고
//      결과를 `al` 의 bool 로 낸다. 실행 파일 실측으로 썽크의 jmp 목적지가 그 주소임을
//      확인했다(`E9 DB 08 02 0C` -> 0x0208AF80+5+0x0C0208DB = 0x0E0AB860).
//      진입점을 `mov al,1; ret` 로 덮으면 언제나 통과한다.
//
//   2. **비용**(어비스 결속이 모자람) - RVA **0x0208B639** 의
//      `setge byte [rsp+0x40]`(`0F 9D 44 24 40`, 5바이트)를
//      `mov byte [rsp+0x40],1`(`C6 44 24 40 01`, 5바이트)로 덮는다. 길이가 같아
//      재배치가 없고, **값을 바꾸지 않고 판정만 통과**시키므로 결속 수는 그대로다.
//
// **여기서 안 푸는 것**: 선행 지식(화면의 `[깨달음] 필요`)은 0x0208B669~ 의 루프이고,
// 그 루프가 판정만 하는 것이 아니라 화면에 뿌릴 목록도 만든다. 잘못 건드리면 툴팁이
// 깨지므로 따로 조사한 뒤에 한다.
enum SkillGate : int {
    kGateFromType = 0,   // "특정 조건을 통해 배울 수 있습니다"
    kGateCost = 1,       // 어비스 결속 부족
    kGateCount = 2,
};

struct SkillGateInfo {
    const char* name = "";
    const char* what = "";
    bool on = false;           // 지금 걸려 있나
    bool unsupported = false;  // 원본 바이트가 달라 설치를 거부했다(게임 갱신)
    std::uintptr_t site = 0;   // 실제 주소(0 이면 아직 안 봤다)
};

SkillGateInfo skillgate_info(int gate);
// 켜면 패치를 걸고, 끄면 원본을 되돌린다. 성공하면 참.
bool skillgate_set(const mem::Reader& reader, int gate, bool on);
// 모듈이 내려갈 때 전부 되돌린다.
void skillgate_remove_all();

// --------------------------------------------------------- 순수 부분(시험용)

// [addr, addr+len) 을 담는 **8바이트 정렬 창**을 구한다. 못 담으면 거짓.
//
// 짧은 바이트를 memcpy 로 쓰면 보통 dword + byte 두 번으로 나가는데, 그 틈에 다른
// 스레드가 그 명령을 실행하면 **반쯤 바뀐 명령**을 실행한다. 정렬된 8바이트 한 번의
// 원자 교환이면 그 창이 없다(낙사 훅에서 같은 이유로 같은 기법을 쓴다).
bool patch_window(std::uintptr_t addr, std::size_t len, std::uintptr_t* base,
                  std::size_t* off);

// 원본 qword 의 off 자리에 with[len] 을 끼운 값을 낸다(리틀엔디언 바이트 순서).
std::uint64_t patch_splice(std::uint64_t orig, std::size_t off,
                           const std::uint8_t* with, std::size_t len);

}  // namespace cdtb::game
