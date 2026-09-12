#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "fake_memory.h"
#include "harness.h"
#include "mem/rtti.h"

// 가짜 RTTI 위에서 힙 스캔 계약을 못박는다 - 살아 있는 게임 관측(주소가 같더라)만으로는
// 게임이 갱신되면 재현이 안 된다(리뷰 O-7, 2026-09-12).
//
// 이미지 배치(색인이 읽는 그대로): 타입 서술자는 +16 에 `.?AV…@@` 이름, COL 은 +0 sig(0/1)
// +12 서술자 RVA, vtable 은 vtable-8 슬롯에 COL 절대주소. Foo 는 vtable 이 둘(다중 상속 흉내),
// Bar 는 하나. 힙에는 Foo 셋(둘째 vtable 하나 포함)·Bar 하나와 잡음 둘을 둔다.

namespace {

using cdtb::tests::FakeMemory;

constexpr std::uintptr_t kMb = FakeMemory::kModuleBase;

void put_img_u32(FakeMemory& m, std::size_t off, std::uint32_t v) {
    std::memcpy(m.image.data() + off, &v, 4);
}
void put_img_u64(FakeMemory& m, std::size_t off, std::uint64_t v) {
    std::memcpy(m.image.data() + off, &v, 8);
}
void put_img_str(FakeMemory& m, std::size_t off, const char* s) {
    std::memcpy(m.image.data() + off, s, std::strlen(s) + 1);
}

struct Fixture {
    static constexpr std::size_t kDescFoo = 0x100, kDescBar = 0x200;
    static constexpr std::size_t kColFoo = 0x300, kColBar = 0x400, kColFoo2 = 0x500;
    static constexpr std::size_t kSlotFoo = 0x800, kSlotBar = 0x900, kSlotFoo2 = 0xA00;
    // 힙 오프셋(8 정렬). 주소 오름차순으로 Foo, Bar, Foo(둘째 vtable), Foo.
    static constexpr std::size_t kObjFoo1 = 0x10, kObjBar = 0x40, kObjFoo2 = 0x80,
                                 kObjFoo3 = 0xC0;

    FakeMemory mem;

    static std::uintptr_t vt(std::size_t slot) { return kMb + slot + 8; }

    Fixture() {
        mem.image.assign(0x1000, 0);
        put_img_str(mem, kDescFoo + 16, ".?AVFoo@@");
        put_img_str(mem, kDescBar + 16, ".?AVBar@@");
        put_img_u32(mem, kColFoo, 1);
        put_img_u32(mem, kColFoo + 12, static_cast<std::uint32_t>(kDescFoo));
        put_img_u32(mem, kColBar, 0);
        put_img_u32(mem, kColBar + 12, static_cast<std::uint32_t>(kDescBar));
        put_img_u32(mem, kColFoo2, 1);
        put_img_u32(mem, kColFoo2 + 12, static_cast<std::uint32_t>(kDescFoo));
        put_img_u64(mem, kSlotFoo, kMb + kColFoo);
        put_img_u64(mem, kSlotBar, kMb + kColBar);
        put_img_u64(mem, kSlotFoo2, kMb + kColFoo2);

        mem.heap.assign(0x1000, 0);
        mem.put_u64(kObjFoo1, vt(kSlotFoo));
        mem.put_u64(kObjBar, vt(kSlotBar));
        mem.put_u64(kObjFoo2, vt(kSlotFoo2));
        mem.put_u64(kObjFoo3, vt(kSlotFoo));
        mem.put_u64(0x100, kMb + 0x1234);            // 모듈 안이지만 vtable 아님
        mem.put_u64(0x140, 0x1122334455667788ull);   // 모듈 밖
    }
};

std::vector<std::uintptr_t> addresses(const std::vector<cdtb::mem::Rtti::Found>& v) {
    std::vector<std::uintptr_t> out;
    for (const auto& f : v) out.push_back(f.address);
    return out;
}

}  // namespace

