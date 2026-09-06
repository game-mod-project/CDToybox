#include <cstring>
#include <string>
#include <vector>

#include "game/localization.h"
#include "fake_memory.h"
#include "harness.h"
#include "mem/reader.h"
#include "mem/rtti.h"

namespace {

using cdtb::game::LocSystem;
using cdtb::tests::FakeMemory;

// str() 본문 28바이트를 만든다. global_rva 를 가리키도록 disp32 를 맞춘다.
std::vector<std::uint8_t> make_str_body(std::uint32_t at_rva,
                                        std::uint32_t global_rva) {
    std::vector<std::uint8_t> b = {
        0x8B, 0x41, 0x18,                          // mov eax,[rcx+0x18]
        0x48, 0x8B, 0x0D, 0, 0, 0, 0,              // mov rcx,[rip+disp]
        0x3B, 0x41, 0x60,                          // cmp eax,[rcx+0x60]
        0x72, 0x08,                                // jb +8
        0x48, 0x8D, 0x05, 0, 0, 0, 0,              // lea rax,[rip+disp]
        0xC3,                                      // ret
        0x48, 0x03, 0x41, 0x58,                    // add rax,[rcx+0x58]
        0xC3,                                      // ret
    };
    const std::int32_t disp =
        static_cast<std::int32_t>(global_rva) - static_cast<std::int32_t>(at_rva + 10);
    std::memcpy(b.data() + 6, &disp, 4);
    return b;
}

// 시스템 객체 + 카테고리 표 + 항목 + 문자열 풀을 갖춘 가짜 힙.
//
// 배치 (힙 오프셋):
//   0x0000  시스템 객체 (0x80 바이트)
//   0x0100  카테고리 표 (16 * 54)
//   0x0500  카테고리 3 의 포인터 배열 (8 * 3)
//   0x0600  항목 3개 (0x20 간격)
//   0x0800  문자열 풀
struct Fixture {
    FakeMemory mem;
    static constexpr std::size_t kSys = 0x0000;
    static constexpr std::size_t kCatTable = 0x0100;
    static constexpr std::size_t kPtrArray = 0x0500;
    static constexpr std::size_t kEntries = 0x0600;
    static constexpr std::size_t kPool = 0x0800;
    static constexpr int kCategory = 3;

    // 키는 오름차순이어야 이분 탐색이 성립한다.
    static constexpr std::uint64_t kKeyA = 0x0000089800000070ull;   // 2200
    static constexpr std::uint64_t kKeyB = 0x0000C35100000070ull;   // 50001
    static constexpr std::uint64_t kKeyC = 0x0000C35100000071ull;

    Fixture() {
        mem.heap.assign(0x1000, 0);
        mem.put_u64(kSys + 0x48, mem.heap_addr(kCatTable));
        mem.put_u64(kSys + 0x58, mem.heap_addr(kPool));
        mem.put_u32(kSys + 0x60, 0x40);

        // 카테고리 3 에만 항목 3개
        mem.put_u64(kCatTable + kCategory * 16 + 0, mem.heap_addr(kPtrArray));
        mem.put_u32(kCatTable + kCategory * 16 + 8, 3);

        const std::uint64_t keys[3] = {kKeyA, kKeyB, kKeyC};
        const std::uint32_t offs[3] = {0x00, 0x10, 0x20};
        for (int i = 0; i < 3; ++i) {
            const std::size_t e = kEntries + i * 0x20;
            mem.put_u64(kPtrArray + i * 8, mem.heap_addr(e));
            mem.put_u64(e + 0x10, keys[i]);
            mem.put_u32(e + 0x18, offs[i]);
            mem.put_u8(e + 0x1C, static_cast<std::uint8_t>(kCategory));
        }
        mem.put_str(kPool + 0x00, "무쇠 단검");
        mem.put_str(kPool + 0x10, "낡은 밧줄");
        mem.put_str(kPool + 0x20, "낡은 밧줄 설명");
    }

    LocSystem system() const {
        LocSystem s;
        s.object = mem.heap_addr(kSys);
        s.pool = mem.heap_addr(kPool);
        s.pool_size = 0x40;
        return s;
    }
};

}  // namespace

// ---------------------------------------------------------------- 키 조립

TEST(loc_key_packs_entity_into_high_32_bits) {
    // 문서에 기록된 실측 값: 아이템 2200 의 이름 키
    CHECK_EQ(cdtb::game::loc_key(2200, 0x70), 9448928051312ull);
    CHECK_EQ(cdtb::game::loc_key(50001, 0x70), 214752659767408ull);
    CHECK_EQ(cdtb::game::loc_key(50001, 0x71), 214752659767409ull);
}

TEST(loc_key_entity_returns_high_half) {
    CHECK_EQ(cdtb::game::loc_key_entity(214752659767409ull), 50001u);
    CHECK_EQ(cdtb::game::loc_key_field(214752659767409ull), 0x71u);
}

// -------------------------------------------------------- 전역 패턴 찾기

TEST(find_loc_global_rva_reads_disp_from_single_match) {
    std::vector<std::uint8_t> image(0x400, 0xCC);
    const std::uint32_t at = 0x100;
    const std::uint32_t global = 0x300;
    const auto body = make_str_body(at, global);
    std::memcpy(image.data() + at, body.data(), body.size());

    std::uint64_t rva = 0;
    CHECK(cdtb::game::find_loc_global_rva(image, &rva));
    CHECK_EQ(rva, static_cast<std::uint64_t>(global));
}

TEST(find_loc_global_rva_fails_when_absent) {
    std::vector<std::uint8_t> image(0x400, 0xCC);
    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_loc_global_rva(image, &rva));
}

