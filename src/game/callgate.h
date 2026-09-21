#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::game {

// 호출 **위치** 관문 우회 — "지붕 위에서는 호출할 수 없습니다" 계열.
//
// `failmessageinfo` 조건(§1, `IsInTown()` 등)과 **다른 층**이다. 그쪽은 부르기
// 전에 조건식을 보고, 이쪽은 호출 처리기가 **지금 서 있는 자리**를 보고
// `eErrNo*` 를 돌려준다. 사용자가 A.T.A.G. 에서 본 문구가 이쪽이다.
//
// 오류 이름 문자열 **바로 뒤에 한국어 설명이 붙어 있다**(2026-09-18 실측).
// 등록 루프(RVA 0x2150380~)가 오류마다 {값 전역 슬롯, 설명, 이름} 을 넘기므로,
// 값 슬롯을 역참조하면 **그 오류를 내는 코드가 한 곳**으로 좁혀진다:
//
//   (exe 1.0.0.2944 기준)
//   실내  모듈+0x6CF7B18 -> RVA 0x9DE7DE  "실내에서는 호출할 수 없습니다."
//   지붕  모듈+0x6CF7B1C -> RVA 0x9DE83B  "지붕 위에서는 호출할 수 없습니다."
//   지역  모듈+0x6CF7B20 -> RVA 0x9DD89B  "호출할 수 없는 지역입니다."
//   위치  모듈+0x6CF7AFC -> RVA 0x9DD6C6  "호출할 수 없는 위치입니다."
//
// **이 경로는 갱신을 안 탄다.** 시작점이 RVA 가 아니라 한국어 문구라서다.
// 2850 -> 2944 에서 넷 다 밀렸는데(+0x7B1E0), 문구로 다시 짚어 몇 초에 끝났다.
// 스크립트: 스크래치패드 `rederive.py` (인자는 exe 하나).
//
// **성벽·지붕 위에서 실제로 뜨는 것은 "위치" 쪽이다**(2026-09-18 사용자 실측).
// 지붕 관문 셋을 다 켜도 성벽 위에서 막혔고, 화면 문구가 "지붕 위에서는…" 이
// 아니라 "호출할 수 없는 위치입니다." 였다 — 게임이 그 자리를 지붕이 아니라
// **유효하지 않은 위치**로 분류한다. 그래서 넷째 관문이 필요했다.
//
//   (2850 기준 발췌 — 2944 에서는 +0x7B1E0 밀렸다)
//   0x009624D5  mov rcx, [액터 + 0x68]
//   0x009624D9  mov rcx, [rcx + 0x48]
//   0x009624DD  call 0x9CD730          ; 위치가 유효한가
//   0x009624E2  test al, al
//   0x009624E4  jne  통과              ; <- 여기 (74 가 아니라 **75**)
//
// 같은 오류를 내는 자리가 하나 더 있다(2850 RVA 0x2B2AD05). 그쪽은 좌표가
// (0,0,0)인지 보는 위생 검사라 우리 경우와 무관해 **안 건드린다**.
//
// 실내·지붕은 한 함수(0x963420~0x963698) 안의 연속된 두 검사이고 **월드 질의**다
// (지붕은 `call 0x7479E0(월드, 위치, 반경)`). 표 값이 아니라서 데이터로는 못
// 끈다. 지역은 다르다 — `[[액터+0x68]+0x1A0]+0x38`(겹쳐 있는 구역 목록)을 들고
// RVA 0x16E4380 을 부르는데, 그 함수가 `RegionInfo._forbiddenMercenaryKeyList`
// 를 읽는다(`towngate.h` 의 전수 조사에서 그 목록이 채워진 구역은
// `Region_Abyss` 하나뿐이었다).
//
// 넷 다 거부로 가는 분기가 **조건 점프 한 바이트**이고(오류로 떨어지는 쪽이
// 아래로 붙었느냐 위로 붙었느냐에 따라 `je` 0x74 이거나 `jne` 0x75 다),
// **정렬된 8바이트 창 안**에 들어간다. 그래서 `skillgate` 의 원자 교환을 그대로
// 쓴다 — 그 한 바이트를 `jmp`(0xEB) 로 바꾸면 오류 대입을 건너뛴다. 확인 폭은
// 쓰기 폭(1바이트)이 아니라 **창 8바이트 전체**다(game-update-rva-drift:
// 갱신마다 영역별로 다르게 밀린다).
//
// ⚠️ **위치가 진짜로 안 되는 자리면** 소환 뒤에 탈것이 지형에 박히거나 곧바로
// 사라질 수 있다. 상시가 아니라 사용자가 켜고 끄는 토글이다.

enum CallGate : int {
    kCallGateIndoor = 0,    // 실내에서는 호출할 수 없습니다
    kCallGateRoof = 1,      // 지붕 위에서는 호출할 수 없습니다
    kCallGateRegion = 2,    // 호출할 수 없는 지역입니다
    kCallGatePosition = 3,  // 호출할 수 없는 위치입니다 (성벽·지붕 위에서 실제로 뜨는 것)
    // 검증기의 탑승 제한(`eErrNoCallVehicleMercenaryRideLimit`). 보스룸 같은 곳이
    // 거는 행동 제한(`PlayerActionLimitDesc._rideLimit`)을 넘긴다 - `bosscall.h`.
    kCallGateRideLimit = 4,
    kCallGateCount = 5,
};

struct CallGateInfo {
    const char* name = "";
    const char* what = "";
    bool on = false;           // 지금 걸려 있나
    bool unsupported = false;  // 원본 바이트가 달라 설치를 거부했다(게임 갱신)
    bool probed = false;       // 한 번이라도 그 자리를 확인해 봤나
    std::uintptr_t site = 0;   // 실제 주소(0 이면 아직 안 봤다)
};

CallGateInfo callgate_info(int gate);

// 켜면 분기를 덮고, 끄면 원본을 되돌린다. 성공하면 참.
// 실패하면 `why` 에 사유 한 줄이 담긴다(널 가능).
//
// **모드(주입 DLL) 전용이다** — 자기 주소공간에 쓴다.
bool callgate_set(int gate, bool on, const char** why = nullptr);

// 창을 열 때 한 번 훑어 "이 빌드에서 쓸 수 있나" 를 미리 정한다. 안 그러면
// 원본이 다른 빌드에서 첫 클릭이 조용히 먹히고 빨간 줄은 다음 프레임에야 뜬다.
void callgate_probe();

// 사용자가 모드를 내릴 때 전부 되돌린다. **해상도 변경 경로에서 부르면 안 된다.**
void callgate_remove_all();

// 관문 하나의 정의(시험용). 없으면 널.
struct GateDef;
const GateDef* callgate_def(int gate);

}  // namespace cdtb::game
