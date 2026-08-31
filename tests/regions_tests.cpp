#include "harness.h"
#include "mem/regions.h"
#include "mem/safe_read.h"

using namespace cdtb::mem;

TEST(writable_regions_is_not_empty) {
    const auto r = writable_regions();
    CHECK(!r.empty());
}

TEST(writable_regions_are_sane_and_readable) {
    const auto regions = writable_regions();
    for (const auto& r : regions) {
        CHECK(r.begin != nullptr);
        CHECK(r.size > 0);
    }
    // 첫 몇 개 영역의 선두 4바이트를 실제로 읽어 본다.
    int probed = 0;
    for (const auto& r : regions) {
        float v = 0.0f;
        CHECK(safe_read_float(reinterpret_cast<std::uintptr_t>(r.begin), &v));
        if (++probed >= 10) break;
    }
    CHECK(probed > 0);
}

TEST(writable_regions_contain_a_heap_allocation) {
    // 방금 할당한 메모리가 열거 결과 안에 있어야 한다.
    auto* p = new float(42.0f);
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    const auto regions = writable_regions();
    bool found = false;
    for (const auto& r : regions) {
        const auto b = reinterpret_cast<std::uintptr_t>(r.begin);
        if (addr >= b && addr < b + r.size) { found = true; break; }
    }
    CHECK(found);
    delete p;
}

TEST(total_bytes_sums_region_sizes) {
    const auto regions = writable_regions();
    std::size_t manual = 0;
    for (const auto& r : regions) manual += r.size;
    CHECK_EQ(total_bytes(regions), manual);
    CHECK(total_bytes(regions) > 0);
}
