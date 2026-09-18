#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mem/reader.h"

namespace cdtb::game {

// 동반자 **휠 슬롯 등록/해제** — 훅 없이.
//
// 동반자마다 "어느 휠 칸에 올려놨는가" 를 적는 u16 이 있다(등록 항목 +0x148).
// 0xFFFF 면 아무 칸에도 안 올라가 있고, 그러면 소환 판정의 마지막 관문이
// 빈손이 되어 거부된다. 드래곤이 딱 그 상태였다(`wheelfill.h`).
//
// ---- 주인 객체를 직접 잡는다 (2026-09-18 실측)
//
// 기존 구현(`dragondiag.cpp`)은 조회 함수를 **후킹해서** 첫 인자로 오는 주인을
// 받았다. "표의 주인은 호출 인자로만 온다" 고 적혀 있었는데, 실제로는 플레이어
// 컴포넌트 홀더에 그대로 달려 있다:
//
//     주인 = [[플레이어 char + 0x68] + 0x110]
//
// 그래서 **훅이 필요 없다.** 게임이 갱신돼 `dragondiag` 가 통째로 꺼져 있어도
// (2944 가 그 상태다) 이 경로는 산다. 고정 RVA 를 하나도 안 쓰기 때문이다 —
// 전부 구조 오프셋이다.
//
// ---- 표 걷기 (`dragondiag::find_category_list` 와 같다)
//
//     map = 주인 + 0x18
//       +0x30 버킷 수 · +0x34 살아있는 수 · +0x40 버킷표(0x100 간격) · +0x48 레코드
//     버킷: [0] 항목 수 · +8 부터 {u32 카테고리, u32 색인}
//     레코드: +0x08 항목 배열(포인터 stride 8) · +0x10 개수
//     항목: +0x20 종행 · +0xA0 생명 · +0x148 휠 칸 · +0x158 성장치
//
// 실측으로 확인한 배치(카테고리별 항목 수): 말 8 · 드래곤 2 · A.T.A.G. 1 ·
// 특수 탑승물 15 · 마차 1 · 동물 9.
//
// ---- 쓰기는 유지된다
//
// 체력과 달리 **게임이 되돌리지 않는다**(2.4초간 지켜봄, 그대로였다). 그래서
// 고정이 필요 없고 한 번 쓰면 끝이다.
//
// ---- 종이 같은 항목이 여럿일 수 있다
//
// 드래곤이 그렇다 — 종행 7008 이 **둘**이고 하나만 올라가 있다. 둘 다 올리면
// 조회가 엉뚱한 쪽(껍데기)을 집어 소환이 스탯 0 으로 끝난다(`wheelfill.h` 의
// 2026-09-16 기록). 그래서 추천은 **종마다 하나만** 올린다.
//
// > ⚠️ 다만 "어느 쪽이 나은가" 판정은 믿을 것이 못 된다. 2026-09-16 기록은
// > 생명 2500 대 1 이었는데 2026-09-18 실측은 **871 대 -1** 이다. `-1` 은
// > `wheelfill.h` 가 "게임이 정한다 센티널, 껍데기보다 나은 쪽" 이라고 적어 둔
// > 값이라, 지금 올라가 있는 쪽이 오히려 덜 좋을 수도 있다. **그래서 화면이
// > 값을 그대로 보여 주고 손으로 고를 수 있게 한다** - 추천은 거들 뿐이다.

inline constexpr std::size_t kSlotOwnerOff = 0x110;   // 홀더 -> 주인
inline constexpr std::size_t kSlotMapOff = 0x18;      // 주인 -> 해시 표
inline constexpr std::size_t kSlotEntrySpecies = 0x20;
inline constexpr std::size_t kSlotEntryHp = 0xA0;
inline constexpr std::size_t kSlotEntrySlot = 0x148;
inline constexpr std::size_t kSlotEntryGrow = 0x158;
inline constexpr std::uint16_t kSlotNone = 0xFFFF;    // 미등록
inline constexpr int kSlotMaxEntries = 256;
inline constexpr int kSlotMaxCats = 16;

struct SlotEntry {
    std::uint16_t species = 0;
    std::uint16_t slot = kSlotNone;
    std::int32_t hp = 0;
    std::int32_t grow = 0;
    std::string name;      // 종 내부 이름(로스터에서, 없으면 빈 문자열)
    int nth = 0;           // 같은 카테고리 안에서 몇 번째 항목인가
};

struct SlotCategory {
    std::uint32_t cat = 0;
    std::vector<SlotEntry> entries;
};

struct SlotTable {
    bool ready = false;
    std::uintptr_t owner = 0;
    std::vector<SlotCategory> cats;
    char note[96] = {};
};

// 지금 표를 읽는다. 월드 밖이면 `ready == false` 이고 `note` 에 이유가 담긴다.
//
// **플레이어를 직접 안 찾는다** - `player_char()` 를 인자로 받는다. 이 모듈이
// "플레이어를 어떻게 고르는가" 를 몰라도 되고, 시험에서 가짜 주소를 넣을 수 있다.
SlotTable mount_slots(const mem::Reader& reader, std::uintptr_t player_char);

// 항목 하나의 휠 칸을 쓴다. `slot == kSlotNone` 이면 해제다.
//
// **주소를 받지 않는다** - 카테고리와 항목 번호로 지정하고, 쓰기 직전에 표를
// 다시 걸어 주소를 얻는다(clan-roster-volatile-writes: 예전 주소에 써서 게임을
// 팅긴 적이 있다).
bool mount_slot_set(const mem::Reader& reader, std::uintptr_t player_char,
                    std::uint32_t cat, int nth, std::uint16_t slot);

// 한 카테고리를 추천대로 정리한다: 종마다 **제일 나은 하나**만 올리고 같은 종의
// 나머지는 내린다. 올릴 칸은 그 종이 지금 쓰고 있는 칸을 그대로 쓰고, 아무도
// 안 올라가 있으면 아무것도 안 한다(어느 칸에 올려야 하는지 모르기 때문이다).
// 바꾼 항목 수를 돌려준다.
int mount_slot_autofix(const mem::Reader& reader, std::uintptr_t player_char,
                       std::uint32_t cat);

// --------------------------------------------------------- 순수 부분(시험용)

bool slot_registered(std::uint16_t slot);

// 카테고리 이름. 모르면 빈 문자열이 아니라 "카테고리 N".
std::string slot_category_name(std::uint32_t cat);

}  // namespace cdtb::game
