#include <cstring>
#include <vector>

#include "core/config.h"
#include "fake_memory.h"
#include "game/items.h"
#include "game/localization.h"
#include "harness.h"

namespace {

using cdtb::game::ItemEntry;
using cdtb::game::LocSystem;
using cdtb::tests::FakeMemory;

// ItemInfoManager 를 흉내낸 가짜 힙.
//
// 실측 구조 (docs/superpowers/specs/2026-09-01-item-table.md):
//   매니저 +0x28  색인 표 포인터 ((u32 키, u32 ?) 쌍)
//          +0x30  u32 개수
//          +0x58  레코드 포인터 배열
//   레코드 +0x00  u32 키
//          +0x28  u64 이름 현지화 키
//
// 배치 (힙 오프셋):
//   0x0000  매니저
//   0x0100  색인 표
//   0x0200  레코드 포인터 배열
//   0x1000  레코드 3개 (0x600 간격. 등급이 +0x210 에 있어 겹치면 안 된다)
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kMgr = 0x0000;
    static constexpr std::size_t kIndex = 0x0100;
    static constexpr std::size_t kPtrs = 0x0200;
    static constexpr std::size_t kRecords = 0x1000;
    static constexpr std::size_t kRecStride = 0x600;

    static constexpr std::uint32_t kKeyA = 2200;
    static constexpr std::uint32_t kKeyB = 50001;
    static constexpr std::uint32_t kKeyC = 1002557;

    Fixture() {
        mem.heap.assign(0x4000, 0);
        mem.put_u64(kMgr + 0x28, mem.heap_addr(kIndex));
        mem.put_u32(kMgr + 0x30, 3);
        mem.put_u64(kMgr + 0x58, mem.heap_addr(kPtrs));

        const std::uint32_t keys[3] = {kKeyA, kKeyB, kKeyC};
        for (int i = 0; i < 3; ++i) {
            // 색인 표: (키, 용도 미상)
            mem.put_u32(kIndex + i * 8 + 0, keys[i]);
            mem.put_u32(kIndex + i * 8 + 4, static_cast<std::uint32_t>(i * 690));

            const std::size_t rec = kRecords + i * kRecStride;
            mem.put_u64(kPtrs + i * 8, mem.heap_addr(rec));
            mem.put_u32(rec + 0x00, keys[i]);
            // 이름 현지화 키는 레코드가 직접 들고 있다.
            mem.put_u64(rec + 0x28,
                        (static_cast<std::uint64_t>(keys[i]) << 32) | 0x70ull);
            // 등급 +0x210 (0=없음, 1..5), 분류 +0xA3
            mem.put_u8(rec + 0x210, static_cast<std::uint8_t>(i + 1));
            mem.put_u8(rec + 0xA3, static_cast<std::uint8_t>(56 + i));
            // 최대 스택 +0x18. 지급 개수를 여기에 맞춰 자른다.
            mem.put_u32(rec + 0x18, static_cast<std::uint32_t>(10 * (i + 1)));
        }
        build_localization();
    }

    std::uintptr_t manager() const { return mem.heap_addr(kMgr); }

    // --- 현지화 시스템도 같은 힙에 세운다 ---
    //   0x0600 시스템 / 0x0700 카테고리 표 / 0x0A80 포인터 배열
    //   0x0B00 항목 3개 / 0x0C00 문자열 풀
    static constexpr std::size_t kLocSys = 0x0600;
    static constexpr std::size_t kLocCats = 0x0700;
    static constexpr std::size_t kLocPtrs = 0x0A80;
    static constexpr std::size_t kLocEntries = 0x0B00;
    static constexpr std::size_t kLocPool = 0x0C00;
    static constexpr std::uint32_t kLocPoolSize = 0x40;
    static constexpr int kLocCategory = 3;

