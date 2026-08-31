#include "harness.h"
#include "mem/module.h"

using namespace cdtb::mem;

TEST(find_module_returns_main_executable_for_null) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    CHECK(m->base != nullptr);
    CHECK(m->size > 0);
    // PE 이미지는 'MZ'로 시작한다.
    CHECK_EQ(m->base[0], std::uint8_t{'M'});
    CHECK_EQ(m->base[1], std::uint8_t{'Z'});
}

TEST(find_module_finds_a_loaded_system_dll) {
    auto m = find_module(L"kernel32.dll");
    CHECK(m.has_value());
    CHECK(m->base != nullptr);
}

TEST(find_module_returns_nullopt_for_unloaded_name) {
    auto m = find_module(L"cdtb_definitely_not_loaded_xyz.dll");
    CHECK(!m.has_value());
}

TEST(executable_ranges_are_non_empty_and_inside_module) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    auto ranges = executable_ranges(*m);
    CHECK(!ranges.empty());
    for (const auto& r : ranges) {
        CHECK(r.begin >= m->base);
        CHECK(r.size > 0);
        CHECK(r.begin + r.size <= m->base + m->size);
    }
}

// 스캐너가 실제 모듈에서 동작하는지 확인한다.
// 테스트 실행 파일 자신에 심은 고유 마커를 전체 이미지 범위에서 찾는다.
static volatile const std::uint8_t kMarker[16] = {
    0xC0, 0xDE, 0x70, 0x0B, 0x0C, 0xAF, 0xE1, 0x23,
    0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xED,
};

TEST(scanner_finds_marker_inside_own_module_image) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    auto pat = parse_pattern(
        "C0 DE 70 0B 0C AF E1 23 45 67 89 AB CD EF FE ED");
    CHECK(pat.has_value());
    const std::uint8_t* hit = find_first(Range{m->base, m->size}, *pat);
    CHECK(hit != nullptr);
    CHECK_EQ(hit, const_cast<const std::uint8_t*>(kMarker));
}
