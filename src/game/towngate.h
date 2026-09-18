#pragma once

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"

namespace cdtb::mem {
class Rtti;
}

namespace cdtb::game {

// 마을 판정(`IsInTown()`) 풀기 — 드래곤·A.T.A.G. 를 마을에서 부르고, 타고
// 들어가도 안 내려지게 한다.
//
// `gamedata/failmessageinfo` 가 조건을 평문으로 들고 있다
// (specs/2026-09-16-vehicle-place-and-dismount.md §1):
//
//     드래곤 못 부름/강제 하차   IsInTown() && !IsAboveRoad(Bird,20)
//     A.T.A.G. 못 부름/강제 하차 IsInTown()
//
// 그 `IsInTown()` 이 무엇을 읽는지 실행 파일에서 전부 떴다(2026-09-17).
// `AICondition_IsInTown` 의 판정 함수(RVA 0x2232130)는 **두 갈래의 OR** 이다:
//
//   (1) RVA 0x1764580 - 액터가 지금 들어와 있는 구역을 훑어 하나라도
//       `RegionInfo._isTown != 0` 이면 참
//
//         rax = [액터+0x68]          ; 컴포넌트 홀더
//         rcx = [rax +0x1A0]         ; 구역 상태 컴포넌트
//         rax = [rcx +0x38]          ; 겹쳐 있는 구역 목록
//         rbx = [rax +0x10]          ;   시작
//         eax = [rax +0x18]          ;   개수 (항목 stride 0xC)
//         call 0x49BB80              ; 항목 앞 u16 -> RegionInfo*
//         cmp byte [rax+0x75], 0     ; _isTown
//
//   (2) `dword [[액터+0x68]+0xB0]+0x358 != 0`
//
// 실측으로 평소 걸리는 것은 **(1)** 이다. 마을 한복판에 서 있어도 (2)는 0
// 이었고, 겹친 구역 여섯 중 `Region_Node_Dem_DemenissCathedral._isTown=1`
// 하나가 참을 만들고 있었다. 그 한 바이트를 0 으로 쓰자 조건이 그 자리에서
// 거짓이 됐고, 사용자 실측으로 **마을에서 소환·탑승이 됐다**.
//
// 그래서 여기서 하는 일은 단순하다 — `RegionInfoManager` 의 행을 돌며
// `_isTown`(+0x75) 과 `_limitVehicleRun`(+0x74) 을 0 으로 두고, 끌 때 원래
// 값을 되돌린다. 정적 표라 **세이브에 안 남고** 실행마다 다시 걸어야 한다
// (`call_place_free` 와 같은 모양).
//
// ⚠️ **범위가 넓다.** `IsInTown()` 은 현상금·상점·NPC 일과 등 다른 계통도
// 같이 쓴다(조건식에 `WantedLevel()>=1 && WantedState(Normal) && IsInTown()`
// 같은 것이 있다). 그래서 상시가 아니라 **사용자가 켜고 끄는 토글**이다.
//
// 되돌리기는 **행 번호로** 들고 있는다. 레코드 포인터를 적어 두면 표가 다시
// 잡혔을 때 남의 메모리에 쓰게 된다(clan-roster-volatile-writes 의 교훈).

// ------------------------------------------------------- 실측 오프셋 (2026-09-17)

// 매니저 전역. **RVA 는 갱신마다 밀리므로**(game-update-rva-drift) 이것만 믿지
// 않는다 - 읽은 객체의 클래스 이름을 RTTI 로 대조하고, 표 배치(색인 첫 키 ==
// 첫 레코드 키)까지 맞아야 쓴다. 안 맞으면 RTTI 인스턴스 탐색으로 넘어간다.
// 그래서 이 값이 낡아도 기능은 살아 있고, 다만 힙 전수 탐색 한 번을 더 한다.
//
// 다시 짚는 길은 **표 이름 문자열**이다(밀림과 무관하다). `"regioninfo"` 를
// 참조하는 조회 함수가 전역을 그대로 보여 준다:
//
//   0x0050D712  movzx edi, word ptr [rcx]        ; 행 번호
//   0x0050D715  mov   rbx, [rip+...]             ; <- **전역**
//   0x0050D71C  cmp   edi, dword ptr [rbx + 8]   ; 개수
//   0x0050D72D  mov   rax, qword ptr [rbx + 0x58]; 배열
//
// 스크립트: 스크래치패드 `mgrglobals.py` (인자는 exe + 표 이름들).
//
// 2850: 0x06C2E2F0 (조회 0x49BB80) -> 2944: 0x06D69AD0 (조회 0x50D700). +0x13B7E0.
inline constexpr std::uintptr_t kRegionMgrGlobalRva = 0x06D69AD0;
inline constexpr const char* kRegionMgrClass = "RegionInfoManager";

// RegionInfo 레코드(192바이트). `tools/rtti/fields.py` 로 20개를 짝지었다.
inline constexpr std::size_t kRiStringKey = 0x08;        // 엔진 문자열 객체 포인터
inline constexpr std::size_t kRiLimitVehicleRun = 0x74;  // u8
inline constexpr std::size_t kRiIsTown = 0x75;           // u8

// 액터에서 내려가는 사슬. 첫 칸(char -> 컴포넌트 홀더, +0x68)은 이미
// `reserveslot.h` 의 `kActorSub` 다 - 같은 숫자를 두 이름으로 두지 않는다.
inline constexpr std::size_t kHolderRegionState = 0x1A0;  // 구역 상태 컴포넌트
inline constexpr std::size_t kRegionStateList = 0x38;     // 겹쳐 있는 구역 목록
inline constexpr std::size_t kListBegin = 0x10;
inline constexpr std::size_t kListCount = 0x18;
inline constexpr std::size_t kListStride = 0x0C;   // 항목 앞 u16 = **표의 행 번호**
inline constexpr std::size_t kHolderTownComp = 0xB0;
inline constexpr std::size_t kTownCounter = 0x358;  // i32

// 훑기 한도. 표가 말이 안 되면 거기서 멈춘다.
inline constexpr int kRegionMaxRows = 8192;
inline constexpr int kRegionHereMax = 64;   // 한 자리에 겹칠 수 있는 구역 수
inline constexpr int kTownPatchMax = 1024;  // 되돌릴 자국의 상한

struct TownGateState {
    bool ready = false;
    bool on = false;         // 지금 풀려 있나
    int rows = 0;            // 구역 표 행 수
    int towns = 0;           // `_isTown=1` 인 행
    int runlimits = 0;       // `_limitVehicleRun=1` 인 행
    int here_rows = 0;       // 지금 겹쳐 들어와 있는 구역 수
    bool here_town = false;  // 그중 마을이 있나 (= IsInTown 이 참)
    int town_counter = -1;   // (2)번 갈래. -1 이면 못 읽었다
    char here_name[64] = {}; // 마을 판정을 만드는 구역의 내부 이름
    char note[96] = {};
};

// `player_actor` 가 0 이면 "지금 여기" 부분은 비운다(표 통계는 그대로 낸다).
TownGateState town_gate_state(const mem::Reader& reader,
                              std::uintptr_t player_actor);

// 켜면 표를 풀고, 끄면 원래 값을 되돌린다. 성공하면 참.
bool town_gate_free(const mem::Reader& reader, bool on);

// 모드를 내릴 때 전부 되돌린다.
void town_gate_teardown();

// --------------------------------------------------------- 순수 부분(시험용)

// 되돌릴 자국 하나. **행 번호로** 들고 있는다 - 레코드 포인터를 적어 두면
// 표가 다시 잡혔을 때 남의 메모리에 쓴다.
struct TownPatch {
    std::uint16_t row = 0;
    std::uint8_t off = 0;   // kRiIsTown 또는 kRiLimitVehicleRun
    std::uint8_t old = 0;   // 원래 값(되돌릴 때 쓴다)
};

// 이 행을 건드릴 것인가. 이미 0 인 행은 자국을 남기지 않는다 - 되돌릴 때
// 원래 0 이던 것을 1 로 만들면 안 된다.
bool town_row_gated(std::uint8_t is_town, std::uint8_t limit_vehicle_run);

// 한 행이 낼 자국을 만든다. 낸 개수(0·1·2)를 돌려준다. 순서는 `_isTown`
// 먼저다 - 자국 상한에 걸려 잘릴 때 마을 쪽이 살아남아야 한다.
int town_row_plan(std::uint16_t row, std::uint8_t is_town,
                  std::uint8_t limit_vehicle_run, TownPatch out[2]);

// 표 개수가 말이 되나. 0 이거나 한도를 넘으면 표가 아니다.
bool region_count_plausible(std::uint32_t count);

// 구역 목록 항목의 앞 u16 은 **행 번호**다(조회 함수가 배열 첨자로 그대로
// 쓴다). 표 밖이면 버린다.
bool region_row_in_table(std::uint32_t row, std::uint32_t count);

}  // namespace cdtb::game