TEST(rtti_index_sees_the_fake_types_and_vtables) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    const auto st = [&] {
        rt.build_index();
        return rt.index_stats();
    }();
    CHECK_EQ(st.types, 2u);
    CHECK_EQ(st.vtables, 3u);   // Foo 둘 + Bar 하나
    CHECK_EQ(rt.class_of_vtable(Fixture::vt(Fixture::kSlotFoo)), std::string(".?AVFoo@@"));
    CHECK_EQ(rt.class_of_vtable(Fixture::vt(Fixture::kSlotFoo2)), std::string(".?AVFoo@@"));
    CHECK_EQ(rt.class_of_vtable(Fixture::vt(Fixture::kSlotBar)), std::string(".?AVBar@@"));
}

TEST(rtti_find_objects_of_collects_several_classes_in_one_pass) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    const auto found = rt.find_objects_of({".?AVFoo@@", ".?AVBar@@"}, 100);
    CHECK_EQ(found.size(), 4u);
    // 힙 주소 오름차순으로, 클래스가 섞여 나온다
    CHECK_EQ(found[0].address, f.mem.heap_addr(Fixture::kObjFoo1));
    CHECK_EQ(found[0].cls, std::string(".?AVFoo@@"));
    CHECK_EQ(found[1].address, f.mem.heap_addr(Fixture::kObjBar));
    CHECK_EQ(found[1].cls, std::string(".?AVBar@@"));
    CHECK_EQ(found[2].address, f.mem.heap_addr(Fixture::kObjFoo2));   // 둘째 vtable 도 Foo
    CHECK_EQ(found[2].cls, std::string(".?AVFoo@@"));
    CHECK_EQ(found[3].address, f.mem.heap_addr(Fixture::kObjFoo3));
    CHECK_EQ(found[3].cls, std::string(".?AVFoo@@"));
}

TEST(rtti_find_objects_of_equals_union_of_instances_of_class) {
    // 카메라 탐색이 예전에 클래스마다 따로 훑던 결과(vtable 마다 힙 전수)와 같은 집합이어야
    // 한다 - 그래야 합쳐도 잡던 객체를 놓치지 않는다.
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    std::vector<std::uintptr_t> old_way;
    for (const char* name : {".?AVFoo@@", ".?AVBar@@"}) {
        for (const auto a : rt.instances_of_class(name, 16)) old_way.push_back(a);
    }
    std::vector<std::uintptr_t> new_way =
        addresses(rt.find_objects_of({".?AVFoo@@", ".?AVBar@@"}, 100));
    std::sort(old_way.begin(), old_way.end());
    std::sort(new_way.begin(), new_way.end());
    CHECK(old_way == new_way);
    CHECK_EQ(new_way.size(), 4u);
}

TEST(rtti_find_objects_of_shares_one_cap_in_address_order) {
    // 상한은 클래스별이 아니라 전체 하나이고, 낮은 주소부터 채우다 멈춘다(리뷰 C-2).
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    const auto found = rt.find_objects_of({".?AVFoo@@", ".?AVBar@@"}, 2);
    CHECK_EQ(found.size(), 2u);
    CHECK_EQ(found[0].address, f.mem.heap_addr(Fixture::kObjFoo1));
    CHECK_EQ(found[1].address, f.mem.heap_addr(Fixture::kObjBar));
}

TEST(rtti_find_objects_of_ignores_unknown_and_empty_names) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    CHECK(rt.find_objects_of({".?AVNope@@"}, 100).empty());
    CHECK(rt.find_objects_of({}, 100).empty());
    // 완전 일치만 - 부분 문자열은 안 잡는다
    CHECK(rt.find_objects_of({"Foo"}, 100).empty());
    // 하나만 대면 그 클래스만
    const auto only_bar = rt.find_objects_of({".?AVBar@@"}, 100);
    CHECK_EQ(only_bar.size(), 1u);
    CHECK_EQ(only_bar[0].address, f.mem.heap_addr(Fixture::kObjBar));
}

