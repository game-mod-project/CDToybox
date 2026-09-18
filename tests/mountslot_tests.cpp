// 휠 슬롯 등록의 **순수 부분**. 게임 없이 태운다.
//
// 실측 배치(2026-09-18, exe 1.0.0.2944, 훅 없이 주인을 직접 잡아 걸었다):
//   카테고리 1 말 8 · 2 드래곤 2 · 3 A.T.A.G. 1 · 5 특수 15 · 7 마차 1 · 9 동물 9
//   드래곤(종행 7008)이 **둘**이고 하나만 올라가 있다: 칸 65535(생명 -1) · 칸 0(생명 871)
#include <cstdint>

#include "game/mountslot.h"
#include "harness.h"

namespace {

using cdtb::game::kSlotEntryGrow;
using cdtb::game::kSlotEntryHp;
using cdtb::game::kSlotEntrySlot;
using cdtb::game::kSlotEntrySpecies;
using cdtb::game::kSlotMapOff;
using cdtb::game::kSlotNone;
using cdtb::game::kSlotOwnerOff;
using cdtb::game::slot_category_name;
using cdtb::game::slot_registered;

TEST(slot_offsets_match_the_measured_entry) {
    // 한 칸만 어긋나면 남의 필드를 등록 칸으로 쓴다.
    CHECK_EQ(static_cast<long long>(kSlotOwnerOff), 0x110LL);
    CHECK_EQ(static_cast<long long>(kSlotMapOff), 0x18LL);
    CHECK_EQ(static_cast<long long>(kSlotEntrySpecies), 0x20LL);
    CHECK_EQ(static_cast<long long>(kSlotEntryHp), 0xA0LL);
    CHECK_EQ(static_cast<long long>(kSlotEntrySlot), 0x148LL);
    CHECK_EQ(static_cast<long long>(kSlotEntryGrow), 0x158LL);
}

TEST(slot_registered_treats_ffff_as_not_on_the_wheel) {
    CHECK(!slot_registered(kSlotNone));
    CHECK(!slot_registered(0xFFFF));
    // 실측에서 본 칸 값들 - 0 도 **진짜 칸**이지 "없음" 이 아니다.
    CHECK(slot_registered(0));
    CHECK(slot_registered(3));
    CHECK(slot_registered(5));
    CHECK(slot_registered(7));
}

TEST(slot_category_name_covers_the_measured_categories) {
    CHECK(slot_category_name(1) == "말");
    CHECK(slot_category_name(2) == "드래곤");
    CHECK(slot_category_name(3) == "A.T.A.G.");
    CHECK(slot_category_name(5) == "특수 탑승물");
    CHECK(slot_category_name(7) == "마차");
    CHECK(slot_category_name(9) == "동물");
}

TEST(slot_category_name_never_returns_empty_for_unknown) {
    // 모르는 번호도 화면에 뭔가는 떠야 한다 - 빈 줄이 뜨면 사용자가 못 고른다.
    CHECK(!slot_category_name(42).empty());
    CHECK(!slot_category_name(0).empty());
    CHECK(slot_category_name(42) != slot_category_name(43));
}

}  // namespace
