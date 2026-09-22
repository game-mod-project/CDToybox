#pragma once

// @build 1.0.0.2949  kWheelUiFnRva 만 옮겼다(0x10BE8B0 -> 0x10BE8C0). 나머지는 2949 에서
//   그대로임을 의미로 대조했다(2026-09-22): 검증기 +0x5E 가 새 조회 0x2146120 을 부르고
//   오류 슬롯 여섯을 2944 와 같은 오프셋에서 읽는다 · 앞단 +0x14C 가 검증기를 부른다 ·
//   관리자·관문 바이트 여섯이 맞고 관리자 전역이 0x6D691B0 그대로 · 휠 UI +0x495 가
//   0x6A6555C 를 읽는다. 근거: specs/2026-09-22-game-update-2949.md §6.

#include <cstddef>
#include <cstdint>

#include "mem/reader.h"

namespace cdtb::game {

// 탈것 호출 **클라이언트 검증기** 진단 — "보스룸에서 왜 안 불리는가".
//
// ---------------------------------------------------------------------------
// 왜 이것이 필요한가
// ---------------------------------------------------------------------------
// 보스룸에서 A.T.A.G.·드래곤·말이 **전부** 안 불리고 문구도 없다. 지금까지
// 배제된 것(`specs/2026-09-16-vehicle-place-and-dismount.md` §8-1): 조건층 ·
// UI 층 · 탈것 종류별 조건 · 예약 슬롯 층 · 서버 거부(메시지가 아예 안 나간다).
//
// 그리고 **이름으로 고른 후보 둘을 패치했다가 둘 다 무반응**이었다(§8-2).
// `AICondition_BlockByExclusiveStage` 와 `AICondition_CheckIsRideLimit` 이다.
// 자리가 틀린 것이 아니라 **조건이 틀렸다** - 패치는 로그로 걸린 것을 확인했다.
//
// ⚠️ 2026-09-21 정정 — "조건이 틀렸다" 가 아니라 **조건 체계가 달랐다.** 그 둘은
// AI 차트의 조건이고, 캐릭터 행동 차트가 쓰는 조건 표(0x6DD2870)에는 없다. 이
// 검증기는 휠이 아니라 **"부르기" 동작의 프레임 이벤트**
// (`ClientFrameEventCallMercenary::[1]` 0x7FBA70 의 `call [rax+0x148]`)가 부르고,
// 그 동작을 여는 조건은 `{0xED, Skill_CallVehicle}`(평가 함수 0x362DF0)다. 보스룸이
// 그 조건의 허용 스킬그룹 목록으로 막는다는 가설은 **게임에서 반증**됐다(#92) —
// `actionlimit.h` · `specs/2026-09-21-boss-room-action-limit.md` §10.
//
// 그래서 이번에는 **고르기 전에 잰다.** 오류 이름 -> 값 슬롯 -> 읽는 곳
// (`TROUBLESHOOTING.md` §4.31 의 기법)으로 내려가니 거부가 한 함수에 모여 있다.
//
//   RVA 0x9DD420..0x9DD756 (822 B) — 클라이언트 호출 검증기
//     +0x06B  eErrNoInvalidMercenaryNo
//     +0x125  eErrNotExistVehicle
//     +0x146  eErrNoCallVehicleMercenaryRideLimit
//     +0x186  eErrNoInvalidMercenaryNo
//     +0x228  eErrNoCallVehicleMercenaryMovableNavigation
//     +0x2A6  eErrNoCallVehicleInvalidPosition
//
// **마지막 줄이 우리가 이미 푸는 관문이다** — `callgate.h` 의 "위치" 가
// 2850 에서 0x9624E6 이었고 2944 델타 +0x7B1E0 을 더하면 0x9DD6C6 으로
// 정확히 맞는다. 나머지 다섯은 그 형제다.
//
// 여섯 중 어느 것이 보스룸에서 나오는지는 **추측하지 않는다.** 훅을 걸어
// 그 자리에서 찍는다.
//
// ---------------------------------------------------------------------------
// 인자와 반환 (실측 - 호출자 둘을 **둘 다** 읽었다, §1.14)
// ---------------------------------------------------------------------------
//   0x009E4E5F  lea  rdx, [rbp+0x7f]   ; arg2 = 오류 out (u32*)
//   0x009E4E66  call 0x9DD420          ; 인자 다섯 (다섯째는 스택 u16)
//   0x009E4E6B  mov  edx, [rbp+0x7f]   ; 호출자가 그 값을 읽어 0 이면 성공
//
//   0x009E110C  같은 꼴. 이쪽은 반환값(= rsi 그대로)을 역참조해 읽는다.
//
// 콜리는 `[rbp+0x50]`(= 스택 첫 인자)까지 읽으므로 **다섯**으로 선언해야
// 한다. 넷으로 걸면 다섯째가 우리 스택의 쓰레기가 된다(§1.14 - 같은 날 세 번
// 팅겼던 그것).
//
// arg3 은 **용병 번호**다 - 콜리가 `mov rdx,r8` 한 뒤 번호->레코드 조회
// (`kSpawnLookupRva`)에 그대로 넘긴다.

// 이 검증기가 낼 수 있는 거부 사유. **값은 런타임에 등록**되므로 상수가 아니다
// - 전역 슬롯을 읽어 비교한다. 슬롯 RVA 는 등록 루프에서 정적으로 떴다.
struct CallCheckReason {
    std::uint64_t slot_rva;
    const char* name;
};
inline constexpr CallCheckReason kCallCheckReasons[] = {
    {0x6CF6F60, "eErrNoInvalidMercenaryNo"},
    {0x6CF7AE4, "eErrNotExistVehicle"},
    {0x6CF7B28, "eErrNoCallVehicleMercenaryRideLimit"},
    {0x6CF7B24, "eErrNoCallVehicleMercenaryMovableNavigation"},
    {0x6CF7AFC, "eErrNoCallVehicleInvalidPosition"},
    // 아래 둘은 검증기 **앞단**(`kWheelPreFnRva`)이 낸다(2026-09-21). 등록 코드
    // `lea rcx,[슬롯] / lea r8,[설명] / lea rdx,[이름] / call 0x1427810` 으로
    // 짝지었고, 같은 방법이 대조군 `0x6CF7AFC` 에서 정확히 맞았다.
    //   0x21FE601 -> "FocusActor를 찾을 수 없습니다."
    //   0x21FED36 -> "이미 호출된 용병입니다."
    {0x6CF6ED8, "eErrNotFoundFocusActor"},
    {0x6CF6F7C, "eErrNoAlreadySummonedMercenary"},
};
inline constexpr std::size_t kCallCheckReasonCount =
    sizeof(kCallCheckReasons) / sizeof(kCallCheckReasons[0]);

// 검증기 함수와, 그 함수임을 **의미로** 확인할 자리.
//
// 프롤로그(`48 89 5C 24 10 …`)는 이 실행 파일에 **4,977곳**이라 확인에 못 쓴다
// (§1.5 그대로 - 흔한 프롤로그는 확인이 아니다). 대신 `+0x5E` 의 `call` 이
// **번호->레코드 조회**를 향하는지 본다. 그 조회는 `spawnguard_site.h` 가 이미
// 실측으로 들고 있는 값이라, 갱신 때 둘이 같이 움직이거나 같이 걸린다.
inline constexpr std::uint64_t kCallCheckFnRva = 0x9DD420;
inline constexpr std::size_t kCallCheckLookupCallOff = 0x5E;

// 오류 코드를 이름으로 옮긴다. `values[i]` 는 `kCallCheckReasons[i].slot_rva`
// 에서 읽은 값이다. 못 찾으면 **nullptr** - 이름을 지어내지 않는다.
inline const char* callcheck_reason_name(std::uint32_t err,
                                         const std::uint32_t* values,
                                         std::size_t n) {
    // **`err == 0` 을 먼저 거르는 것이 핵심이다.** 0 은 *성공*이고, 등록 전
    // 슬롯도 0 이다. 둘이 만나면 "성공인데 첫 칸 이름이 붙는" 거짓말이 된다.
    if (err == 0 || values == nullptr) return nullptr;
    for (std::size_t i = 0; i < n && i < kCallCheckReasonCount; ++i) {
        if (values[i] == err) {
            return kCallCheckReasons[i].name;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// **들어온 횟수를 무조건 센다** (2026-09-21)
// ---------------------------------------------------------------------------
// 첫 실측에서 보스룸 시도 뒤 거부 줄이 **한 줄도 없었다**. 그런데 그 침묵은
// 서로 정반대인 두 가지를 못 가른다:
//
//   (가) 검증기를 **아예 안 거쳤다**  -> 차단이 이 **위**(휠·조건층)에 있다
//   (나) 거쳤고 **통과(0)했다**        -> 차단이 이 **아래**(서버·모션층)에 있다
//
// 거부만 세는 계기로는 (가)와 (나)를 영원히 못 가른다. 그래서 오류값과 무관하게
// **들어온 횟수 자체**를 센다. 이 한 칸이 이번 조사의 전부다.
struct CallCheckCounts {
    std::uint32_t calls;   // 검증기에 들어온 총 횟수
    std::uint32_t pass;    // 그중 오류 0 (통과)
    std::uint32_t reject;  // 그중 오류 != 0 (거부)
};

// 한 번의 호출을 센다. `err` 은 검증기가 낸 값(0 = 통과).
//
// **`calls` 는 오류값과 무관하게 는다.** 통과도 호출이다 - 여기서 조건을 달면
// 계기가 다시 (가)/(나)를 못 가르는 옛 상태로 돌아간다.
inline void callcheck_count_one(std::uint32_t err, CallCheckCounts* c) {
    if (c == nullptr) return;
    ++c->calls;
    if (err == 0) {
        ++c->pass;
    } else {
        ++c->reject;
    }
}

// ---------------------------------------------------------------------------
// 휠의 앞 두 단 (2026-09-21, 파일로 거슬러 올라감)
// ---------------------------------------------------------------------------
// ⚠️ **아래 "휠 UI" 는 틀린 짐작이었다 (같은 날 반증).** 벌판 대조군에서 탈것이
// 정상으로 불렸는데(검증기 통과·2166 송신) `ContentSlotMenu::[18]` 은 **한 번도
// 안 불렸다.** 호출 그래프와 클래스 이름만 보고 탈것 휠의 처리기라고 단정했다.
// 앞단(0x9E0FC0)의 남은 호출자는 `UIGamePlayControlRootPet` 아래 0x10A0800 이다
// (이쪽 첫 관문은 다른 전역 `[[0x6D69208]]` 의 +0x50 을 묻는다).
//
// ⚠️ **관문①(수명 단계 0x10)도 보스룸 차단의 원인이 아니다.** `+0x5E` 는 한
// 방향으로만 나아가는 수명 단계다 - 0x1436AA0..0x1436AD0 의 전이 함수가 1->2->4->8->0x10
// 을 **앞 단계에서만** 넘긴다. 보스룸에 들어가면 새 액터가 8 로 시작해 약 27초 뒤
// 0x10(활성)이 된다(밖에서 읽기로 실측). 관문①은 그 27초만 막는다.
//
// 탈것 요청(2166)은 `ClientFrameEventProcessor::[5]`(0x4EDC40)가 만드는
// `ClientFrameEventCallMercenary` 가 보낸다 - "부르기" **동작의 프레임 이벤트**다.
// 보스룸 차단이 동작층에 있을 수 있다. 확정은 아니다.
//
// 보스룸 실측(대조군 포함): 벌판에서는 검증기 진입 1·통과, 휠 요청(2166) 나감,
// 서버 응답 옴. 보스룸에서는 **셋 다 없다** -> 차단이 검증기 **위**다.
// 호출 그래프를 거슬러 가며 vtable 칸을 RTTI 로 풀었다:
//
//   UIGamePlayControlRoot_ContentSlotMenu::vtable[18]  0x10BE8B0  (휠 UI)
//     0x10BE8D5  [this+0x1C8] == -1                -> 나감 (고른 칸 없음)
//     0x10BE8E7  0x64F490 -> 0x8B4480(관리자+0x50)  -> 못 찾으면 **문구 없이** 나감 (관문①)
//     0x10BE8F8  [this+0x158] 로 갈림 - 2 가 탈것
//     0x10BED36  call 0x9E0FC0                      (앞단)
//                  0x8B4510(관리자+0x58 -> +0xD8) 실패 -> eErrNotFoundFocusActor
//                  레코드 +0x148 != FFFF 이고 +0x50 != 0 -> eErrNoAlreadySummonedMercenary
//                  call 0x9DD420                     (검증기)
//     0x10BED45  오류 == [0x6A6555C] 이면 문구 없음, 아니면 등록표에서 문구
//
//   통과하면 ClientFrameEventCallMercenary::vtable[1] 0x7FBA70 -> 0x7FCC10
//   -> 0xBE31F0 이 메시지 2166(`mov ecx,0x876`)을 만든다 - 캡처 헤더와 바이트까지 맞음.
//
// **관문①에는 훅을 안 건다.** `0x64F490` 은 호출자가 545곳인 흔한 도우미다. 대신
// 탈것 칸이고 고른 칸이 있는데 앞단에 **안 왔다면** 그 사이 출구는 관문① 하나뿐이다
// (0x10BE8E7 ~ 0x10BED36 명령 순서로 확인). 그래서 결과는 훅 없이도 가려진다.
//
// 인자 수(§1.14): 휠 UI 는 `this` 하나(rdx/r8/r9 를 읽기 전에 먼저 쓰고 스택 인자를
// 안 읽는다). 앞단은 셋(this · 오류 out · 번호) - 호출자 둘 다 rcx/rdx/r8 만 채우고
// 콜리가 스택 인자를 안 읽는다. 휠은 오류를 반환값이 아니라 **넘겨준 칸**에서 읽는다.
// 1.0.0.2949: `UIGamePlayControlRoot_ContentSlotMenu@uiCommonScript` vtable(0x56EBA00)의
// [18] 이 0x10BE8C0 이고, 그 +0x486 이 앞단 0x9E0FC0 을 부른다(2944 와 같은 오프셋).
inline constexpr std::uint64_t kWheelUiFnRva = 0x10BE8C0;   // 2944 0x10BE8B0
inline constexpr std::size_t kWheelUiPreCallOff = 0x486;   // call kWheelPreFnRva
inline constexpr std::size_t kWheelUiSlotOff = 0x1C8;      // i32, -1 = 고른 칸 없음
inline constexpr std::size_t kWheelUiKindOff = 0x158;      // u8, 칸 종류
inline constexpr std::uint8_t kWheelKindVehicle = 2;
inline constexpr std::uint64_t kWheelPreFnRva = 0x9E0FC0;
inline constexpr std::size_t kWheelPreValidatorCallOff = 0x14C;  // call kCallCheckFnRva
inline constexpr std::uint64_t kSilentErrorRva = 0x6A6555C;      // 이 값과 같으면 문구 없음

// 관문①·②가 묻는 **관리자의 칸**. 결말 줄에 그 포인터와 vtable 을 **읽기만** 해서
// 붙인다 - 게임 함수는 부르지 않는다(관문의 `vtable[24](obj,4,0x10)` 은 잠금·참조
// 계열일 수 있어 부작용이 없다고 장담 못 한다).
//
// 왜: 탈것 권위 문서(`specs/2026-09-19-mount-vitals-authority.md` §7-2)의 실측 -
// **스테이지 전환이 플레이어 액터를 재생성**하고 옛 액터 메모리가 풀린다. 관문①은
// 관리자 `+0x50`(주 플레이어)이 살아 있는지를 묻는다. 보스룸에서 그 칸이 비었는지,
// 다른 클래스로 바뀌었는지가 벌판 줄과 나란히 놓으면 바로 보인다.
//
// 오프셋은 설치 때 **바이트로** 대조한다(2944):
//   0x64F490+0x15  48 8B 0D rel32 -> 0x6D691B0   관리자 전역
//   0x64F490+0x1C  48 8B 49 30                   +0x30
//   0x64F490+0x20  call 0x8B4480                 관문①
//   0x8B4480+0x1A  48 8B 79 50                   주 플레이어 +0x50
//   0x8B4510+0x23  48 8B 79 58                   포커스 +0x58
//   0x8B4510+0xEF  4C 8B B0 D8 00 00 00          -> +0xD8
// 하나라도 다르면 칸 읽기만 끄고 결말은 그대로 찍는다.
inline constexpr std::uint64_t kFocusQueryFnRva = 0x64F490;
inline constexpr std::uint64_t kMainPlayerCheckFnRva = 0x8B4480;
inline constexpr std::uint64_t kFocusActorCheckFnRva = 0x8B4510;
inline constexpr std::uint64_t kFocusMgrGlobalRva = 0x6D691B0;
inline constexpr std::size_t kFocusMgrOff = 0x30;
inline constexpr std::size_t kMainPlayerOff = 0x50;
inline constexpr std::size_t kFocusCtlOff = 0x58;
inline constexpr std::size_t kFocusActorOff = 0xD8;

// 휠 UI 한 번의 결말. 순서가 곧 코드의 순서다.
enum class WheelVerdict : int {
    NoSlot = 0,      // 고른 칸 없음 - 관문① 앞에서 나감
    NotVehicle,      // 탈것 칸이 아니다 (다른 갈래)
    BlockedAtGate1,  // 탈것 칸인데 앞단에 안 옴 -> 관문①(주 플레이어)
    PreRejected,     // 앞단이 오류를 냈다
    PrePassed,       // 앞단 통과 - 검증기까지 갔다
};
inline constexpr int kWheelVerdictCount = 5;

inline WheelVerdict wheel_verdict(std::int32_t slot, std::uint8_t kind,
                                  bool pre_entered, std::uint32_t pre_err) {
    if (slot == -1) return WheelVerdict::NoSlot;
    if (kind != kWheelKindVehicle) return WheelVerdict::NotVehicle;
    // 여기서 "앞단에 안 왔다" 를 관문①로 읽는 근거는 위 명령 순서다 - 탈것
    // 갈래에서는 관문① 뒤에 다른 출구가 없다.
    if (!pre_entered) return WheelVerdict::BlockedAtGate1;
    return pre_err == 0 ? WheelVerdict::PrePassed : WheelVerdict::PreRejected;
}

inline const char* wheel_verdict_text(WheelVerdict v) {
    switch (v) {
        case WheelVerdict::NoSlot:
            return "고른 칸 없음 - 관문① 앞에서 나감";
        case WheelVerdict::NotVehicle:
            return "탈것 칸이 아님";
        case WheelVerdict::BlockedAtGate1:
            return "앞단에 안 옴 - 관문①(주 플레이어 조회)에서 문구 없이 막힘";
        case WheelVerdict::PreRejected:
            return "앞단이 거부";
        case WheelVerdict::PrePassed:
            return "앞단 통과 - 검증기까지 감";
    }
    return "?";
}

// 훅을 건다. 거부는 **사유까지** 찍고, 통과는 **수만** 센다(줄 수 제한).
// 검증기 하나에 더해 휠 UI·앞단도 같이 건다 - 전부 `call_diag` 뒤다.
bool callcheck_diag_install(const mem::Reader& reader);
bool callcheck_diag_installed();
// 마지막으로 본 거부 코드(0 이면 아직 없음). 화면·시험용.
std::uint32_t callcheck_last_error();
// 지금까지 센 것. 화면·시험용.
CallCheckCounts callcheck_counts();
// 프레임마다 부른다. 첫 호출을 한 번 알리고, 그 뒤로는 수가 달라졌을 때만
// 요약을 찍는다(되풀이되는 줄은 로그를 묻는다 - TROUBLESHOOTING 6.19).
void callcheck_tick_report();

}  // namespace cdtb::game