TEST(rtti_find_objects_substring_still_finds_all_after_refactor) {
    // find_objects(부분 일치)는 몸통을 scan_heap 으로 옮겼을 뿐 결과가 같아야 한다.
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    const auto all = rt.find_objects("?AV", 100);
    CHECK_EQ(all.size(), 4u);
    const auto foo = rt.find_objects("Foo", 100);
    CHECK_EQ(foo.size(), 3u);
    CHECK(rt.find_objects("Nope", 100).empty());
    // 상한도 그대로 - 낮은 주소부터
    const auto two = rt.find_objects("?AV", 2);
    CHECK_EQ(two.size(), 2u);
    CHECK_EQ(two[1].address, f.mem.heap_addr(Fixture::kObjBar));
}

// ---- 통과 단위 미리 훑기(prefetch): 단계들이 힙을 다시 읽지 않고 스냅숏에서 낸다

TEST(rtti_prefetch_serves_snapshot_without_rescanning) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    rt.prefetch_instances({".?AVFoo@@", ".?AVBar@@"}, 64);
    CHECK_EQ(rt.prefetch_stats().names, 2u);
    CHECK_EQ(rt.prefetch_stats().objects, 4u);
    // 그 뒤 힙에 Foo 가 하나 더 생겨도 캐시는 그때의 스냅숏이다
    f.mem.put_u64(0x200, Fixture::vt(Fixture::kSlotFoo));
    CHECK_EQ(rt.instances_of_class(".?AVFoo@@", 16).size(), 3u);
    CHECK_EQ(rt.instances_of_class(".?AVBar@@", 16).size(), 1u);
    CHECK_EQ(rt.instances_of_class(".?AVFoo@@", 2).size(), 2u);   // max 는 캐시에서도
    // 부분 일치 경로(find_objects)는 캐시를 안 쓰므로 새 객체가 보인다
    CHECK_EQ(rt.find_objects("Foo", 100).size(), 4u);
    // find_objects_of 는 이름이 전부 캐시됐을 때만 스냅숏이다
    CHECK_EQ(rt.find_objects_of({".?AVFoo@@", ".?AVBar@@"}, 100).size(), 4u);
    CHECK_EQ(rt.find_objects_of({".?AVFoo@@"}, 100).size(), 3u);
    const auto both = rt.find_objects_of({".?AVBar@@", ".?AVFoo@@"}, 100);
    CHECK_EQ(both[0].address, f.mem.heap_addr(Fixture::kObjFoo1));   // 주소 오름차순
    CHECK_EQ(both[1].cls, std::string(".?AVBar@@"));
    // 비우면 다시 걷는다
    rt.clear_prefetch();
    CHECK_EQ(rt.prefetch_stats().names, 0u);
    CHECK_EQ(rt.instances_of_class(".?AVFoo@@", 16).size(), 4u);
    CHECK_EQ(rt.find_objects_of({".?AVFoo@@", ".?AVBar@@"}, 100).size(), 5u);
}

TEST(rtti_prefetch_caps_each_class_separately) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    rt.prefetch_instances({".?AVFoo@@", ".?AVBar@@"}, 2);
    const auto foo = rt.instances_of_class(".?AVFoo@@", 16);
    CHECK_EQ(foo.size(), 2u);   // 셋 중 낮은 주소 둘
    CHECK_EQ(foo[0], f.mem.heap_addr(Fixture::kObjFoo1));
    CHECK_EQ(foo[1], f.mem.heap_addr(Fixture::kObjFoo2));
    // Foo 가 상한에 닿아도 Bar 는 제 몫을 받는다
    CHECK_EQ(rt.instances_of_class(".?AVBar@@", 16).size(), 1u);
    CHECK_EQ(rt.prefetch_stats().objects, 3u);
}

TEST(rtti_prefetch_unknown_name_is_cached_as_empty_and_others_still_walk) {
    Fixture f;
    cdtb::mem::Rtti rt(f.mem);
    CHECK(rt.load_image());
    rt.prefetch_instances({".?AVNope@@"}, 64);
    CHECK_EQ(rt.prefetch_stats().names, 1u);
    CHECK_EQ(rt.prefetch_stats().objects, 0u);
    CHECK(rt.instances_of_class(".?AVNope@@", 16).empty());   // 걷지 않고 빈 결과
    CHECK_EQ(rt.instances_of_class(".?AVFoo@@", 16).size(), 3u);   // 모르는 이름은 걷는다
    rt.clear_prefetch();
}
