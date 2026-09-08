#include <algorithm>
#include <cstring>
#include <vector>

#include "fake_memory.h"
#include "game/items.h"
#include "harness.h"

namespace {

using cdtb::game::ItemKeyMap;
using cdtb::game::ItemKeyPair;
using cdtb::tests::FakeMemory;

// 변환 함수 본문 40바이트를 만든다. global_rva 를 가리키도록 disp32 를
// 맞춘다.
//
// 실측 바이트 (docs/superpowers/specs/2026-09-02-inventory.md):
//   8B 44 24 30           mov   eax,[rsp+0x30]      스트림에서 읽은 키
//   48 8D 54 24 40        lea   rdx,[rsp+0x40]
//   48 8B 0D ?? ?? ?? ??  mov   rcx,[rip+disp]      <- 전역
//   48 83 C1 68           add   rcx,0x68            표는 객체의 +0x68
//   89 44 24 40           mov   [rsp+0x40],eax
//   E8 ?? ?? ?? ??        call  조회 (레코드 포인터를 낸다)
//   48 85 C0              test  rax,rax
//   74 0E                 jz    없음
//   0F B7 00              movzx eax,word ptr [rax]  레코드의 첫 u16
//   66 89 03              mov   [rbx],ax            그것이 짧은 식별자
std::vector<std::uint8_t> make_convert_body(std::uint32_t at_rva,
                                            std::uint32_t global_rva) {
    std::vector<std::uint8_t> b = {
        0x8B, 0x44, 0x24, 0x30,
        0x48, 0x8D, 0x54, 0x24, 0x40,
        0x48, 0x8B, 0x0D, 0, 0, 0, 0,
        0x48, 0x83, 0xC1, 0x68,
        0x89, 0x44, 0x24, 0x40,
        0xE8, 0, 0, 0, 0,
        0x48, 0x85, 0xC0,
        0x74, 0x0E,
        0x0F, 0xB7, 0x00,
        0x66, 0x89, 0x03,
    };
    const std::int32_t disp = static_cast<std::int32_t>(global_rva) -
                              static_cast<std::int32_t>(at_rva + 16);
    std::memcpy(b.data() + 12, &disp, 4);
    return b;
}

// 후보가 둘인 가짜 메모리. 실측 이미지에서 34곳이 걸렸으므로 후보가
// 하나인 상황을 기준으로 삼으면 안 된다.
//
// 배치:
//   이미지 RVA 0x100  변환 함수 A       RVA 0x800  전역 A
//          RVA 0x400  변환 함수 B       RVA 0x900  전역 B
//   힙     0x0000  객체 A (표 +0x68)
//          0x0100  해시 슬롯 A (8칸)      0x0200  레코드 포인터 A (3개)
//          0x0240  레코드 A (0x10 간격)
//          0x0400  객체 B (표 +0x68)
//          0x0500  레코드 포인터 B        0x0540  레코드 B
//          0x0600  해시 슬롯 B
//
// 레코드 값은 실측한 것을 쓴다 - 편전 / 화살 / 늑대의 한손검.
struct Fixture {
    FakeMemory mem;
    static constexpr std::uint32_t kGlobalA = 0x800;
    static constexpr std::uint32_t kGlobalB = 0x900;
    static constexpr std::size_t kObjA = 0x0000;
    static constexpr std::size_t kObjB = 0x0400;
    static constexpr std::size_t kMapA = kObjA + 0x68;
    static constexpr std::size_t kMapB = kObjB + 0x68;
    static constexpr std::size_t kSlotsA = 0x0100;
    static constexpr std::size_t kPtrsA = 0x0200;
    static constexpr std::size_t kRecsA = 0x0240;
    static constexpr std::size_t kSlotsB = 0x0600;
    static constexpr std::size_t kPtrsB = 0x0500;
    static constexpr std::size_t kRecsB = 0x0540;
    static constexpr std::size_t kRecStride = 0x10;
    static constexpr std::uint32_t kCapacity = 8;
    static constexpr std::uint32_t kCountA = 3;
    static constexpr std::uint32_t kCountB = 5;