    void build_localization() {
        mem.put_u64(kLocSys + 0x48, mem.heap_addr(kLocCats));
        mem.put_u64(kLocSys + 0x58, mem.heap_addr(kLocPool));
        mem.put_u32(kLocSys + 0x60, kLocPoolSize);

        mem.put_u64(kLocCats + kLocCategory * 16 + 0, mem.heap_addr(kLocPtrs));
        mem.put_u32(kLocCats + kLocCategory * 16 + 8, 3);

        // 이름 키는 레코드가 든 것과 같아야 한다. 오름차순이어야
        // 이분 탐색이 성립하는데 키 자체가 오름차순이므로 그대로다.
        const std::uint32_t keys[3] = {kKeyA, kKeyB, kKeyC};
        const std::uint32_t offs[3] = {0x00, 0x10, 0x20};
        for (int i = 0; i < 3; ++i) {
            const std::size_t e = kLocEntries + i * 0x20;
            mem.put_u64(kLocPtrs + i * 8, mem.heap_addr(e));
            mem.put_u64(e + 0x10,
                        (static_cast<std::uint64_t>(keys[i]) << 32) | 0x70ull);
            mem.put_u32(e + 0x18, offs[i]);
        }
        mem.put_str(kLocPool + 0x00, "편전");
        mem.put_str(kLocPool + 0x10, "화살");
        mem.put_str(kLocPool + 0x20, "지속 보급 화살");
    }

    LocSystem loc_system() const {
        LocSystem s;
        s.object = mem.heap_addr(kLocSys);
        s.pool = mem.heap_addr(kLocPool);
        s.pool_size = kLocPoolSize;
        return s;
    }
};

}  // namespace

// ---------------------------------------------------------------- 순회

TEST(read_item_table_walks_record_pointer_array) {
    Fixture f;
    std::vector<ItemEntry> out;
    CHECK(cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK_EQ(out[0].key, Fixture::kKeyA);
        CHECK_EQ(out[1].key, Fixture::kKeyB);
        CHECK_EQ(out[2].key, Fixture::kKeyC);
        CHECK_EQ(out[0].name_key, 0x0000089800000070ull);
        CHECK_EQ(out[1].name_key, 0x0000C35100000070ull);
        CHECK_EQ(out[0].record, f.mem.heap_addr(Fixture::kRecords));
    }
}

TEST(read_item_table_honors_max) {
    Fixture f;
    std::vector<ItemEntry> out;
    CHECK(cdtb::game::read_item_table(f.mem, f.manager(), &out, 2));
    CHECK_EQ(out.size(), static_cast<std::size_t>(2));
    if (out.size() == 2) CHECK_EQ(out[1].key, Fixture::kKeyB);
}

