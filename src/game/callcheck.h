#pragma once

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
// 배제된 것(`specs/2026-09-16-vehicle-place-and-dismount.md` §6-1): 조건층 ·
// UI 층 · 탈것 종류별 조건 · 예약 슬롯 층 · 서버 거부(메시지가 아예 안 나간다).
//
// 그리고 **이름으로 고른 후보 둘을 패치했다가 둘 다 무반응**이었다(§6-2).
// `AICondition_BlockByExclusiveStage` 와 `AICondition_CheckIsRideLimit` 이다.
// 자리가 틀린 것이 아니라 **조건이 틀렸다** - 패치는 로그로 걸린 것을 확인했다.
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

// 훅을 건다. 거부는 **사유까지** 찍고, 통과는 **수만** 센다(줄 수 제한).
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