    Fixture() {
        mem.image.assign(0x1000, 0xCC);
        const auto a = make_convert_body(0x100, kGlobalA);
        const auto b = make_convert_body(0x400, kGlobalB);
        std::memcpy(mem.image.data() + 0x100, a.data(), a.size());
        std::memcpy(mem.image.data() + 0x400, b.data(), b.size());

        mem.heap.assign(0x1000, 0);
        set_global(kGlobalA, mem.heap_addr(kObjA));
        set_global(kGlobalB, mem.heap_addr(kObjB));

        put_table(kMapA, kCountA, kCapacity, mem.heap_addr(kSlotsA), kCountA,
                  mem.heap_addr(kPtrsA));
        put_table(kMapB, kCountB, kCapacity, mem.heap_addr(kSlotsB), kCountB,
                  mem.heap_addr(kPtrsB));

        put_record(kPtrsA, kRecsA, 0, 1596, 2200);      // 편전
        put_record(kPtrsA, kRecsA, 1, 8296, 50001);     // 화살
        put_record(kPtrsA, kRecsA, 2, 3921, 1163042);   // 늑대의 한손검
        for (std::uint32_t i = 0; i < kCountB; ++i) {
            put_record(kPtrsB, kRecsB, i, 100 + i, 900000 + i);
        }
    }

    void set_global(std::uint32_t rva, std::uintptr_t value) {
        const std::uint64_t v = value;
        std::memcpy(mem.image.data() + rva, &v, 8);
    }
    void put_table(std::size_t map, std::uint32_t count, std::uint32_t cap,
                   std::uintptr_t slots, std::uint32_t rec_count,
                   std::uintptr_t records) {
        mem.put_u32(map + 0x04, count);
        mem.put_u32(map + 0x08, cap);
        mem.put_u32(map + 0x0C, rec_count);
        mem.put_u64(map + 0x10, slots);
        mem.put_u64(map + 0x18, records);
    }
    void put_record(std::size_t ptrs, std::size_t recs, std::uint32_t i,
                    std::uint16_t id, std::uint32_t key) {
        const std::size_t rec = recs + i * kRecStride;
        mem.put_u64(ptrs + i * 8, mem.heap_addr(rec));
        mem.put_u32(rec + 0x00, id);
        mem.put_u32(rec + 0x04, key);
    }
    ItemKeyMap map_a() const {
        ItemKeyMap m;
        m.global = FakeMemory::kModuleBase + kGlobalA;
        m.object = mem.heap_addr(kObjA);
        m.table = mem.heap_addr(kMapA);
        m.slots = mem.heap_addr(kSlotsA);
        m.records = mem.heap_addr(kPtrsA);
        m.count = kCountA;
        m.capacity = kCapacity;
        m.record_count = kCountA;
        return m;
    }
};

}  // namespace

// ---------------------------------------------------- 후보 전부 모으기

TEST(find_item_key_map_rvas_collects_every_match) {
    // 같은 꼴의 조회 코드가 표마다 있다. 한 곳만 일치하기를 기대하면
    // 안 된다 - 실측 이미지에서 34곳이 걸렸다.
    std::vector<std::uint8_t> image(0x1000, 0xCC);
    const auto a = make_convert_body(0x100, 0x800);
    const auto b = make_convert_body(0x400, 0x900);
    std::memcpy(image.data() + 0x100, a.data(), a.size());
    std::memcpy(image.data() + 0x400, b.data(), b.size());

    const auto rvas = cdtb::game::find_item_key_map_rvas(image, 8);
    CHECK_EQ(rvas.size(), std::size_t{2});
    CHECK_EQ(rvas[0], 0x800ull);
    CHECK_EQ(rvas[1], 0x900ull);
}

TEST(find_item_key_map_rvas_is_empty_when_absent) {
    std::vector<std::uint8_t> image(0x1000, 0xCC);
    CHECK(cdtb::game::find_item_key_map_rvas(image, 8).empty());
}

TEST(find_item_key_map_rvas_drops_disp_pointing_outside_image) {
    std::vector<std::uint8_t> image(0x1000, 0xCC);
    const auto a = make_convert_body(0x100, 0x900000);
    std::memcpy(image.data() + 0x100, a.data(), a.size());
    CHECK(cdtb::game::find_item_key_map_rvas(image, 8).empty());
}