TEST(read_item_table_skips_null_record_slot) {
    // 중간 슬롯이 비어도 나머지는 읽는다.
    Fixture f;
    f.mem.put_u64(Fixture::kPtrs + 8, 0);
    std::vector<ItemEntry> out;
    CHECK(cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
    CHECK_EQ(out.size(), static_cast<std::size_t>(2));
    if (out.size() == 2) {
        CHECK_EQ(out[0].key, Fixture::kKeyA);
        CHECK_EQ(out[1].key, Fixture::kKeyC);
    }
}

TEST(read_item_table_rejects_absurd_count) {
    // 잘못된 후보를 매니저로 착각하면 개수가 쓰레기값이 된다.
    // 그대로 믿고 할당하면 메모리를 통째로 먹는다.
    Fixture f;
    f.mem.put_u32(Fixture::kMgr + 0x30, 0xFFFFFFFFu);
    std::vector<ItemEntry> out;
    CHECK(!cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
}

TEST(read_item_table_rejects_null_record_array) {
    Fixture f;
    f.mem.put_u64(Fixture::kMgr + 0x58, 0);
    std::vector<ItemEntry> out;
    CHECK(!cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
}

TEST(read_item_table_rejects_zero_count) {
    Fixture f;
    f.mem.put_u32(Fixture::kMgr + 0x30, 0);
    std::vector<ItemEntry> out;
    CHECK(!cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
}

// ---------------------------------------------------------------- 목록

// 게임은 아이템 표를 현지화보다 먼저 올린다. 예전에는 그 순간에
// 목록을 만들고 굳어 버려서 이름이 영영 비었다 - 실제로 그렇게 났다
// (로그: "현지화 시스템이 없다" -> "이름 풀린 것 0개").
TEST(rebuild_catalog_when_none_yet) {
    CHECK(cdtb::game::should_rebuild_catalog(false, false, false));
    CHECK(cdtb::game::should_rebuild_catalog(false, false, true));
}

TEST(rebuild_catalog_once_localization_shows_up) {
    CHECK(cdtb::game::should_rebuild_catalog(true, false, true));
}

TEST(no_rebuild_while_localization_is_still_missing) {
    CHECK(!cdtb::game::should_rebuild_catalog(true, false, false));
}

TEST(no_rebuild_once_names_resolved) {
    CHECK(!cdtb::game::should_rebuild_catalog(true, true, true));
}

TEST(build_item_catalog_fills_names_from_localization) {
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK_EQ(out[0].key, Fixture::kKeyA);
        CHECK_EQ(out[0].name, std::string("편전"));
        CHECK_EQ(out[1].name, std::string("화살"));
        CHECK_EQ(out[2].name, std::string("지속 보급 화살"));
    }
}

TEST(build_item_catalog_leaves_name_empty_when_key_is_absent) {
    // 실측에서 6,810개 중 72개가 현지화 표에 없었다. 목록에서
    // 빼지 않는다 - 키는 있는 아이템이므로 지급 대상이 될 수 있다.
    Fixture f;
    f.mem.put_u64(Fixture::kLocEntries + 0x10, 0xDEADBEEFull);
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK(out[0].name.empty());
        CHECK_EQ(out[0].key, Fixture::kKeyA);
        CHECK_EQ(out[1].name, std::string("화살"));
    }
}

TEST(build_item_catalog_still_lists_keys_without_localization) {
    // 현지화 시스템을 못 찾아도 키 목록은 낸다.
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), LocSystem{}, &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK_EQ(out[0].key, Fixture::kKeyA);
        CHECK(out[0].name.empty());
    }
}

TEST(build_item_catalog_fails_when_manager_is_bad) {
    Fixture f;
    f.mem.put_u64(Fixture::kMgr + 0x58, 0);
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(!cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                          &out));
}

// ------------------------------------------------------------ 후보 검증

TEST(item_manager_is_valid_when_index_key_matches_first_record) {
    // 색인 표와 레코드 배열이 같은 키를 말해야 진짜다. 카메라에서
    // vtable 값을 우연히 담은 메모리를 후보로 집어 11회 어긋났다.
    Fixture f;
    CHECK(cdtb::game::looks_like_item_manager(f.mem, f.manager()));
}

TEST(item_manager_is_rejected_when_index_disagrees_with_record) {
    Fixture f;
    f.mem.put_u32(Fixture::kIndex + 0, 9999);
    CHECK(!cdtb::game::looks_like_item_manager(f.mem, f.manager()));
}

TEST(item_manager_is_rejected_when_index_pointer_is_null) {
    Fixture f;
    f.mem.put_u64(Fixture::kMgr + 0x28, 0);
    CHECK(!cdtb::game::looks_like_item_manager(f.mem, f.manager()));
}

// ------------------------------------------------------------ 등급·분류

TEST(read_item_table_reads_grade_and_category) {
    // 등급은 +0x210 (0=없음, 1..5 = T1..T5), 분류는 +0xA3.
    // 사이트의 T1~T5 가 알려진 아이템 103개와 대조해 확정했다 -
    // 1,280바이트 중 다섯 등급을 완벽히 가르는 칸은 +0x210 하나뿐이다.
    Fixture f;
    std::vector<ItemEntry> out;
    CHECK(cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() != 3) return;
    CHECK_EQ(out[0].grade, static_cast<std::uint8_t>(1));
    CHECK_EQ(out[2].grade, static_cast<std::uint8_t>(3));
    CHECK_EQ(out[0].category, static_cast<std::uint8_t>(56));
    CHECK_EQ(out[2].category, static_cast<std::uint8_t>(58));
}

