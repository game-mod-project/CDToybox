#include <cstring>
#include <vector>

#include "game/icon_map.h"
#include "harness.h"

namespace {

using cdtb::game::IconAtlas;

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v >> 16));
    b.push_back(static_cast<std::uint8_t>(v >> 24));
}

// 2x2 격자, 한 칸 4픽셀 -> 8x8 RGBA = 256바이트
std::vector<std::uint8_t> make_atlas(std::uint32_t magic = 0x43494443,
                                     std::uint32_t version = 1,
                                     bool full_pixels = true) {
    std::vector<std::uint8_t> b;
    put32(b, magic);
    put32(b, version);
    put32(b, 4);     // cell
    put32(b, 2);     // cols
    put32(b, 2);     // rows
    put32(b, 3);     // count
    // 일부러 키 순서를 흩뜨려 둔다. 파서가 정렬해야 한다.
    put32(b, 50001); put32(b, 1);
    put32(b, 2200);  put32(b, 0);
    put32(b, 950002); put32(b, 3);
    const std::size_t pixels = full_pixels ? 8 * 8 * 4 : 10;
    b.insert(b.end(), pixels, 0x7F);
    return b;
}

}  // namespace

TEST(parse_icon_atlas_reads_header_and_mapping) {
    const auto b = make_atlas();
    IconAtlas a;
    CHECK(cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
    CHECK_EQ(a.cell, 4u);
    CHECK_EQ(a.cols, 2u);
    CHECK_EQ(a.rows, 2u);
    CHECK_EQ(a.width(), 8u);
    CHECK_EQ(a.height(), 8u);
    CHECK_EQ(a.pixels_bytes(), static_cast<std::size_t>(8 * 8 * 4));
    CHECK_EQ(a.mapping.size(), static_cast<std::size_t>(3));
    CHECK(a.valid());
}

TEST(parse_icon_atlas_sorts_mapping_for_binary_search) {
    const auto b = make_atlas();
    IconAtlas a;
    CHECK(cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
    for (std::size_t i = 0; i + 1 < a.mapping.size(); ++i) {
        CHECK(a.mapping[i].first < a.mapping[i + 1].first);
    }
}

TEST(find_icon_cell_looks_up_by_key) {
    const auto b = make_atlas();
    IconAtlas a;
    CHECK(cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
    std::uint32_t cell = 0xFFFFFFFFu;
    CHECK(cdtb::game::find_icon_cell(a, 2200, &cell));
    CHECK_EQ(cell, 0u);
    CHECK(cdtb::game::find_icon_cell(a, 50001, &cell));
    CHECK_EQ(cell, 1u);
    CHECK(cdtb::game::find_icon_cell(a, 950002, &cell));
    CHECK_EQ(cell, 3u);
}

TEST(find_icon_cell_reports_missing_key) {
    const auto b = make_atlas();
    IconAtlas a;
    CHECK(cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
    std::uint32_t cell = 0;
    CHECK(!cdtb::game::find_icon_cell(a, 12345, &cell));
}

TEST(parse_icon_atlas_rejects_bad_magic) {
    const auto b = make_atlas(0xDEADBEEF);
    IconAtlas a;
    CHECK(!cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
}

TEST(parse_icon_atlas_rejects_unknown_version) {
    const auto b = make_atlas(0x43494443, 99);
    IconAtlas a;
    CHECK(!cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
}

TEST(parse_icon_atlas_rejects_truncated_pixels) {
    // 픽셀이 모자란 파일을 그대로 믿고 텍스처를 만들면 남의 메모리를
    // 읽는다. 길이를 먼저 본다.
    const auto b = make_atlas(0x43494443, 1, false);
    IconAtlas a;
    CHECK(!cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
}

TEST(parse_icon_atlas_rejects_truncated_header) {
    const auto b = make_atlas();
    IconAtlas a;
    CHECK(!cdtb::game::parse_icon_atlas(b.data(), 10, &a));
}

TEST(parse_icon_atlas_rejects_absurd_dimensions) {
    std::vector<std::uint8_t> b;
    put32(b, 0x43494443);
    put32(b, 1);
    put32(b, 4096);   // cell 이 상한을 넘는다
    put32(b, 2);
    put32(b, 2);
    put32(b, 0);
    IconAtlas a;
    CHECK(!cdtb::game::parse_icon_atlas(b.data(), b.size(), &a));
}