// ------------------------------------------------------- 후보 가려내기

TEST(find_item_key_map_picks_the_candidate_whose_count_matches) {
    // 실측에서 후보 34곳 중 개수가 아이템 표와 같은(6810) 것은
    // 하나뿐이었다. 개수가 판별자다.
    Fixture f;
    ItemKeyMap m;
    CHECK(cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountA,
                                        &m));
    CHECK_EQ(m.global, FakeMemory::kModuleBase + Fixture::kGlobalA);
    CHECK_EQ(m.object, f.mem.heap_addr(Fixture::kObjA));
    CHECK_EQ(m.table, f.mem.heap_addr(Fixture::kMapA));
    CHECK_EQ(m.slots, f.mem.heap_addr(Fixture::kSlotsA));
    CHECK_EQ(m.records, f.mem.heap_addr(Fixture::kPtrsA));
    CHECK_EQ(m.count, Fixture::kCountA);
    CHECK_EQ(m.capacity, Fixture::kCapacity);
    CHECK_EQ(m.record_count, Fixture::kCountA);
}

TEST(find_item_key_map_picks_the_other_candidate_for_its_count) {
    Fixture f;
    ItemKeyMap m;
    CHECK(cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountB,
                                        &m));
    CHECK_EQ(m.object, f.mem.heap_addr(Fixture::kObjB));
    CHECK_EQ(m.count, Fixture::kCountB);
}

TEST(find_item_key_map_fails_when_no_candidate_matches_count) {
    Fixture f;
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, 99, &m));
}

TEST(find_item_key_map_fails_when_two_candidates_share_the_count) {
    // 같은 개수의 표가 둘이면 무엇이 아이템 표인지 고를 수 없다.
    Fixture f;
    f.put_table(Fixture::kMapB, Fixture::kCountA, Fixture::kCapacity,
                f.mem.heap_addr(Fixture::kSlotsB), Fixture::kCountA,
                f.mem.heap_addr(Fixture::kPtrsB));
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountA,
                                         &m));
}

TEST(find_item_key_map_fails_when_global_empty) {
    // 표가 아직 안 올라온 시점이다. 조용히 실패해야 재시도로 살아난다.
    Fixture f;
    f.set_global(Fixture::kGlobalA, 0);
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountA,
                                         &m));
}

TEST(find_item_key_map_rejects_bogus_capacity) {
    // 잘못 집으면 용량이 쓰레기값이 된다. 그대로 믿고 할당하면
    // 메모리를 통째로 먹는다.
    Fixture f;
    f.mem.put_u32(Fixture::kMapA + 0x08, 0x7FFFFFFFu);
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountA,
                                         &m));
}

TEST(find_item_key_map_rejects_count_above_capacity) {
    Fixture f;
    const std::uint32_t bad = Fixture::kCapacity + 1;
    f.mem.put_u32(Fixture::kMapA + 0x04, bad);
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, bad, &m));
}

TEST(find_item_key_map_rejects_missing_record_array) {
    // 레코드 배열이 없으면 짧은 식별자를 얻을 길이 없다.
    Fixture f;
    f.mem.put_u64(Fixture::kMapA + 0x18, 0);
    ItemKeyMap m;
    CHECK(!cdtb::game::find_item_key_map(f.mem, f.mem.image, Fixture::kCountA,
                                         &m));
}

// -------------------------------------------------------- 레코드 읽기

TEST(read_item_key_map_pairs_each_key_with_its_record_index) {
    // 인벤토리가 저장하는 값은 레코드의 순번이다. 실측: 인벤토리의
    // 5915 가 순번 5915(그로테반트 판금 투구)였고, 같은 인벤토리의
    // 5911·5912·5913·5916 이 그 세트의 갑옷·망토·장갑과 이웃 투구였다.
    Fixture f;
    std::vector<ItemKeyPair> pairs;
    CHECK(cdtb::game::read_item_key_map(f.mem, f.map_a(), &pairs));
    CHECK_EQ(pairs.size(), std::size_t{3});
    CHECK_EQ(pairs[0].key, 2200u);
    CHECK_EQ(pairs[0].id, 0u);
    CHECK_EQ(pairs[1].key, 50001u);
    CHECK_EQ(pairs[1].id, 1u);
    CHECK_EQ(pairs[2].key, 1163042u);
    CHECK_EQ(pairs[2].id, 2u);
}

