#include "game/icon_map.h"

#include <algorithm>
#include <cstring>

namespace cdtb::game {
namespace {

constexpr std::uint32_t kMagic = 0x43494443;   // 'CDIC'
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 24;       // u32 여섯 개

std::uint32_t read32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

}  // namespace

bool parse_icon_atlas(const std::uint8_t* data, std::size_t size,
                      IconAtlas* out) {
    if (data == nullptr || out == nullptr || size < kHeaderBytes) return false;
    if (read32(data) != kMagic) return false;
    if (read32(data + 4) != kVersion) return false;

    IconAtlas a;
    a.cell = read32(data + 8);
    a.cols = read32(data + 12);
    a.rows = read32(data + 16);
    const std::uint32_t count = read32(data + 20);

    if (a.cell == 0 || a.cell > kMaxIconCell) return false;
    if (a.cols == 0 || a.rows == 0) return false;
    if (a.width() > kMaxIconSide || a.height() > kMaxIconSide) return false;

    const std::size_t map_bytes = static_cast<std::size_t>(count) * 8;
    if (size < kHeaderBytes + map_bytes) return false;
    a.pixels_offset = kHeaderBytes + map_bytes;
    // 픽셀이 모자란 파일을 믿고 텍스처를 만들면 남의 메모리를 읽는다.
    if (size - a.pixels_offset < a.pixels_bytes()) return false;

    a.mapping.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* p = data + kHeaderBytes + static_cast<std::size_t>(i) * 8;
        a.mapping.emplace_back(read32(p), read32(p + 4));
    }
    // 이분 탐색을 쓰려면 정렬돼 있어야 한다. 파일 순서를 믿지 않는다.
    std::sort(a.mapping.begin(), a.mapping.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });

    *out = std::move(a);
    return true;
}

bool find_icon_cell(const IconAtlas& atlas, std::uint32_t key,
                    std::uint32_t* cell_out) {
    if (cell_out == nullptr) return false;
    const auto it = std::lower_bound(
        atlas.mapping.begin(), atlas.mapping.end(), key,
        [](const std::pair<std::uint32_t, std::uint32_t>& e, std::uint32_t k) {
            return e.first < k;
        });
    if (it == atlas.mapping.end() || it->first != key) return false;
    *cell_out = it->second;
    return true;
}

}  // namespace cdtb::game
