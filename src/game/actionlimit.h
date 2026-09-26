#pragma once

// @build 1.0.0.2976  제한 컴포넌트 vtable +0x98 -> 0x55B5460 (RTTI, 2026-09-26).
//   근거: specs/2026-09-26-game-update-2976.md §10
// @build 1.0.0.2949  제한 컴포넌트 vtable 0x55B53C8 - RTTI(ClientCharacterControlActor
//   Component)로 2949 에서도 같은 자리임을 확인했다(2026-09-22).
// @build 1.0.0.2944  항목 0x38바이트 - 런타임 구조라 2949 에서는 아직 안 쟀다.
//   근거: specs/2026-09-21-boss-room-action-limit.md
#include <cstddef>
#include <cstdint>

namespace cdtb::mem {
class Reader;
}

namespace cdtb::game {

// 캐릭터 **행동 제한 목록** 읽기 — 읽기만 한다. 게임에 아무것도 쓰지 않는다.
//
// 보스룸 탈것 호출 조사(2026-09-21)에서 나왔다. 그때 이 목록의 **허용 스킬그룹
// 목록**이 호출 스킬 판정을 막는다고 보고 넘기는 토글(#91 "보스룸에서도 호출")을
// 넣었는데, 게임 한 판에서 **그 목록이 없었다**(반증, #92). 토글은 걷어냈고
// 이 읽기만 남긴다 — 어느 곳이 무슨 제한을 거는지 화면과 로그로 볼 수 있다.
//
// 목록 (전부 실측, 2944):
//
//   액터 +0x68 -> +0x40 (ClientCharacterControlActorComponent, vt 0x55B53C8)
//        -> +0x120 (제한 보관 객체) -> +0xD8 배열 · +0xE0 개수
//
//   항목 0x38바이트:
//        +0x00 u8  걸어 둔 쪽의 종류 (RideLimit 버프는 6)
//        +0x08 u64 걸어 둔 쪽의 번호
//        +0x10 PlayerActionLimitDesc (역직렬화 0xC12B4C0 과 같은 배치)
//              +0x0 _moveLvLimit · +0x1 _weaponOutLimit · +0x2 _rideLimit
//              +0x3 _rideLimitByIndoor · +0x4 _rideOffLimit
//              +0x5 _unsetLimitOnSequencerControl
//              +0x8 _skillGroupLimitKey {u16* · u32 개수}
//              +0x18 _skillGroupAllowKey {u16* · u32 개수}
//
//   읽는 코드: 0x36E332 이동 · 0x3867A1 무기 · 액터 [88] 0x8DF57B0 탑승(= 검증기의
//   `eErrNoCallVehicleMercenaryRideLimit`) · 액터 [89] 0x5047D0 실내 · 조건 표
//   종류 0x1A4(0x372B70) 하차 · 조건 표 종류 0xED(0x362DF0) 허용/금지 스킬그룹.
//
// 0x362DF0 은 허용 목록이 하나라도 있으면 그룹 없는 스킬(탈것 호출 스킬 포함)을
// 문구 없이 막는다 — 이것은 **코드 사실**이다. 보스룸이 그 목록을 건다는 것만 틀렸다.
//
// 보스룸 한 판에서 본 것: 근처에서 `탑승금지` 둘(출처 0/1, 번호 0x24B), 들어가
// 새로 생긴 액터에는 제한이 **없다**. 호출 동작은 여전히 시작되지 않는다.

inline constexpr std::size_t kActorHolderOff = 0x68;
inline constexpr std::size_t kHolderCtlOff = 0x40;
inline constexpr std::uint64_t kCtlVtRva = 0x55B5460;     // ClientCharacterControlActorComponent
inline constexpr std::size_t kCtlLimitOff = 0x120;
inline constexpr std::size_t kLimitArrayOff = 0xD8;
inline constexpr std::size_t kLimitCountOff = 0xE0;
inline constexpr std::size_t kLimitEntrySize = 0x38;
inline constexpr std::size_t kLimitAllowPtrOff = 0x28;
inline constexpr std::size_t kLimitAllowCountOff = 0x30;

// 한 액터에 걸린 제한이 이보다 많으면 읽지 않는다(실측은 0~2개).
inline constexpr int kMaxLimitEntries = 16;

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

// 화면·로그용 한 줄. `buf` 에 쓰고 쓴 길이를 돌려준다.
std::size_t action_limit_text(const ActionLimit& e, char* buf, std::size_t cap);

struct ActionLimitState {
    bool player = false;      // 주 플레이어의 제한 목록을 읽었나
    int limits = 0;           // 걸린 제한 수
    int with_allow = 0;       // 그중 허용 목록이 찬 것
    int ride = 0;             // _rideLimit
    int ride_off = 0;         // _rideOffLimit
    ActionLimit entry[kMaxLimitEntries];
    int shown = 0;            // entry 에 담은 수
};

// 주 플레이어의 제한 목록을 읽어 담는다.
ActionLimitState action_limit_state(const mem::Reader& reader);

// 매 프레임. `log_limits` 면 제한 목록이 **바뀔 때만** 한 줄 남긴다
// (보스룸 같은 곳의 출입이 저절로 찍힌다). 아니면 아무것도 안 한다.
void action_limit_tick(const mem::Reader& reader, bool log_limits);

}  // namespace cdtb::game