// 개수를 최대 스택보다 크게 넣으면 게임이 조용히 거절한다. 지급
// 칸에서 미리 잘라 주려면 목록이 그 값을 들고 있어야 한다.
TEST(build_item_catalog_carries_max_stack) {
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK(out.size() >= 2);
    CHECK_EQ(out[0].max_stack, std::uint32_t{10});
    CHECK_EQ(out[1].max_stack, std::uint32_t{20});
}

TEST(build_item_catalog_carries_grade_and_category) {
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() != 3) return;
    CHECK_EQ(out[1].grade, static_cast<std::uint8_t>(2));
    CHECK_EQ(out[1].category, static_cast<std::uint8_t>(57));
}

TEST(grade_label_names_the_five_tiers) {
    CHECK_EQ(std::string(cdtb::game::grade_label(0)), std::string("-"));
    CHECK_EQ(std::string(cdtb::game::grade_label(1)), std::string("T1"));
    CHECK_EQ(std::string(cdtb::game::grade_label(5)), std::string("T5"));
    // 표에 없는 값이 나와도 죽지 않는다.
    CHECK_EQ(std::string(cdtb::game::grade_label(9)), std::string("?"));
}

// -------------------------------------------------------- 담금질 상한

// 지급으로 담금질을 실어 보내려면 상한을 알아야 한다. 게임의 작업
// 함수가 `담금질 <= [레코드+0x250] - 1` 을 검사하고 넘으면 거절한다
// (docs/superpowers/specs/2026-09-02-inventory.md).
//
// 실측: 장비는 +0x250 이 11 이라 0..10 이고 툴팁 게이지가 10칸이다.
// 재료(스콜레사이트광석)는 0 이라 담금질이 없다.

TEST(read_item_table_turns_the_cap_into_the_highest_level) {
    Fixture f;
    f.mem.put_u32(Fixture::kRecords + 0x250, 11);

    std::vector<ItemEntry> items;
    CHECK(cdtb::game::read_item_table(f.mem, f.mem.heap_addr(Fixture::kMgr),
                                      &items, 0));
    CHECK(!items.empty());
    if (items.empty()) return;
    CHECK_EQ(items[0].max_temper, std::uint32_t{10});
}

TEST(read_item_table_reports_no_temper_when_the_cap_is_zero) {
    // 재료 아이템이다. 0 을 그대로 빼면 0xFFFFFFFF 가 된다.
    Fixture f;
    f.mem.put_u32(Fixture::kRecords + 0x250, 0);

    std::vector<ItemEntry> items;
    CHECK(cdtb::game::read_item_table(f.mem, f.mem.heap_addr(Fixture::kMgr),
                                      &items, 0));
    CHECK(!items.empty());
    if (items.empty()) return;
    CHECK_EQ(items[0].max_temper, std::uint32_t{0});
}

TEST(build_item_catalog_carries_the_temper_cap) {
    Fixture f;
    f.mem.put_u32(Fixture::kRecords + 0x250, 11);

    std::vector<cdtb::game::ItemCatalogEntry> cat;
    LocSystem sys;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.mem.heap_addr(Fixture::kMgr),
                                         sys, &cat));
    CHECK(!cat.empty());
    if (cat.empty()) return;
    CHECK_EQ(cat[0].max_temper, std::uint32_t{10});
}

// --- 소켓 -------------------------------------------------------------

