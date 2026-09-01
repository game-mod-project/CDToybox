#include <cstring>
#include <vector>

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
//   0x0300  레코드 3개 (0x100 간격 - 진짜는 0x500 이지만 간격은
//           포인터 배열이 정하므로 시험에는 상관없다)
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kMgr = 0x0000;
    static constexpr std::size_t kIndex = 0x0100;
    static constexpr std::size_t kPtrs = 0x0200;
    static constexpr std::size_t kRecords = 0x0300;
    static constexpr std::size_t kRecStride = 0x100;

    static constexpr std::uint32_t kKeyA = 2200;
    static constexpr std::uint32_t kKeyB = 50001;
    static constexpr std::uint32_t kKeyC = 1002557;

    Fixture() {
        mem.heap.assign(0x1000, 0);
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
