#pragma once

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"

namespace cdtb::game {

// 스킬 강화 조건 관문 우회.
//
// 관문 본체는 `KnowledgeActorComponent::CheckLearnOrLevelUp`(약 4.3KB)이고, 그 안에
// 관문이 여덟 단계 순서대로 있다(2026-09-13 조사). 여기서 푸는 것은 둘이다.
//
// **아래 RVA 는 exe 1.0.0.2850 기준 서술이다.** 지금 쓰는 자리(2944)는
// `skillgate.cpp` 의 `kGates` 표에 있고, 거기 재도출 경로를 적어 뒀다.
// 2944: 함수 0x2140730~0x2141885 · 관문0 0x0E808870 · 관문1 0x02140CF9.
//
//   1. **습득 경로**(`eErrNoCannotLearnKnowledgeByFromType`) - 화면의
//      "특정 조건을 통해 배울 수 있습니다".
//      판정 함수 `CanLearnByFromType` 는 썽크 0x0208AF80 -> 본체 **0x0E0AB860** 이고
//      결과를 `al` 의 bool 로 낸다. 실행 파일 실측으로 썽크의 jmp 목적지가 그 주소임을
//      확인했다(`E9 DB 08 02 0C` -> 0x0208AF80+5+0x0C0208DB = 0x0E0AB860).
//      진입점을 `mov al,1; ret` 로 덮으면 언제나 통과한다. 호출자 셋이 전부 호출 직후
//      `test al,al` 만 보므로 `al` 만 세워도 충분하고, 덮는 3바이트는 프롤로그의
//      **첫 명령 안**이라 RSP 도 비휘발성 레지스터도 아직 그대로다(검토가 `.pdata`
//      로 진입점·프롤로그 길이를 확인했다).
//
//   2. **비용**(어비스 결속이 모자람) - RVA **0x0208B639** 의
//      `setge byte [rsp+0x40]`(`0F 9D 44 24 40`, 5바이트)를
//      `mov byte [rsp+0x40],1`(`C6 44 24 40 01`, 5바이트)로 덮는다. 길이가 같아
//      재배치가 없고, 다음 명령 경계(0x0208B63E)가 **분기 목적지**인데 그대로 보존된다.
//
//      **이 패치가 결속 값을 안 쓴다는 것과, 결속이 안 줄어든다는 것은 다른 말이다.**
//      실제 차감은 이 판정 함수 밖(서버 요청 처리기 쪽)에 있고 아직 그 자리를 못
//      찾았다. 판정 함수는 총비용을 출력 인자로 **그대로** 내보내므로
//      (0x0208B623 `add word [rcx], dx`), 보유보다 비싼 노드를 찍으면 차감 경로가
//      u16 보유 칸을 음수로 돌릴 수 있다 - **미확인 위험**이다. 화면 문구가 그것을
//      단정하지 않게 적어 둔다. 결속 자체를 늘리는 길은 이미 있으므로
//      (`skillpoint.h`), 그쪽을 먼저 쓰는 것이 안전하다.
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
    bool probed = false;       // 한 번이라도 그 자리를 확인해 봤나
    std::uintptr_t site = 0;   // 실제 주소(0 이면 아직 안 봤다)
};

SkillGateInfo skillgate_info(int gate);

// 켜면 패치를 걸고, 끄면 원본을 되돌린다. 성공하면 참.
// 실패하면 `why` 에 사유 한 줄이 담긴다(널 가능).
//
// **모드(주입 DLL) 전용이다.** 게임의 주소에 **자기 주소공간으로** 쓰므로
// 외부 분석 도구(cdtb_probe)가 부를 수 있는 모양이면 안 된다 - 그래서 Reader 를
// 받지 않고 안에서 LocalReader 를 쓴다.
bool skillgate_set(int gate, bool on, const char** why = nullptr);

// 패널을 열 때 한 번 훑어 "이 빌드에서 쓸 수 있나" 를 미리 정한다. 안 그러면
// 원본이 다른 빌드에서 **첫 클릭이 조용히 먹히고** 빨간 줄은 다음 프레임에야 뜬다.
// 이미 확인한 관문은 다시 안 본다.
void skillgate_probe();

// 사용자가 모드를 내릴 때 전부 되돌린다. **해상도 변경 경로에서 부르면 안 된다** -
// 코드 페이지는 스왑체인과 아무 관계가 없다.
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