TEST(read_item_table_reads_the_equip_type_at_0x42) {
    Fixture f;
    f.mem.put_u16(Fixture::kRecords + 0x42, 7);

    std::vector<ItemEntry> items;
    CHECK(cdtb::game::read_item_table(f.mem, f.mem.heap_addr(Fixture::kMgr),
                                      &items, 0));
    CHECK(!items.empty());
    if (items.empty()) return;
    CHECK_EQ(items[0].equip_type, std::uint16_t{7});
}

TEST(build_item_catalog_carries_the_socket_cap_and_equip_type) {
    Fixture f;
    f.mem.put_u32(Fixture::kRecords + 0x238, 3);
    f.mem.put_u16(Fixture::kRecords + 0x42, 7);
    f.mem.put_u32(Fixture::kRecords + 0x18, 1);   // 안 겹치는 아이템

    std::vector<cdtb::game::ItemCatalogEntry> cat;
    LocSystem sys;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.mem.heap_addr(Fixture::kMgr),
                                         sys, &cat));
    CHECK(!cat.empty());
    if (cat.empty()) return;
    CHECK_EQ(cat[0].max_sockets, std::uint32_t{3});
    CHECK_EQ(cat[0].equip_type, std::uint16_t{7});
}

// 게임의 규칙(0x2A70000 · 0xF090BC0)을 그대로 옮긴 것이다.
// specs/2026-09-07-socket-grant-unlock-research.md 3.1 절.

TEST(socket_room_gives_the_table_cap_for_equipment) {
    CHECK_EQ(cdtb::game::socket_room(3, 1, 7), std::uint32_t{3});
    // 겹치지 않는 아이템은 max_stack 0 으로도 온다.
    CHECK_EQ(cdtb::game::socket_room(5, 0, 7), std::uint32_t{5});
}

TEST(socket_room_refuses_when_the_item_is_not_equipment) {
    // _equipTypeInfo == 0xFFFF. 판별자의 마지막 줄이 이것이다.
    CHECK_EQ(cdtb::game::socket_room(3, 1, 0xFFFF), std::uint32_t{0});
}

TEST(socket_room_refuses_a_stacking_item) {
    CHECK_EQ(cdtb::game::socket_room(3, 99, 7), std::uint32_t{0});
}

TEST(socket_room_refuses_when_the_table_gives_no_sockets) {
    CHECK_EQ(cdtb::game::socket_room(0, 1, 7), std::uint32_t{0});
}

// --- 소켓 상한 올리기 대상 고르기 ---------------------------------------
//
// 아이템표(`ItemInfo+0x238`)가 화면·사용 칸 수를 정한다(실측 2026-09-08).
// 올릴 것만 고른다.

TEST(socket_cap_target_takes_equipment_below_the_wanted_cap) {
    CHECK(cdtb::game::socket_cap_target(3, 7, 5));
    CHECK(cdtb::game::socket_cap_target(1, 7, 2));
}

TEST(socket_cap_target_skips_items_that_are_not_equipment) {
    CHECK(!cdtb::game::socket_cap_target(3, 0xFFFF, 5));
}

TEST(socket_cap_target_takes_gear_designed_without_sockets) {
    // 원래 0칸인 장비도 대상이다. 망토·귀걸이·목걸이·반지도 레코드에
    // 5칸 벡터가 이미 있어서 표 상한만 올리면 소켓이 생긴다
    // (실측 2026-09-08: 마녀의 반지 0 -> 2칸). 어느 부위를 건드릴지는
    // 규칙이 정하지 여기서 막을 일이 아니다.
    CHECK(cdtb::game::socket_cap_target(0, 7, 5));
}

TEST(socket_cap_target_never_lowers_a_cap) {
    CHECK(!cdtb::game::socket_cap_target(5, 7, 5));
    CHECK(!cdtb::game::socket_cap_target(5, 7, 3));
}

TEST(socket_cap_target_does_nothing_when_no_value_is_wanted) {
    // 규칙에 없는 부위는 want 0 으로 온다.
    CHECK(!cdtb::game::socket_cap_target(0, 7, 0));
    CHECK(!cdtb::game::socket_cap_target(3, 7, 0));
}