TEST(read_item_key_map_keeps_the_index_of_records_after_a_null_slot) {
    // 널 슬롯을 건너뛰되 순번은 배열 위치 그대로여야 한다. 앞으로
    // 당기면 뒤의 아이템이 통째로 어긋난다.
    Fixture f;
    f.mem.put_u64(Fixture::kPtrsA + 8, 0);
    std::vector<ItemKeyPair> pairs;
    CHECK(cdtb::game::read_item_key_map(f.mem, f.map_a(), &pairs));
    CHECK_EQ(pairs.size(), std::size_t{2});
    CHECK_EQ(pairs[0].key, 2200u);
    CHECK_EQ(pairs[0].id, 0u);
    CHECK_EQ(pairs[1].key, 1163042u);
    CHECK_EQ(pairs[1].id, 2u);
}

TEST(read_item_key_map_ignores_the_first_field_of_the_record) {
    // 레코드 +0x00 의 u16 은 순번이 아니다 - 실측에서 순번 5915 의
    // 레코드는 946 을 들고 있었다.
    Fixture f;
    f.mem.put_u32(Fixture::kRecsA + 0x00, 946);
    std::vector<ItemKeyPair> pairs;
    CHECK(cdtb::game::read_item_key_map(f.mem, f.map_a(), &pairs));
    CHECK_EQ(pairs[0].id, 0u);
    CHECK_EQ(pairs[0].key, 2200u);
}

TEST(read_item_key_map_fails_without_records) {
    Fixture f;
    ItemKeyMap m = f.map_a();
    m.records = 0;
    std::vector<ItemKeyPair> pairs;
    CHECK(!cdtb::game::read_item_key_map(f.mem, m, &pairs));
}

// --- 키 -> 순번 되찾기 -------------------------------------------------
//
// 소켓 지급이 이것을 쓴다. 보관함 파일은 보석의 아이템 키를 들고
// 있는데 게임에 보내는 6바이트는 순번으로 시작한다. 순번은 표에서의
// 위치라 게임이 갱신되면 달라지므로 지급할 때 다시 찾는다.

TEST(find_item_id_returns_the_id_for_a_known_key) {
    const std::vector<cdtb::game::ItemKeyPair> sorted = {
        {105, 3}, {1002569, 3364}, {1002785, 3214}, {200914, 6438}};
    // 정렬돼 있어야 한다 - 부르는 쪽이 지킨다.
    std::vector<cdtb::game::ItemKeyPair> v = sorted;
    std::sort(v.begin(), v.end(),
              [](const cdtb::game::ItemKeyPair& a,
                 const cdtb::game::ItemKeyPair& b) { return a.key < b.key; });

    CHECK_EQ(cdtb::game::find_item_id(v, 1002569), std::uint32_t{3364});
    CHECK_EQ(cdtb::game::find_item_id(v, 200914), std::uint32_t{6438});
    CHECK_EQ(cdtb::game::find_item_id(v, 105), std::uint32_t{3});
}

TEST(find_item_id_says_no_for_an_unknown_key) {
    std::vector<cdtb::game::ItemKeyPair> v = {{100, 1}, {200, 2}, {300, 3}};
    CHECK_EQ(cdtb::game::find_item_id(v, 150), cdtb::game::kNoItemId);
    CHECK_EQ(cdtb::game::find_item_id(v, 0), cdtb::game::kNoItemId);
    CHECK_EQ(cdtb::game::find_item_id(v, 400), cdtb::game::kNoItemId);
}

TEST(find_item_id_says_no_on_an_empty_table) {
    const std::vector<cdtb::game::ItemKeyPair> v;
    CHECK_EQ(cdtb::game::find_item_id(v, 1002569), cdtb::game::kNoItemId);
}

