#include <cstdint>
#include <unordered_map>
#include <vector>

#include "fake_memory.h"
#include "game/equip.h"
#include "game/equip_bag.h"
#include "harness.h"

namespace {

using cdtb::game::WornPiece;
using cdtb::tests::FakeMemory;

// 서버 인벤토리 컴포넌트 하나, 가방 컨테이너 하나, 레코드 둘.
// 레코드 0 = 2026-09-22 실측 방패(인스턴스 0xF4348, 순번 6387, 담금질 10, 연마 50,
// 열린 칸 2). 소켓 벡터 30바이트는 서버 가방 레코드에서 읽은 그대로다.
//
//   0x0000  컴포넌트  +0x18 -> 0x0100, +0x20 개수 1, +0x24 용량 1
//   0x0100  컨테이너 포인터 [0x0200]
//   0x0200  컨테이너  +0x00 -> 0x0400, +0x08 칸 2, +0x10 종류 1 | 사용 2<<16, +0x14 용량 700
//   0x0400  레코드 0 (0xC8 간격) / 0x04C8 레코드 1
//   0x0800  레코드 0 의 소켓 벡터
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kComp = 0x0000;
    static constexpr std::size_t kRec0 = 0x0400;
    static constexpr std::size_t kRec1 = 0x04C8;
    static constexpr std::size_t kSock = 0x0800;

    Fixture() {
        mem.heap.assign(0x1000, 0);
        mem.put_u64(kComp + 0x18, mem.heap_addr(0x0100));
        mem.put_u32(kComp + 0x20, 1);
        mem.put_u32(kComp + 0x24, 1);
        mem.put_u64(0x0100, mem.heap_addr(0x0200));
        mem.put_u64(0x0200 + 0x00, mem.heap_addr(kRec0));
        mem.put_u32(0x0200 + 0x08, 2);
        mem.put_u32(0x0200 + 0x0C, 2);
        mem.put_u32(0x0200 + 0x10, 1u | (2u << 16));
        mem.put_u32(0x0200 + 0x14, 700);

        mem.put_u64(kRec0 + 0x00, 0xF4348ull);
        mem.put_u16(kRec0 + 0x08, 6387);
        mem.put_u16(kRec0 + 0x0A, 10);
        mem.put_u64(kRec0 + 0x10, 1);
        mem.put_u16(kRec0 + 0x58, 50);
        mem.put_u64(kRec0 + 0x60, mem.heap_addr(kSock));
        mem.put_u32(kRec0 + 0x68, 5);
        mem.put_u32(kRec0 + 0x6C, 5);
        mem.put_u8(kRec0 + 0x70, 2);
        const std::uint8_t sock[30] = {
            0x0D, 0x0D, 0xFF, 0xFF, 0x00, 0x04,   // 칸 0: 보석 3341
            0x0E, 0x0D, 0xFF, 0xFF, 0x01, 0x04,   // 칸 1: 보석 3342
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x6F,   // 칸 2: 잠김
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00,   // 칸 3: 잠김
            0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x00,   // 칸 4: 잠김
        };
        for (std::size_t i = 0; i < sizeof(sock); ++i) mem.put_u8(kSock + i, sock[i]);

        mem.put_u64(kRec1 + 0x00, 0xF44A1ull);
        mem.put_u16(kRec1 + 0x08, 1270);
        mem.put_u64(kRec1 + 0x10, 12);
    }
    std::uintptr_t component() const { return mem.heap_addr(kComp); }
};

// 입은 장비 모양: 소켓 다섯 칸이 전부 열려 있다(장비 표 사본에 우리가 5칸을 썼던 모양).
WornPiece open_piece(std::uint64_t instance) {
    WornPiece w;
    w.instance = instance;
    w.temper = 10;
    w.sharpness = 50;
    w.unlocked = 5;
    for (int k = 0; k < 5; ++k) {
        w.sockets[k].gem = 0xFFFF;
        w.sockets[k].marker = 0;
        w.sockets[k].index = static_cast<std::uint8_t>(k);
    }
    return w;
}

}  // namespace

TEST(bag_index_maps_each_instance_to_its_record) {
    Fixture f;
    const auto idx = cdtb::game::bag_index(f.mem, f.component());
    CHECK_EQ(idx.size(), std::size_t{2});
    const auto it = idx.find(0xF4348ull);
    CHECK(it != idx.end());
    if (it != idx.end()) CHECK_EQ(it->second, f.mem.heap_addr(Fixture::kRec0));
}

TEST(bag_index_is_empty_without_a_component) {
    Fixture f;
    CHECK(cdtb::game::bag_index(f.mem, 0).empty());
}

TEST(read_level_and_sockets_reads_the_bag_record) {
    Fixture f;
    WornPiece w;
    cdtb::game::read_level_and_sockets(f.mem, f.mem.heap_addr(Fixture::kRec0), &w);
    CHECK_EQ(w.temper, std::uint16_t{10});
    CHECK_EQ(w.sharpness, std::uint16_t{50});
    CHECK_EQ(w.unlocked, 2);
    CHECK_EQ(w.sockets[0].gem, std::uint16_t{3341});
    CHECK(w.sockets[0].filled());
    CHECK_EQ(w.sockets[1].gem, std::uint16_t{3342});
    CHECK(w.sockets[2].locked());
    CHECK(w.sockets[4].locked());
}

TEST(apply_bag_truth_takes_the_bag_record_for_a_bag_piece) {
    // 2026-09-22 실측 모양: 장비 표 사본은 5칸(우리가 쓴 것), 가방 레코드는 2칸.
    Fixture f;
    std::vector<WornPiece> ps{open_piece(0xF4348)};
    cdtb::game::apply_bag_truth(f.mem, cdtb::game::bag_index(f.mem, f.component()), &ps);
    CHECK(ps[0].in_bag);
    CHECK_EQ(ps[0].unlocked, 2);
    CHECK(ps[0].sockets[0].filled());
    CHECK(ps[0].sockets[2].locked());
}

TEST(apply_bag_truth_leaves_a_worn_piece_alone) {
    // 실제로 입은 장비는 인벤토리에 없다(실측: 클리프 21개 중 20개).
    Fixture f;
    std::vector<WornPiece> ps{open_piece(0xF4346)};
    cdtb::game::apply_bag_truth(f.mem, cdtb::game::bag_index(f.mem, f.component()), &ps);
    CHECK(!ps[0].in_bag);
    CHECK_EQ(ps[0].unlocked, 5);
    CHECK_EQ(ps[0].sockets[2].index, std::uint8_t{2});
}
