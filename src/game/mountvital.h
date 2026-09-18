#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"

namespace cdtb::game {

// 탈것(드래곤·A.T.A.G.·와이번 등)의 체력·스태미나 읽기 · 바꾸기 · 고정.
//
// **플레이어와 같은 사슬이다**(2026-09-18 실측, exe 1.0.0.2944). `player.h` 의
// 게이지 배열 체인이 탈것 액터에도 그대로 먹는다:
//
//     char +0x68 -> actor +0x20 -> marker +0x18 -> root +0x58 = 게이지 배열
//     항목 k = 배열 + k*0x90 : +0x00 i32 타입 · +0x08 i64 현재 · +0x18 i64 최대
//
// 값은 **1000배 척도**다. 블랙스타의 최대 체력 2,500,000 은 명부 레코드의
// "생명 2500"(`+0xA0`)과 정확히 맞는다.
//
// 실측값(참고):
//
//     블랙스타(드래곤)  체력 2,500,000  스태미나 300,000
//     와이번            체력   600,000  스태미나 150,000
//     무역 마차         체력 5,000,000  스태미나 —
//
// ---- 체력은 덮인다, 스태미나는 안 덮인다 (이 모듈이 둘로 갈리는 이유)
//
// 써 넣고 0.4초 간격으로 지켜본 결과다:
//
//     체력 현재를 2,500,000 으로 -> 2.0초 유지 -> 843,400 으로 복귀
//     체력 최대를 5,000,000 으로 -> 1.6초 유지 -> 2,500,000 으로 복귀
//     스태미나 최대를 500,000 으로 -> **그대로 유지**
//
// 즉 **체력은 약 2초마다 권위 쪽이 덮어쓴다.** `player.cpp` 가 "게이지가 권위라
// 양쪽 realm 을 다 freeze 해야 한다" 고 적어 둔 것과 같은 현상으로 보인다.
// 그래서 체력은 **매 틱 다시 쓰는 고정**이 필요하고, 스태미나는 한 번 쓰면 된다.
//
// ---- 대상은 **핸들**로 들고 있는다
//
// 액터 주소는 휘발성이다(clan-roster-volatile-writes: 예전 주소에 써서 게임을
// 팅긴 적이 있다). 고정 대상은 핸들로 기억하고, 매 틱 살아있는 액터 목록에서
// 그 핸들을 **다시 찾아** 주소를 얻는다. 못 찾으면 그 틱은 조용히 건너뛴다.
//
// ---- 안 건드리는 것
//
// 타입 **17·18**(발열·자연발화)과 **48**(탈것 화염)은 `player.h` 가 "핀 금지" 로
// 표시해 둔 위험 타입이다. 여기서도 읽지도 쓰지도 않는다.

// 게이지 배열 배치
inline constexpr std::size_t kGaugeStride = 0x90;
inline constexpr std::size_t kGaugeType = 0x00;   // i32
inline constexpr std::size_t kGaugeCur = 0x08;    // i64
inline constexpr std::size_t kGaugeMax = 0x18;    // i64
inline constexpr int kGaugeMaxEntries = 24;

// 액터에서 게이지 배열까지
inline constexpr std::size_t kMvActorSub = 0x68;
inline constexpr std::size_t kMvMarker = 0x20;
inline constexpr std::size_t kMvRoot = 0x18;
inline constexpr std::size_t kMvArray = 0x58;

// 게이지 타입
inline constexpr std::uint32_t kTypeHealth = 0;
inline constexpr std::uint32_t kTypeStamina = 22;

// 값 상한. 표에서 본 제일 큰 것이 마차의 5,000,000 이라 넉넉히 잡는다.
// 상한을 두는 이유는 화면 입력이 엔진 칸을 넘겨 음수로 돌지 않게 하려는 것이다.
inline constexpr std::int64_t kMountVitalMax = 100'000'000;

struct MountVital {
    std::uintptr_t actor = 0;
    std::uint32_t handle = 0;
    std::string name;                 // 표시명(없으면 내부 이름)
    std::uint16_t merc_row = 0xFFFF;  // 동반자 타입 행
    std::uintptr_t gauges = 0;        // 게이지 배열(0 이면 못 잡음)
    bool ok = false;                  // 항목[0].타입 == 0 게이트를 통과했나
    int hp_idx = -1;
    int sta_idx = -1;
    std::int64_t hp_cur = 0, hp_max = 0;
    std::int64_t sta_cur = 0, sta_max = 0;
};

// 월드에 나와 있는 **동반자** 중 게이지 배열이 잡히는 것만.
// 살아있는 액터 목록(`actors.h`)의 캐시를 쓴다 - 힙을 다시 안 훑는다.
std::vector<MountVital> mount_vitals(const mem::Reader& reader);

// 고정 설정. `-1` 은 "그 칸은 안 건드린다" 는 뜻이다.
struct MountPin {
    std::uint32_t handle = 0;   // 대상. 0 이면 꺼진 것이다
    std::int64_t hp_cur = -1;
    std::int64_t hp_max = -1;
    std::int64_t sta_cur = -1;
    std::int64_t sta_max = -1;
};

// 한 번만 쓴다(고정 없이). 성공하면 참.
bool mount_vital_write(const mem::Reader& reader, std::uint32_t handle,
                       const MountPin& what);

void mount_pin_set(const MountPin& pin);
MountPin mount_pin_get();
void mount_pin_clear();

// 매 틱 적용. **모드(주입 DLL)에서만.** 대상이 없으면 조용히 돌아간다.
void mount_pin_tick(const mem::Reader& reader);

// --------------------------------------------------------- 순수 부분(시험용)

// 그 게이지 타입을 건드려도 되나. 17·18·48 은 위험 타입이라 거짓.
bool mount_type_safe(std::uint32_t type);

// 항목 k 의 배열 안 오프셋.
std::size_t gauge_offset(int index);

// 화면에서 들어온 값을 칸에 맞게 자른다. 음수는 0, 상한 초과는 상한.
std::int64_t mount_clamp(std::int64_t value);

// 이 칸을 쓸 것인가(-1 은 안 건드림).
bool mount_pin_wants(std::int64_t field);

// 고정이 켜져 있나(대상이 있고 쓸 칸이 하나라도 있나).
bool mount_pin_active(const MountPin& pin);

}  // namespace cdtb::game