// 순번 0 은 유효한 값이다. "못 찾음" 과 섞이면 첫 아이템이 사라진다.
TEST(find_item_id_treats_zero_as_a_real_id) {
    const std::vector<cdtb::game::ItemKeyPair> v = {{100, 0}, {200, 5}};
    CHECK_EQ(cdtb::game::find_item_id(v, 100), std::uint32_t{0});
}

// --- 소켓 6바이트 조립 -------------------------------------------------
//
// 게임의 복사 루프(0x234FC31~)가 `TrItemValue +0x40 + k*6` 을 그대로
// 옮기고 **다섯 번째 바이트만 칸 번호(k)로 덮어쓴다**. 그래서 우리는
// 순번·채움 표시·꼬리만 채우면 된다.
//
// 2026-09-07 에 라이브 인벤토리의 소켓 벡터를 전수로 떠서 고쳤다.
//
//   F6 0C FF FF 00 04   보석 3318 이 박힌 칸 0
//   FF FF 00 00 01 04   열려 있는 빈 칸 1
//   FF FF 00 00 FF ??   아직 안 열린 칸 (게임이 [5] 를 안 건드린다)
//
// 꼬리(`[5]`)는 **상수가 아니다.** 실행마다 바뀌는 세션 값이고(실측 세
// 번: 04 · 05 · 03), 로드할 때 게임이 다시 매긴다 - 2026-09-08 에 세션
// 값이 05 인 판에 04 를 실어 지급했더니 정상으로 들어왔고 재시작 뒤엔
// 03 으로 덮여 있었다. 그래서 아래 기대값의 0x04 는 "게임이 무시하는
// 아무 값" 이라는 뜻이다. 예전에 쓰던 0xFF 만은 라이브 어디에도 없던
// 값이라 피한다. specs/2026-09-07-socket-grant-unlock-research.md 2.2 절.

TEST(make_socket_bytes_matches_a_measured_entry) {
    std::uint8_t raw[6]{};
    cdtb::game::make_socket_bytes(3364, raw);
    const std::uint8_t want[6] = {0x24, 0x0D, 0xFF, 0xFF, 0x00, 0x04};
    CHECK(std::memcmp(raw, want, sizeof(want)) == 0);
}

TEST(make_socket_bytes_leaves_the_slot_byte_zero) {
    // 실측본은 칸 1 이라 다섯 번째가 0x01 이었다. 게임이 덮어쓰므로
    // 우리는 0 으로 둔다 - 나머지 다섯 칸이 맞으면 된다.
    std::uint8_t raw[6]{};
    cdtb::game::make_socket_bytes(3214, raw);
    const std::uint8_t want[6] = {0x8E, 0x0C, 0xFF, 0xFF, 0x00, 0x04};
    CHECK(std::memcmp(raw, want, sizeof(want)) == 0);
}

TEST(make_socket_bytes_marks_an_empty_open_slot) {
    // 보석 없이 칸만 여는 꼴(= 어비스 슬롯 락 우회). 채움 표시가
    // 0xFFFF 가 아니라 0x0000 이어야 게임이 빈 칸으로 본다.
    std::uint8_t raw[6]{};
    cdtb::game::make_socket_bytes(0xFFFF, raw);
    const std::uint8_t want[6] = {0xFF, 0xFF, 0x00, 0x00, 0x00, 0x04};
    CHECK(std::memcmp(raw, want, sizeof(want)) == 0);
}

TEST(make_socket_bytes_writes_the_id_little_endian) {
    std::uint8_t raw[6]{};
    cdtb::game::make_socket_bytes(0x0102, raw);
    CHECK_EQ(raw[0], std::uint8_t{0x02});
    CHECK_EQ(raw[1], std::uint8_t{0x01});
}

// 대응표가 없으면 조립하지 않는다. 엉뚱한 순번을 보내면 다른 보석이
// 박히므로 조용히 넘기지 않고 거절한다.
TEST(socket_bytes_for_key_refuses_without_the_map) {
    std::uint8_t raw[6]{};
    CHECK(!cdtb::game::socket_bytes_for_key(1002569, raw));
}

TEST(socket_bytes_for_key_refuses_a_null_buffer) {
    CHECK(!cdtb::game::socket_bytes_for_key(1002569, nullptr));
}
