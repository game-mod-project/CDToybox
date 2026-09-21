#pragma once

// @build 1.0.0.2944  스킬 판정 0x362DF0 · 제한 컴포넌트 vtable 0x55B53C8
//   근거: specs/2026-09-21-boss-room-action-limit.md
#include <cstddef>
#include <cstdint>

namespace cdtb::mem {
class Reader;
}

namespace cdtb::game {

// 보스룸에서 탈것 호출 — 휠에서 골라도 **아무 일도 안 나고 문구도 없던** 것.
//
// ❌ **2026-09-21 게임 확인: 보스룸에서 동작 안 함.** 그 판 어디에도 허용
// 스킬그룹 목록이 없었다 - 아래 "보스룸이 그 목록을 건다" 는 가설은 반증됐다.
// 아래의 **코드 사실**(판정 순서 · 제한 목록 배치)은 그대로다. 이 토글은 걷어낼
// 대상이다(specs/2026-09-21-boss-room-action-limit.md §10).
//
// 차단 자리를 파일로 끝까지 따라갔다(2026-09-21, 전부 실측):
//
//   (1) 요청 2166 과 검증기(0x9DD420)는 **"부르기" 동작의 프레임 이벤트**
//       `ClientFrameEventCallMercenary::[1]`(0x7FBA70) 안에서 불린다
//       (`call [rax+0x148]` = `ClientMercenaryClanActorComponent::[41]`).
//       벌판 로그에서 검증기와 송신이 9~23ms 차이로 붙어 있는 것과 맞는다.
//       보스룸에서 검증기가 0회였다는 것은 **동작 자체가 시작되지 않았다**는 뜻이다.
//   (2) 그 동작은 캐릭터 상체 행동 차트 `common_upper_branchset.paac`(탈것 호출
//       입력 `key_callvehicle` 이 든 파일)가 조건 레코드
//       `{종류 0xED, 스킬 키 1505(Skill_CallVehicle), 1}` 로 막고 연다.
//       드래곤은 `{0xED, 1506, 1}`.
//   (3) 조건 종류 0xED 의 평가 함수가 **0x362DF0** 이다(조건 분배기
//       `checkConditionExtend` 0x360BE0 이 `표[종류-0x83]` 을 인자 7개로 부른다.
//       표 초기화 0x383460 이 `+0x350` 에 이 함수를 넣는다). 레코드의 `+4` 를
//       스킬 **키**로 읽는다 — 차트 데이터와 코드가 바이트까지 맞는다.
//   (4) 0x362DF0 은 스킬에 배울 지식이 걸려 있으면(`SkillInfo+0xA0 != 0xFFFF`)
//       액터의 **행동 제한 목록**을 돌며 **허용 스킬그룹 목록이 있는 항목마다**
//       스킬의 그룹(`SkillInfo+0x100`)이 거기 드는지 본다. 하나라도 허용 목록이
//       있는데 맞는 그룹이 없으면 **1(못 씀)** 을 돌려준다.
//       `Skill_CallVehicle` 은 스킬그룹 9개 어디에도 없다(`skillgroupinfo` 전수)
//       — 그래서 허용 목록이 걸리는 순간 **조용히** 막힌다.
//
// 행동 제한 목록 (`PlayerActionLimitDesc` 를 품은 0x38바이트 항목의 배열):
//
//   액터 +0x68 -> +0x40 (ClientCharacterControlActorComponent, vt 0x55B53C8)
//        -> +0x120 (제한 보관 객체) -> +0xD8 배열 · +0xE0 개수
//
//   항목 +0x00 u8  걸어 둔 쪽의 종류 (RideLimit 버프는 6)
//        +0x08 u64 걸어 둔 쪽의 번호
//        +0x10 PlayerActionLimitDesc (역직렬화 0xC12B4C0 과 같은 배치)
//              +0x0 _moveLvLimit · +0x1 _weaponOutLimit · +0x2 _rideLimit
//              +0x3 _rideLimitByIndoor · +0x4 _rideOffLimit
//              +0x5 _unsetLimitOnSequencerControl
//              +0x8 _skillGroupLimitKey {u16* · u32 개수}
//              +0x18 _skillGroupAllowKey {u16* · u32 개수}
//
//   읽는 코드(실측): 0x36E332 이동 · 0x3867A1 무기 · 액터 [88] 0x8DF57B0 탑승
//   (= 검증기의 `eErrNoCallVehicleMercenaryRideLimit`) · 액터 [89] 0x5047D0 실내
//   · 0x372C13 하차 · 0x362DF0 허용/금지.
//
// 그래서 여기서 하는 일은 둘이다(둘 다 사용자가 켜는 토글):
//
//   ① 0x362DF0 을 감싸, 판정 대상이 **탈것·드래곤 호출 스킬일 때만** 그 액터의
//     제한 항목들의 **허용 목록 개수를 잠깐 0 으로 가리고** 원본을 부른 뒤
//     되돌린다. 나머지 판정(스킬 컴포넌트·지식·쿨다운 = 0x21ED110)은 원본이
//     그대로 한다. 되돌리기 전에 배열·개수·목록 포인터가 그대로인지 보고,
//     달라졌으면 **안 쓴다**(엉뚱한 항목에 개수를 써 넣으면 게임이 목록 밖을 읽는다).
//   ② 검증기의 `RideLimit` 분기를 넘긴다 — `callgate` 의 다섯째 관문.
//
// ⚠️ **실측 전인 것**: 보스룸이 실제로 허용 목록을 거는지는 파일로 못 가렸다
// (스테이지 차트 데이터를 못 찾았다). 그래서 제한 목록을 **화면과 로그에 찍는다**
// — 들어가고 나올 때 무엇이 붙고 떨어지는지 계기가 스스로 남긴다.

// ------------------------------------------------------------ 실측 상수

inline constexpr std::uint64_t kSkillCheckFnRva = 0x362DF0;
inline constexpr std::uint32_t kSkillCheckCondType = 0xED;
inline constexpr std::uint32_t kCallVehicleSkillKey = 1505;  // Skill_CallVehicle
inline constexpr std::uint32_t kCallDragonSkillKey = 1506;   // Skill_CallDragon

// 설치 전 **의미로** 확인할 명령들(함수 시작 기준). 프롤로그는 흔해서 확인이
// 못 된다(TROUBLESHOOTING 1.5).
inline constexpr std::size_t kCheckCtlLoadOff = 0xA8;     // mov rdi,[rax+0x120]
inline constexpr std::size_t kCheckLearnCmpOff = 0xB9;    // cmp word [rax+0xA0],si
inline constexpr std::size_t kCheckAllowCmpOff = 0x106;   // cmp dword [rbx+0x30],0
inline constexpr std::size_t kCheckAllowPtrOff = 0x13D;   // mov r9,[rbx+0x28]

inline constexpr std::size_t kActorHolderOff = 0x68;
inline constexpr std::size_t kHolderCtlOff = 0x40;
inline constexpr std::uint64_t kCtlVtRva = 0x55B53C8;     // ClientCharacterControlActorComponent
inline constexpr std::size_t kCtlLimitOff = 0x120;
inline constexpr std::size_t kLimitArrayOff = 0xD8;
inline constexpr std::size_t kLimitCountOff = 0xE0;
inline constexpr std::size_t kLimitEntrySize = 0x38;
inline constexpr std::size_t kLimitAllowPtrOff = 0x28;
inline constexpr std::size_t kLimitAllowCountOff = 0x30;

// 한 액터에 걸린 제한이 이보다 많으면 손대지 않는다(평소 0~2개).
inline constexpr int kMaxLimitEntries = 16;

// ------------------------------------------------------------ 순수 부분

// 항목 하나(0x38바이트)를 푼 것.
struct ActionLimit {
    std::uint8_t source = 0;
    std::uint64_t id = 0;
    std::uint8_t move_lv = 0;
    bool weapon_out = false;
    bool ride = false;
    bool ride_indoor = false;
    bool ride_off = false;
    bool unset_on_seq = false;
    std::uintptr_t limit_ptr = 0;
    std::uint32_t limit_n = 0;
    std::uintptr_t allow_ptr = 0;
    std::uint32_t allow_n = 0;
};

ActionLimit action_limit_decode(const std::uint8_t* raw);

// 이 조건 레코드가 우리가 넘길 호출 스킬 판정인가.
bool bosscall_targets(std::uint32_t cond_type, std::uint32_t skill_key);

// 가릴 항목(허용 목록이 찬 것)의 첨자를 `out` 에 담고 개수를 돌려준다.
// 항목 수가 0 이거나 `kMaxLimitEntries` 를 넘으면 0 (손대지 않는다).
int bosscall_hide_plan(const ActionLimit* e, int n, int* out, int cap);

// 가리기 직전과 원본 호출 직후의 모습. 둘이 같아야만 되돌린다.
struct LimitShape {
    std::uintptr_t array = 0;
    std::uint32_t count = 0;
    std::uintptr_t allow_ptr[kMaxLimitEntries] = {};
};

bool bosscall_can_restore(const LimitShape& before, const LimitShape& after);

// 화면·로그용 한 줄. `buf` 에 쓰고 쓴 길이를 돌려준다.
std::size_t action_limit_text(const ActionLimit& e, char* buf, std::size_t cap);

// ------------------------------------------------------------ 게임 쪽

struct BossCallState {
    bool on = false;          // 켜져 있나(훅이 걸려 있나)
    bool unsupported = false; // 설치 대조가 실패했다(게임 갱신)
    bool player = false;      // 주 플레이어의 제한 목록을 읽었나
    int limits = 0;           // 걸린 제한 수
    int with_allow = 0;       // 그중 허용 목록이 찬 것
    int ride = 0;             // _rideLimit
    int ride_off = 0;         // _rideOffLimit
    std::uint32_t hidden = 0; // 호출 판정에서 허용 목록을 가린 횟수
    std::uint32_t passed = 0; // 그때 원본이 통과(0)를 준 횟수
    std::uint32_t blocked = 0;// 가렸는데도 막힌 횟수 - 허용 목록 말고 다른 것
    ActionLimit entry[kMaxLimitEntries];
    int shown = 0;            // entry 에 담은 수
};

// 켜기/끄기. 켜면 0x362DF0 에 훅을 걸고 검증기 탑승 제한 관문도 켠다.
bool bosscall_set(const mem::Reader& reader, bool on, const char** why = nullptr);

// 주 플레이어의 제한 목록을 읽어 담는다(읽기만 한다).
BossCallState bosscall_state(const mem::Reader& reader);

// 매 프레임. 켜져 있거나 `log_limits` 면 제한 목록이 **바뀔 때만** 로그에 남긴다
// (보스룸 출입이 저절로 찍힌다).
void bosscall_tick(const mem::Reader& reader, bool log_limits);

}  // namespace cdtb::game
