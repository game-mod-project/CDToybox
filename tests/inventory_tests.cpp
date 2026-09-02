#include <cstring>
#include <vector>

#include "fake_memory.h"
#include "game/inventory.h"
#include "harness.h"

namespace {

using cdtb::game::InventoryContainer;
using cdtb::game::InventoryRecord;
using cdtb::tests::FakeMemory;

// 서버 인벤토리 컴포넌트를 흉내낸 가짜 힙.
//
// 실측 구조 (docs/superpowers/specs/2026-09-02-inventory.md):
//
//   컴포넌트 +0x18  ptr 컨테이너 포인터 배열
//            +0x20  u32 개수 / +0x24 u32 용량   (실측 18 / 18)
//   컨테이너 +0x00  ptr 레코드 배열
//            +0x08  u32 배열 칸 수 (실측 1460)
//            +0x10  u16 종류 / +0x12 u16 사용 / +0x14 u16 용량
//   레코드   0xC8 간격. +0x00 u64 인스턴스 ID
//            +0x08 {u16 순번, u16 담금질}, +0x10 i64 개수
//
// 배치 (힙 오프셋):
//   0x0000  컴포넌트
//   0x0100  컨테이너 포인터 배열 (2개)
//   0x0200  컨테이너 A / 0x0230 컨테이너 B
//   0x0400  A 의 레코드 4칸 / 0x0800  B 의 레코드 2칸
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kComp = 0x0000;
    static constexpr std::size_t kPtrs = 0x0100;
    static constexpr std::size_t kContA = 0x0200;
    static constexpr std::size_t kContB = 0x0230;
    static constexpr std::size_t kRecsA = 0x0400;
    static constexpr std::size_t kRecsB = 0x0800;
    static constexpr std::size_t kStride = 0xC8;

    Fixture() {
        mem.heap.assign(0x1000, 0);
        mem.put_u64(kComp + 0x18, mem.heap_addr(kPtrs));
        mem.put_u32(kComp + 0x20, 2);
        mem.put_u32(kComp + 0x24, 2);

        mem.put_u64(kPtrs + 0, mem.heap_addr(kContA));
        mem.put_u64(kPtrs + 8, mem.heap_addr(kContB));

        put_container(kContA, mem.heap_addr(kRecsA), 4, 1, 2, 130);
        put_container(kContB, mem.heap_addr(kRecsB), 2, 4, 1, 300);

        // 빈 칸은 인스턴스 ID 가 전부 0xFF 다.
        for (std::uint32_t i = 0; i < 4; ++i) put_empty(kRecsA, i);
        for (std::uint32_t i = 0; i < 2; ++i) put_empty(kRecsB, i);

        put_record(kRecsA, 0, 18510, 5915, 0, 1);   // 그로테반트 판금 투구
        put_record(kRecsA, 2, 1000536, 5921, 3, 1); // 바르그란 방패 담금질 3
        put_record(kRecsB, 0, 2183, 6496, 0, 1);    // 늑대의 한손검
    }

    void put_container(std::size_t at, std::uintptr_t records,
                       std::uint32_t slots, std::uint16_t kind,
                       std::uint16_t used, std::uint16_t capacity) {
        mem.put_u64(at + 0x00, records);
        mem.put_u32(at + 0x08, slots);
        mem.put_u32(at + 0x0C, slots);
        mem.put_u32(at + 0x10,
                    static_cast<std::uint32_t>(kind) |
                        (static_cast<std::uint32_t>(used) << 16));
        mem.put_u32(at + 0x14, capacity);
    }
    void put_empty(std::size_t recs, std::uint32_t i) {
        mem.put_u64(recs + i * kStride + 0x00, 0xFFFFFFFFFFFFFFFFull);
        mem.put_u32(recs + i * kStride + 0x08, 0x0000FFFFu);
    }
    void put_record(std::size_t recs, std::uint32_t i, std::uint64_t id,
                    std::uint16_t index, std::uint16_t temper,
                    std::int64_t count) {
        const std::size_t r = recs + i * kStride;
        mem.put_u64(r + 0x00, id);
        mem.put_u32(r + 0x08,
                    static_cast<std::uint32_t>(index) |
                        (static_cast<std::uint32_t>(temper) << 16));
        mem.put_u64(r + 0x10, static_cast<std::uint64_t>(count));
    }
    std::uintptr_t component() const { return mem.heap_addr(kComp); }
};

}  // namespace

// ------------------------------------------------------------ 컨테이너