TEST(find_loc_global_rva_fails_when_ambiguous) {
    // 두 곳 이상 일치하면 무엇을 고를지 알 수 없다. 실패로 친다 -
    // 카메라 조사에서 가짜 후보를 집어 11회 어긋난 적이 있다.
    std::vector<std::uint8_t> image(0x800, 0xCC);
    const auto a = make_str_body(0x100, 0x300);
    const auto b = make_str_body(0x400, 0x600);
    std::memcpy(image.data() + 0x100, a.data(), a.size());
    std::memcpy(image.data() + 0x400, b.data(), b.size());

    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_loc_global_rva(image, &rva));
}

// ------------------------------------------------------------ 이분 탐색

TEST(resolve_finds_first_key_in_sorted_array) {
    Fixture f;
    std::string text;
    int cat = -1;
    CHECK(cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyA, &text, &cat));
    CHECK_EQ(text, std::string("무쇠 단검"));
    CHECK_EQ(cat, Fixture::kCategory);
}

TEST(resolve_finds_middle_and_last_keys) {
    Fixture f;
    std::string text;
    CHECK(cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyB, &text, nullptr));
    CHECK_EQ(text, std::string("낡은 밧줄"));
    CHECK(cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyC, &text, nullptr));
    CHECK_EQ(text, std::string("낡은 밧줄 설명"));
}

TEST(resolve_fails_for_missing_key) {
    Fixture f;
    std::string text;
    CHECK(!cdtb::game::resolve(f.mem, f.system(), 0x1234ull, &text, nullptr));
}

TEST(resolve_fails_when_offset_is_unresolved) {
    // 0xFFFFFFFF 는 "아직 안 풀린 것"이다. 문자열이 아니다.
    Fixture f;
    f.mem.put_u32(Fixture::kEntries + 0x18, 0xFFFFFFFFu);
    std::string text;
    CHECK(!cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyA, &text, nullptr));
}

TEST(resolve_rejects_offset_past_pool_end) {
    // 게임 자신도 [sys+0x60] 과 비교해 걸러낸다. 같게 둔다.
    Fixture f;
    f.mem.put_u32(Fixture::kEntries + 0x18, 0x40u);
    std::string text;
    CHECK(!cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyA, &text, nullptr));
}

TEST(resolve_handles_empty_category_without_reading_array) {
    // 개수 0 이면 포인터 배열은 널일 수 있다. 만지면 안 된다.
    Fixture f;
    f.mem.put_u32(Fixture::kCatTable + Fixture::kCategory * 16 + 8, 0);
    f.mem.put_u64(Fixture::kCatTable + Fixture::kCategory * 16 + 0, 0);
    std::string text;
    CHECK(!cdtb::game::resolve(f.mem, f.system(), Fixture::kKeyA, &text, nullptr));
}

// ---------------------------------------------------------- 카테고리 표

TEST(loc_categories_reports_every_slot_with_counts) {
    Fixture f;
    std::vector<cdtb::game::LocCategory> cats;
    CHECK(cdtb::game::loc_categories(f.mem, f.system(), &cats));
    CHECK_EQ(cats.size(), static_cast<std::size_t>(cdtb::game::kLocCategoryCount));
    CHECK_EQ(cats[Fixture::kCategory].count, 3u);
    CHECK_EQ(cats[Fixture::kCategory].array, f.mem.heap_addr(Fixture::kPtrArray));
    CHECK_EQ(cats[0].count, 0u);
}

TEST(loc_categories_fails_when_table_pointer_is_null) {
    Fixture f;
    f.mem.put_u64(Fixture::kSys + 0x48, 0);
    std::vector<cdtb::game::LocCategory> cats;
    CHECK(!cdtb::game::loc_categories(f.mem, f.system(), &cats));
}

// ------------------------------------------------------ 시스템 찾기 전체

TEST(find_loc_system_follows_pattern_to_live_object) {
    Fixture f;
    // 모듈 이미지: 패턴 한 곳 + 전역 슬롯에 시스템 객체 주소
    const std::uint32_t at = 0x200;
    const std::uint32_t global = 0x1000;
    f.mem.image.assign(0x2000, 0xCC);
    const auto body = make_str_body(at, global);
    std::memcpy(f.mem.image.data() + at, body.data(), body.size());
    const std::uint64_t sys_addr = f.mem.heap_addr(Fixture::kSys);
    std::memcpy(f.mem.image.data() + global, &sys_addr, 8);

    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());

    LocSystem out;
    CHECK(cdtb::game::find_loc_system(rt, f.mem, &out));
    CHECK_EQ(out.object, sys_addr);
    CHECK_EQ(out.pool, f.mem.heap_addr(Fixture::kPool));
    CHECK_EQ(out.pool_size, 0x40u);
    CHECK(out.valid());
}

TEST(find_loc_system_fails_when_global_is_null) {
    // 출시 빌드에서 시스템이 안 만들어졌으면 전역이 비어 있다.
    // 디버그 콘솔 전역에서 겪을 수 있는 것과 같은 상황이다.
    Fixture f;
    const std::uint32_t at = 0x200;
    const std::uint32_t global = 0x1000;
    f.mem.image.assign(0x2000, 0xCC);
    const auto body = make_str_body(at, global);
    std::memcpy(f.mem.image.data() + at, body.data(), body.size());
    std::memset(f.mem.image.data() + global, 0, 8);

    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());

    LocSystem out;
    CHECK(!cdtb::game::find_loc_system(rt, f.mem, &out));
}
