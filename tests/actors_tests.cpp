#include <cstdint>
#include <utility>
#include <vector>

#include "fake_memory.h"
#include "game/actors.h"
#include "harness.h"

namespace {

using cdtb::tests::FakeMemory;

// ClientActorManager 와 액터 몇 개를 흉내낸 가짜 힙. 배치는 actors.h 머리말.
//
//   0x0000 매니저 (버킷 +0x128.. {배열, 개수, 용량})
//   0x1000 버킷 A 배열 (개수 2, 용량 8, 배열엔 옛 포인터가 3번째 칸에 남아 있음)
//   0x1100 버킷 B 배열 (개수 1, 용량 4)
//   0x2000.. 액터 3개 (0x200 간격), 각각 +0x68 -> 홀더, 홀더 +0x20 -> 정보, 정보 +0x30 u16 행
//   0x3000.. 홀더/정보 객체
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kMgr = 0x0000;
    static constexpr std::size_t kArrA = 0x1000;
    static constexpr std::size_t kArrB = 0x1100;
    static constexpr std::size_t kActor = 0x2000;
    static constexpr std::size_t kAux = 0x3000;
    static constexpr std::size_t kCont = 0x3800;
    static constexpr std::size_t kBucket = 0x3A00;
    static constexpr std::size_t kNodes = 0x3B00;
    static constexpr std::size_t kNode0 = 0x3B40;
    static constexpr std::size_t kNode1 = 0x3B80;

    std::uintptr_t actor(int i) const { return mem.heap_addr(kActor + i * 0x200); }

    void put_actor(int i, std::uint16_t row) {
        const std::size_t a = kActor + i * 0x200;
        const std::size_t holder = kAux + i * 0x100;
        const std::size_t info = holder + 0x80;
        mem.put_u64(a + 0x68, mem.heap_addr(holder));
        mem.put_u64(holder + 0x20, mem.heap_addr(info));
        mem.put_u32(info + 0x30, row);
    }

    Fixture() {
        mem.heap.assign(0x4000, 0);
        // 버킷 A: +0x128 {arr, count 2, cap 8}
        mem.put_u64(kMgr + 0x128, mem.heap_addr(kArrA));
        mem.put_u32(kMgr + 0x130, 2);
        mem.put_u32(kMgr + 0x134, 8);
        // 버킷 B: +0x138 {arr, count 1, cap 4}
        mem.put_u64(kMgr + 0x138, mem.heap_addr(kArrB));
        mem.put_u32(kMgr + 0x140, 1);
        mem.put_u32(kMgr + 0x144, 4);
        // 나머지 버킷은 0 (없음)
        mem.put_u64(kArrA + 0, actor(0));
        mem.put_u64(kArrA + 8, actor(1));
        mem.put_u64(kArrA + 16, 0);         // 끝 표시(0)
        mem.put_u64(kArrA + 24, actor(2));  // 끝 너머의 옛 포인터 - 걷지 않아야
        mem.put_u64(kArrB + 0, actor(1));   // 다른 버킷에 중복 - 한 번만
        mem.put_u64(kArrB + 8, 0xFFFFFFFFFFFFFFFFull);  // 쓰레기 끝
        // 핸들 컨테이너: 매니저 +0x08 -> 컨테이너, 버킷 1개, 노드 2개
        mem.put_u64(kMgr + 0x08, mem.heap_addr(kCont));
        mem.put_u32(kCont + 0x88, 1);
        mem.put_u64(kCont + 0x98, mem.heap_addr(kBucket));
        mem.put_u64(kCont + 0xA0, mem.heap_addr(kNodes));
        mem.put_u32(kBucket + 0, 2);                       // 이 버킷의 항목 수
        mem.put_u32(kBucket + 8 + 0, 0xB0100001);          // 키
        mem.put_u32(kBucket + 8 + 4, 0);                   // 노드 색인
        mem.put_u32(kBucket + 16 + 0, 0xB0100002);
        mem.put_u32(kBucket + 16 + 4, 1);
        mem.put_u64(kNodes + 0, mem.heap_addr(kNode0));
        mem.put_u64(kNodes + 8, mem.heap_addr(kNode1));
        mem.put_u32(kNode0 + 4, 0xB0100001);
        mem.put_u64(kNode0 + 8, actor(0));
        mem.put_u32(kNode1 + 4, 0xB0100002);
        mem.put_u64(kNode1 + 8, actor(1));
        put_actor(0, 4074);
        put_actor(1, 3);
        put_actor(2, 100);
    }
};

}  // namespace

