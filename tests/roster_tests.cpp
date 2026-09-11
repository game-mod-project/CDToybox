#include <cstring>
#include <string>
#include <vector>

#include "fake_memory.h"
#include "game/clan.h"
#include "game/roster.h"
#include "harness.h"

namespace {

using cdtb::game::RosterEntry;
using cdtb::game::RosterKind;
using cdtb::tests::FakeMemory;

// 캐릭터 표(CharacterInfoManager)와 용병 표(MercenaryInfoManager)를
// 흉내낸 가짜 힙. 실측 배치는 roster.h 머리말.
//
// 힙 오프셋:
//   0x0000 캐릭터 매니저   0x0100 색인   0x0200 레코드 포인터
//   0x1000.. 캐릭터 레코드 3개 (0x400 간격)
//   0x3000 용병 매니저     0x3100 색인   0x3200 레코드 포인터
//   0x4000.. 용병 레코드 2개 (0x100 간격)
//   0x5000.. 엔진 문자열 객체 + 문자 데이터
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kCharMgr = 0x0000;
    static constexpr std::size_t kCharIndex = 0x0100;
    static constexpr std::size_t kCharPtrs = 0x0200;
    static constexpr std::size_t kCharRec = 0x1000;
    static constexpr std::size_t kCharStride = 0x400;
    static constexpr std::size_t kMercMgr = 0x3000;
    static constexpr std::size_t kMercIndex = 0x3100;
    static constexpr std::size_t kMercPtrs = 0x3200;
    static constexpr std::size_t kMercRec = 0x4000;
    static constexpr std::size_t kMercStride = 0x100;
    std::size_t str_cursor = 0x5000;

    // 엔진 문자열 객체를 만들고 그 힙 주소를 준다.
    std::uintptr_t make_string(const char* s) {
        const std::size_t obj = str_cursor;
        const std::size_t data = obj + 0x10;
        mem.put_u64(obj + 0x00, mem.heap_addr(data));
        mem.put_u32(obj + 0x08, static_cast<std::uint32_t>(std::strlen(s)));
        mem.put_str(data, s);
        str_cursor = data + std::strlen(s) + 0x10;
        str_cursor = (str_cursor + 0xF) & ~static_cast<std::size_t>(0xF);
        return mem.heap_addr(obj);
    }

    void put_char(int i, std::uint16_t key, const char* name,
                  std::uint16_t merc_row, bool catchable, bool unique,
                  bool hirable) {
        const std::size_t rec =
            kCharRec + static_cast<std::size_t>(i) * kCharStride;
        mem.put_u64(kCharPtrs + static_cast<std::size_t>(i) * 8,
                    mem.heap_addr(rec));
        // 첫 u32: 하위 16비트가 키, 상위 16비트는 다른 필드(실측)
        mem.put_u32(rec + 0x00, static_cast<std::uint32_t>(key) | 0xABCD0000u);
        mem.put_u64(rec + 0x08, make_string(name));
        mem.put_u32(rec + 0xBE, merc_row);  // u16 자리. 상위는 0
        mem.put_u8(rec + 0x148, catchable ? 1 : 0);
        mem.put_u8(rec + 0x14B, unique ? 1 : 0);
        mem.put_u8(rec + 0x156, hirable ? 1 : 0);
    }

    void put_merc(int i, const char* name, std::uint8_t type) {
        const std::size_t rec =
            kMercRec + static_cast<std::size_t>(i) * kMercStride;
        mem.put_u64(kMercPtrs + static_cast<std::size_t>(i) * 8,
                    mem.heap_addr(rec));
        // +0x00 은 키가 아니라 포인터(실측) - 일부러 큰 값을 넣는다.
        mem.put_u64(rec + 0x00, mem.heap_addr(0x7000));
        mem.put_u64(rec + 0x08, make_string(name));
        mem.put_u8(rec + 0x20, type);
    }

    Fixture() {
        mem.heap.assign(0x8000, 0);
        // 캐릭터 매니저
        mem.put_u64(kCharMgr + 0x28, mem.heap_addr(kCharIndex));
        mem.put_u32(kCharMgr + 0x30, 3);
        mem.put_u64(kCharMgr + 0x58, mem.heap_addr(kCharPtrs));
        mem.put_u32(kCharIndex + 0, 32340);  // 색인 첫 키 == 첫 레코드 키
        put_char(0, 32340, "Animal_Ayut_Wild_32340", 1, true, false, true);
        put_char(1, 20873, "Riding_Wolf_1000", 5, true, true, true);
        put_char(2, 3002, "NHM_Citizen_Dyer_3002", 0xFFFF, true, false, false);
        // 용병 매니저
        mem.put_u64(kMercMgr + 0x28, mem.heap_addr(kMercIndex));
        mem.put_u32(kMercMgr + 0x30, 2);
        mem.put_u64(kMercMgr + 0x58, mem.heap_addr(kMercPtrs));
        mem.put_u32(kMercIndex + 0, 0x4560D040u);  // 실측 색인 키 꼴
        put_merc(0, "Mercenary_Main", 1);
        put_merc(1, "Vehicle_Horse", 2);
    }
};

}  // namespace

