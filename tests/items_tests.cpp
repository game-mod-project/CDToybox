#include <cstring>
#include <vector>

#include "fake_memory.h"
#include "game/items.h"
#include "harness.h"

namespace {

using cdtb::game::ItemEntry;
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
    }

    std::uintptr_t manager() const { return mem.heap_addr(kMgr); }
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