TEST(actors_manager_validation) {
    Fixture f;
    CHECK(cdtb::game::looks_like_actor_manager(f.mem, f.mem.heap_addr(Fixture::kMgr)));
    // 버킷 용량이 상한을 넘으면 그 버킷은 무시 - 다른 버킷이 있어 여전히 통과
    f.mem.put_u32(Fixture::kMgr + 0x134, 100000);
    CHECK(cdtb::game::looks_like_actor_manager(f.mem, f.mem.heap_addr(Fixture::kMgr)));
    // 둘 다 망가지면 실패 (B 의 첫 칸을 0 으로)
    f.mem.put_u64(Fixture::kArrB + 0, 0);
    CHECK(!cdtb::game::looks_like_actor_manager(f.mem, f.mem.heap_addr(Fixture::kMgr)));
    CHECK(!cdtb::game::looks_like_actor_manager(f.mem, 0));
}

TEST(actors_walk_stops_at_terminator_and_dedups) {
    Fixture f;
    std::vector<std::uintptr_t> ptrs;
    CHECK(cdtb::game::walk_actor_pointers(f.mem, f.mem.heap_addr(Fixture::kMgr), &ptrs));
    CHECK_EQ(ptrs.size(), static_cast<std::size_t>(2));
    bool has0 = false, has1 = false, has2 = false;
    for (auto p : ptrs) {
        if (p == f.actor(0)) has0 = true;
        if (p == f.actor(1)) has1 = true;
        if (p == f.actor(2)) has2 = true;
    }
    CHECK(has0);
    CHECK(has1);
    CHECK(!has2);  // 끝 표시 너머
}

TEST(actors_character_row_chain) {
    Fixture f;
    std::uint16_t row = 0;
    CHECK(cdtb::game::actor_character_row(f.mem, f.actor(0), &row));
    CHECK_EQ(row, static_cast<std::uint16_t>(4074));
    CHECK(cdtb::game::actor_character_row(f.mem, f.actor(1), &row));
    CHECK_EQ(row, static_cast<std::uint16_t>(3));
    // 홀더가 없는 액터는 실패
    f.mem.put_u64(Fixture::kActor + 0x68, 0);
    CHECK(!cdtb::game::actor_character_row(f.mem, f.actor(0), &row));
    CHECK(!cdtb::game::actor_character_row(f.mem, 0, &row));
}

TEST(actors_snapshot_fills_rows_without_roster) {
    Fixture f;
    std::vector<cdtb::game::LiveActor> list;
    CHECK(cdtb::game::snapshot_live_actors(f.mem, f.mem.heap_addr(Fixture::kMgr), &list));
    CHECK_EQ(list.size(), static_cast<std::size_t>(2));
    // roster 가 비어 있으면 이름은 없고 행 번호만 있다
    for (const auto& la : list) {
        CHECK(la.row == 4074 || la.row == 3);
        CHECK(la.name.empty());
        CHECK(!la.is_companion());
    }
}

TEST(actors_handles_are_read_from_container) {
    Fixture f;
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> hs;
    CHECK(cdtb::game::read_actor_handles(f.mem, f.mem.heap_addr(Fixture::kMgr), &hs));
    CHECK_EQ(hs.size(), static_cast<std::size_t>(2));
    bool ok0 = false, ok1 = false;
    for (const auto& p : hs) {
        if (p.first == f.actor(0) && p.second == 0xB0100001u) ok0 = true;
        if (p.first == f.actor(1) && p.second == 0xB0100002u) ok1 = true;
    }
    CHECK(ok0);
    CHECK(ok1);
    // 노드의 키가 버킷과 다르면 버린다
    f.mem.put_u32(Fixture::kNode0 + 4, 0xDEADBEEF);
    CHECK(cdtb::game::read_actor_handles(f.mem, f.mem.heap_addr(Fixture::kMgr), &hs));
    CHECK_EQ(hs.size(), static_cast<std::size_t>(1));
}

TEST(actors_snapshot_fills_handles) {
    Fixture f;
    std::vector<cdtb::game::LiveActor> list;
    CHECK(cdtb::game::snapshot_live_actors(f.mem, f.mem.heap_addr(Fixture::kMgr), &list));
    CHECK_EQ(list.size(), static_cast<std::size_t>(2));
    for (const auto& la : list) {
        CHECK(la.handle == 0xB0100001u || la.handle == 0xB0100002u);
    }
}

TEST(actor_manager_candidate_cap_leaves_room_for_junk) {
    // 후보는 **주소 순**이라, 힙이 위쪽에 잡히는 실행에서는 vtable 값을 우연히
    // 담은 낮은 주소의 가짜가 앞자리를 다 차지한다. 2026-09-18 실측에서 진짜
    // 매니저가 11번째였고, 그때 상한이 8이라 월드 안인데도 못 찾았다.
    // 이 상수를 다시 줄이면 그 증상이 그대로 돌아온다.
    CHECK(cdtb::game::kActorManagerCandidates >= 64);
}
