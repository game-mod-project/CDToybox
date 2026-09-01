#include "harness.h"
#include "mem/safe_read.h"

#include <cstring>

using namespace cdtb::mem;

TEST(safe_read_float_reads_valid_address) {
    float v = 3.5f;
    float out = 0.0f;
    CHECK(safe_read_float(reinterpret_cast<std::uintptr_t>(&v), &out));
    CHECK(out == 3.5f);
}

TEST(safe_read_float_rejects_null) {
    float out = 0.0f;
    CHECK(!safe_read_float(0, &out));
}

TEST(safe_read_float_survives_bad_address) {
    // 매핑되지 않은 주소. 크래시하지 않고 false를 반환해야 한다.
    float out = 0.0f;
    CHECK(!safe_read_float(0x10, &out));
}

TEST(safe_write_float_writes_valid_address) {
    float v = 1.0f;
    CHECK(safe_write_float(reinterpret_cast<std::uintptr_t>(&v), 9.25f));
    CHECK(v == 9.25f);
}

TEST(safe_write_float_survives_bad_address) {
    CHECK(!safe_write_float(0x10, 1.0f));
}

TEST(safe_read_bytes_copies_exact_length) {
    const std::uint8_t src[] = {1, 2, 3, 4, 5};
    std::uint8_t dst[5]{};
    CHECK(safe_read_bytes(reinterpret_cast<std::uintptr_t>(src), dst, 5));
    CHECK(std::memcmp(src, dst, 5) == 0);
}

TEST(scan_region_floats_finds_matches_at_aligned_offsets) {
    alignas(4) float buf[8] = {0.f, 1.5f, 0.f, 1.5f, 0.f, 0.f, 1.5f, 0.f};
    std::uintptr_t out[8]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    CHECK(scan_region_floats(base, sizeof(buf), 1.5f, 0.0001f, out, 8, &count));
    CHECK_EQ(count, std::size_t{3});
    CHECK_EQ(out[0], reinterpret_cast<std::uintptr_t>(&buf[1]));
    CHECK_EQ(out[1], reinterpret_cast<std::uintptr_t>(&buf[3]));
    CHECK_EQ(out[2], reinterpret_cast<std::uintptr_t>(&buf[6]));
}

TEST(scan_region_floats_honours_epsilon) {
    alignas(4) float buf[4] = {1.0f, 1.0005f, 1.01f, 2.0f};
    std::uintptr_t out[4]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    CHECK(scan_region_floats(base, sizeof(buf), 1.0f, 0.001f, out, 4, &count));
    CHECK_EQ(count, std::size_t{2});   // 1.0 과 1.0005 만
}

TEST(scan_region_floats_stops_at_capacity) {
    alignas(4) float buf[8] = {5.f, 5.f, 5.f, 5.f, 5.f, 5.f, 5.f, 5.f};
    std::uintptr_t out[3]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    scan_region_floats(base, sizeof(buf), 5.0f, 0.0001f, out, 3, &count);
    CHECK_EQ(count, std::size_t{3});
}