TEST(roster_character_catalog_reads_companion_fields) {
    Fixture f;
    std::vector<RosterEntry> out;
    CHECK(cdtb::game::build_catalog_from_manager(
        f.mem, f.mem.heap_addr(Fixture::kCharMgr), RosterKind::Character,
        &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() != 3) return;
    CHECK_EQ(out[0].key, 32340u);
    CHECK_EQ(out[0].name, std::string("Animal_Ayut_Wild_32340"));
    CHECK_EQ(out[0].merc_row, static_cast<std::uint16_t>(1));
    CHECK(out[0].is_companion());
    CHECK(out[0].catchable);
    CHECK(out[0].hirable);
    CHECK(!out[0].unique);
    CHECK_EQ(out[1].merc_row, static_cast<std::uint16_t>(5));
    CHECK(out[1].unique);
    CHECK(!out[2].is_companion());
    CHECK(!out[2].hirable);
}

TEST(roster_vehicle_kind_ignores_character_fields) {
    Fixture f;
    std::vector<RosterEntry> out;
    CHECK(cdtb::game::build_catalog_from_manager(
        f.mem, f.mem.heap_addr(Fixture::kCharMgr), RosterKind::Vehicle, &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.empty()) return;
    CHECK_EQ(out[0].merc_row, static_cast<std::uint16_t>(0xFFFF));
    CHECK(!out[0].hirable);
}

TEST(roster_mercenary_catalog_uses_row_index_as_key) {
    Fixture f;
    std::vector<RosterEntry> out;
    CHECK(cdtb::game::build_catalog_from_manager(
        f.mem, f.mem.heap_addr(Fixture::kMercMgr), RosterKind::Mercenary,
        &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(2));
    if (out.size() != 2) return;
    CHECK_EQ(out[0].key, 0u);
    CHECK_EQ(out[0].name, std::string("Mercenary_Main"));
    CHECK_EQ(out[0].merc_type, static_cast<std::uint8_t>(1));
    CHECK_EQ(out[1].key, 1u);
    CHECK_EQ(out[1].name, std::string("Vehicle_Horse"));
    CHECK_EQ(out[1].merc_type, static_cast<std::uint8_t>(2));
}

TEST(roster_manager_validation_strict_vs_lenient) {
    Fixture f;
    // 캐릭터 매니저: 색인 첫 키 == 첫 레코드 키 -> 엄격 판정 통과
    CHECK(cdtb::game::looks_like_static_manager(
        f.mem, f.mem.heap_addr(Fixture::kCharMgr)));
    // 용병 매니저: +0x00 이 포인터라 엄격 판정은 실패, 느슨한 판정은 통과
    CHECK(!cdtb::game::looks_like_static_manager(
        f.mem, f.mem.heap_addr(Fixture::kMercMgr)));
    CHECK(cdtb::game::looks_like_mercenary_manager(
        f.mem, f.mem.heap_addr(Fixture::kMercMgr)));
    // 개수가 너무 크면 느슨한 판정도 거부
    f.mem.put_u32(Fixture::kMercMgr + 0x30, 100000);
    CHECK(!cdtb::game::looks_like_mercenary_manager(
        f.mem, f.mem.heap_addr(Fixture::kMercMgr)));
}

TEST(roster_companion_type_predicate) {
    using namespace cdtb::game;
    CHECK(!is_companion_merc_type(kMercTypeMain));
    CHECK(is_companion_merc_type(kMercTypeVehicle));
    CHECK(is_companion_merc_type(kMercTypeWagon));
    CHECK(is_companion_merc_type(kMercTypePet));
    CHECK(is_companion_merc_type(kMercTypeDomestic));
    CHECK(!is_companion_merc_type(6));
}

TEST(roster_name_rules_wild_and_species) {
    using cdtb::game::roster_is_wild;
    using cdtb::game::roster_species;
    CHECK(roster_is_wild("Animal_Lumif_Wild_32501"));
    CHECK(roster_is_wild("Animal_Black_Horse_Wild_31378"));
    CHECK(!roster_is_wild("Animal_Lumif_Domestic_32506"));
    CHECK(!roster_is_wild("Riding_Wolf_1000"));
    CHECK_EQ(roster_species("Animal_Lumif_Wild_32501"),
             std::string("Animal_Lumif"));
    CHECK_EQ(roster_species("Animal_Ayut_Domestic_Saddle_32348"),
             std::string("Animal_Ayut"));
    CHECK_EQ(roster_species("Animal_Lumif_Domestic_WagonConnecter_32609"),
             std::string("Animal_Lumif"));
    CHECK_EQ(roster_species("Riding_Wolf_1000"), std::string("Riding_Wolf"));
    CHECK_EQ(roster_species("Animal_Tiger_Wild_2"), std::string("Animal_Tiger"));
    CHECK_EQ(roster_species("MainVehicle"), std::string("MainVehicle"));
}

// 표시명이 있으면 그것을, 없으면 내부 이름을 보여 준다. 현지화 표에
// 없는 행이 실제로 있다 - 실측 2026-09-06: 키 20955
// (Animal_Wolf_Wild_30020) 는 어느 필드에도 없었다.
TEST(roster_entry_falls_back_to_the_internal_name) {
    cdtb::game::RosterEntry e;
    e.name = "Animal_Wolf_Wild_30020";
    CHECK(e.display() == "Animal_Wolf_Wild_30020");
    e.label = "늑대";
    CHECK(e.display() == "늑대");
}

TEST(apply_roster_labels_handles_null) {
    cdtb::tests::FakeMemory mem;
    cdtb::game::LocSystem sys;
    CHECK_EQ(cdtb::game::apply_roster_labels(mem, sys, nullptr),
             static_cast<std::size_t>(0));
}

// 키가 0 인 행은 조회하지 않는다. 현지화 키는 엔티티+필드로 만드는데
// 엔티티 0 은 표의 첫 칸과 부딪힌다.
TEST(apply_roster_labels_skips_key_zero) {
    cdtb::tests::FakeMemory mem;
    cdtb::game::LocSystem sys;
    std::vector<cdtb::game::RosterEntry> v(1);
    v[0].key = 0;
    v[0].name = "Nameless";
    CHECK_EQ(cdtb::game::apply_roster_labels(mem, sys, &v),
             static_cast<std::size_t>(0));
    CHECK(v[0].label.empty());
}

// 현지화 엔티티가 레코드 키가 아니라 내부 이름 끝의 숫자인 행이 있다.
// 실측 2026-09-06: Animal_Parrot_Wild_32884 는 엔티티 32884 에
// '스픽스마코 앵무새'가 있는데 레코드 키가 달라 빗나갔다.
TEST(roster_name_suffix_reads_the_trailing_number) {
    CHECK_EQ(cdtb::game::roster_name_suffix("Animal_Parrot_Wild_32884"),
             32884u);
    CHECK_EQ(cdtb::game::roster_name_suffix("Animal_Bear_Wild_30048"), 30048u);
    CHECK_EQ(cdtb::game::roster_name_suffix("Animal_AmericanBullDog_Domestic_05"),
             5u);
}

TEST(roster_name_suffix_says_none_without_an_underscore_number) {
    CHECK_EQ(cdtb::game::roster_name_suffix("Animal_Black_Cat_Domestic"), 0u);
    CHECK_EQ(cdtb::game::roster_name_suffix("Damian"), 0u);
    CHECK_EQ(cdtb::game::roster_name_suffix(""), 0u);
    // "_" 없이 붙은 숫자는 접미사로 보지 않는다.
    CHECK_EQ(cdtb::game::roster_name_suffix("Wolf30019"), 0u);
    // 전부 숫자면 이름이 아니다.
    CHECK_EQ(cdtb::game::roster_name_suffix("30019"), 0u);
}

TEST(roster_name_suffix_rejects_an_overlong_run) {
    // u32 를 넘길 만큼 긴 숫자는 버린다. 넘치면 엉뚱한 엔티티가 된다.
    CHECK_EQ(cdtb::game::roster_name_suffix("Animal_X_1234567890"), 0u);
}

TEST(apply_species_without_rtti_reports_not_ready) {
    // RTTI 가 없는 프로세스에서는 자리를 찾기 전에 돌아온다 - 아무것도 안 쓴다
    // 이 실행 파일은 camera.cpp 를 링크하지 않아 discover_clan 이 불리지 않는다 -
    // g_rtti 는 늘 nullptr 이다.
    cdtb::tests::FakeMemory mem;
    std::string msg;
    const auto r = cdtb::game::apply_species(mem, 1, 2, &msg);
    CHECK(r == cdtb::game::SpeciesApply::NoRtti);
    CHECK(msg == "RTTI 준비 전입니다");
}