// 한 관문의 정의.
//
// **`want` 는 창 8바이트 전체다.** 확인 폭과 쓰기 폭을 일부러 분리했다 - 쓰기는
// `len` 바이트면 충분하지만 확인은 전혀 충분하지 않다. 관문0 의 쓰기 폭 3바이트
// (`48 89 5C`, MSVC x64 가 가장 흔히 내는 프롤로그 머리)는 이 실행 파일의 실행
// 섹션에 **136,428곳**(8정렬 자리만 95,665곳) 있고, 창 8바이트로 비교하면
// **44곳**으로 줄어든다. 관문1 은 8바이트면 이미지 전체에 **유일**하다.
// 이 저장소는 갱신마다 고정 RVA 가 영역별로 다르게 밀리는 것을 실측해 두었으므로
// (game-update-rva-drift: +0x2040/+0x1630/+0x1740/+0x4150 - 단일 델타가 아니다)
// 이 확인은 반드시 실제로 시험받는다. 그때 통과해 버리면 남의 함수 프롤로그가
// `mov al,1; ret` 이 되고, 로그는 "설치 완료" 라고 쓴다.
struct GateDef {
    const char* name;
    const char* what;
    std::uintptr_t rva;
    std::uint8_t want[8];   // 창 8바이트 **전체**(확인용)
    std::uint8_t with[8];   // 쓸 값(앞 len 바이트만 본다)
    std::size_t len;
};

struct GateSlot {
    std::uintptr_t site = 0;
    std::uint64_t orig = 0;   // 켤 때 떠 둔 창 8바이트(되돌리기용)
    bool on = false;
    bool unsupported = false;
    bool probed = false;
};

enum GateStep : int {
    kStepOk = 0,
    kStepAlready,          // 이미 그 상태다
    kStepOutOfImage,       // 이미지 밖이다(갱신으로 이미지가 줄었다)
    kStepBadWindow,        // 8바이트 창에 안 들어간다
    kStepReadFailed,       // 그 자리를 못 읽는다
    kStepForeignOriginal,  // 원본이 우리가 아는 바이트가 아니다
    kStepForeignNow,       // 끄는데 지금 값이 우리가 쓴 것이 아니다
    kStepWriteFailed,
};

// 읽기·쓰기를 밖에서 받는다. 그래야 **상태 기계 전체**를 가짜 메모리 위에서
// 시험할 수 있다(skillpoint 의 `BondPlan` 과 같은 "순수 계획 + 얇은 적용" 모양).
struct GateIo {
    bool (*read8)(void* ctx, std::uintptr_t base, std::uint8_t out[8]) = nullptr;
    bool (*write8)(void* ctx, std::uintptr_t base, std::uint64_t value) = nullptr;
    void* ctx = nullptr;
};

// 관문 하나를 켜거나 끈다. `s` 를 제자리에서 갱신한다.
// **`unsupported` 는 여기서 안 세운다** - 끄는 길이 막히면 패치가 걸린 채 끌 방법이
// 사라지므로, 그 판단은 켜기에서만 하도록 부르는 쪽에 맡긴다.
GateStep gate_apply(const GateDef& d, GateSlot* s, std::uintptr_t module_base,
                    std::size_t module_size, bool on, const GateIo& io);

// 그 자리가 우리가 아는 바이트인지만 본다(쓰지 않는다). `s->probed` 를 세운다.
GateStep gate_probe(const GateDef& d, GateSlot* s, std::uintptr_t module_base,
                    std::size_t module_size, const GateIo& io);

const GateDef* gate_def(int gate);
const char* gate_step_text(GateStep step);

// **실제 게임 메모리에 붙는 `GateIo`.** 보호 해제 · 정렬 8바이트 원자 교환 ·
// 보호 복원 · 명령 캐시 무효화 · 쓰기 로그가 전부 이 뒤에 있다.
//
// 코드에 쓰는 기법은 하나여야 한다 - 관문이 늘 때마다 `VirtualProtect` 와
// `_InterlockedExchange64` 를 베껴 쓰면, 한 곳을 고칠 때 나머지가 조용히 남는다.
// 그래서 `callgate` 같은 다른 관문 모듈도 이것을 받아 쓴다.
//
// **모드(주입 DLL) 전용이다** - 자기 주소공간에 쓴다.
// `reader` 와 `what` 은 돌려받은 `GateIo` 를 쓰는 동안 살아 있어야 한다.
// 문맥은 스레드마다 하나씩 들고 있으므로 스레드 사이에 섞이지 않는다.
GateIo gate_local_io(const mem::Reader& reader, const char* what);

}  // namespace cdtb::game