TEST(read_inventory_containers_walks_the_pointer_array) {
    // 컴포넌트 앞부분을 훑어 컨테이너 꼴을 찾는 것이 아니라 +0x18 의
    // 포인터 배열을 따라간다. 훑는 방식은 플레이어 가방을 못 잡았다.
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    CHECK_EQ(cs.size(), std::size_t{2});
    if (cs.size() < 2) return;
    CHECK_EQ(cs[0].records, f.mem.heap_addr(Fixture::kRecsA));
    CHECK_EQ(cs[0].slots, 4u);
    CHECK_EQ(cs[0].kind, std::uint16_t{1});
    CHECK_EQ(cs[0].used, std::uint16_t{2});
    CHECK_EQ(cs[0].capacity, std::uint16_t{130});
    CHECK_EQ(cs[1].kind, std::uint16_t{4});
    CHECK_EQ(cs[1].capacity, std::uint16_t{300});
}

TEST(read_inventory_containers_fails_without_the_array) {
    Fixture f;
    f.mem.put_u64(Fixture::kComp + 0x18, 0);
    std::vector<InventoryContainer> cs;
    CHECK(!cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
}

TEST(read_inventory_containers_rejects_a_bogus_count) {
    // 잘못 집으면 개수가 쓰레기값이 된다. 그대로 믿고 할당하면
    // 메모리를 통째로 먹는다. 실측은 18 개다.
    Fixture f;
    f.mem.put_u32(Fixture::kComp + 0x20, 0x7FFFFFFFu);
    std::vector<InventoryContainer> cs;
    CHECK(!cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
}

TEST(read_inventory_containers_skips_a_null_slot) {
    Fixture f;
    f.mem.put_u64(Fixture::kPtrs + 0, 0);
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    CHECK_EQ(cs.size(), std::size_t{1});
    if (cs.empty()) return;
    CHECK_EQ(cs[0].kind, std::uint16_t{4});
}

// -------------------------------------------------------------- 레코드

TEST(read_inventory_records_skips_empty_slots) {
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    if (cs.empty()) return;

    std::vector<InventoryRecord> rs;
    CHECK(cdtb::game::read_inventory_records(f.mem, cs[0], &rs));
    CHECK_EQ(rs.size(), std::size_t{2});
    if (rs.size() < 2) return;
    CHECK_EQ(rs[0].instance_id, 18510ull);
    CHECK_EQ(rs[1].instance_id, 1000536ull);
}

TEST(read_inventory_records_splits_index_and_temper) {
    // +0x08 은 {u16 순번, u16 담금질} 이다. 통째로 u32 로 읽으면
    // 순번이 202529 같은 값이 되어 표에서 못 찾는다.
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    if (cs.empty()) return;

    std::vector<InventoryRecord> rs;
    CHECK(cdtb::game::read_inventory_records(f.mem, cs[0], &rs));
    if (rs.size() < 2) return;
    CHECK_EQ(rs[0].index, 5915u);
    CHECK_EQ(rs[0].temper, 0u);
    CHECK_EQ(rs[1].index, 5921u);
    CHECK_EQ(rs[1].temper, 3u);
}

TEST(read_inventory_records_keeps_the_slot_and_address) {
    // export 는 어느 칸이었는지도 알아야 다시 넣을 수 있다.
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    if (cs.empty()) return;

    std::vector<InventoryRecord> rs;
    CHECK(cdtb::game::read_inventory_records(f.mem, cs[0], &rs));
    if (rs.size() < 2) return;
    CHECK_EQ(rs[1].slot, 2u);
    CHECK_EQ(rs[1].address,
             f.mem.heap_addr(Fixture::kRecsA + 2 * Fixture::kStride));
    CHECK_EQ(rs[1].count, std::int64_t{1});
}

TEST(read_inventory_records_reads_past_the_used_count) {
    // 사용 개수를 믿고 거기서 멈추면 안 된다 - 실측에서 144 라고
    // 하는데 실제 레코드는 143개였다. 빈 칸이 중간에 섞인다.
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    if (cs.empty()) return;
    cs[0].used = 1;   // 실제로는 2개가 들어 있다

    std::vector<InventoryRecord> rs;
    CHECK(cdtb::game::read_inventory_records(f.mem, cs[0], &rs));
    CHECK_EQ(rs.size(), std::size_t{2});
}

TEST(read_inventory_records_rejects_a_bogus_slot_count) {
    Fixture f;
    std::vector<InventoryContainer> cs;
    CHECK(cdtb::game::read_inventory_containers(f.mem, f.component(), &cs));
    if (cs.empty()) return;
    cs[0].slots = 0x7FFFFFFFu;

    std::vector<InventoryRecord> rs;
    CHECK(!cdtb::game::read_inventory_records(f.mem, cs[0], &rs));
}