TEST(socket_cap_apply_does_nothing_without_a_catalog) {
    // 표가 없으면 쓸 곳도 모른다. 게임 메모리를 안 건드리고 빠진다.
    const Fixture f;
    const std::vector<cdtb::game::SocketCapRule> rules{
        {cdtb::game::SocketPart{24, 4}, 5}};
    const auto r = cdtb::game::socket_cap_apply(f.mem, rules);
    CHECK(!r.ok);
    CHECK_EQ(r.changed, 0);
}

TEST(socket_cap_apply_refuses_a_value_past_the_vector) {
    // 소켓 벡터는 다섯 칸이다. 한 부위라도 그 위면 통째로 거절한다 -
    // 절반만 걸린 상태가 제일 나쁘다.
    const Fixture f;
    const std::vector<cdtb::game::SocketCapRule> bad{
        {cdtb::game::SocketPart{24, 4}, 6}};
    CHECK(!cdtb::game::socket_cap_apply(f.mem, bad).ok);
}

TEST(socket_cap_restore_reports_nothing_when_it_was_never_raised) {
    const Fixture f;
    const auto r = cdtb::game::socket_cap_restore(f.mem);
    CHECK(!r.ok);
    CHECK_EQ(r.changed, 0);
    CHECK(!cdtb::game::socket_cap_active());
    CHECK(cdtb::game::socket_cap_rules().empty());
}

TEST(socket_parts_is_empty_without_a_catalog) {
    CHECK(cdtb::game::socket_parts().empty());
}

// --- 설정 파일의 부위 표기 ---------------------------------------------
//
// `분류:장비타입=칸수` 를 쉼표로 잇는다. 한 항목이 어긋나도 나머지는
// 살린다 - 파일 한 줄 때문에 설정을 통째로 잃을 이유가 없다.

TEST(config_reads_socket_cap_parts) {
    const auto p = cdtb::config::parse_socket_cap_parts("3:5=5,24:4=2");
    CHECK_EQ(p.size(), std::size_t{2});
    if (p.size() < 2) return;
    CHECK_EQ(p[0].category, 3);
    CHECK_EQ(p[0].equip_type, 5);
    CHECK_EQ(p[0].want, 5);
    CHECK_EQ(p[1].category, 24);
    CHECK_EQ(p[1].equip_type, 4);
    CHECK_EQ(p[1].want, 2);
}

TEST(config_drops_a_broken_socket_cap_part_but_keeps_the_rest) {
    const auto p = cdtb::config::parse_socket_cap_parts("3:5=5,쓰레기,24:4=2");
    CHECK_EQ(p.size(), std::size_t{2});
}

TEST(config_drops_socket_cap_parts_past_the_vector) {
    // 게임 표에 이상한 값을 쓰느니 버린다.
    CHECK(cdtb::config::parse_socket_cap_parts("3:5=6").empty());
    CHECK(cdtb::config::parse_socket_cap_parts("3:5=-1").empty());
    // 0 은 "안 건드림" 이라 적을 이유가 없다.
    CHECK(cdtb::config::parse_socket_cap_parts("3:5=0").empty());
}

// ------------------------------------------------------ 보석 거르기

TEST(is_socket_gem_needs_category_74_and_a_name) {
    // 장비 창과 지급 창의 보석 고르기가 같은 조건으로 거른다.
    cdtb::game::ItemCatalogEntry e;
    e.category = cdtb::game::kSocketGemCategory;
    e.name = "바람 가르기";
    CHECK(cdtb::game::is_socket_gem(e));
    // 이름이 안 풀린 것은 고를 수 없다 - 목록에 빈 줄이 뜬다.
    e.name.clear();
    CHECK(!cdtb::game::is_socket_gem(e));
    // 분류가 다르면 이름이 있어도 아니다.
    e.name = "한손검";
    e.category = 56;
    CHECK(!cdtb::game::is_socket_gem(e));
}
