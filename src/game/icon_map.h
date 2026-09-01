#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace cdtb::game {

// 아이콘 아틀라스 파일을 읽는다. ImGui 도 D3D12 도 모른다.
//
// 아틀라스는 tools/icons/build_icons.py 가 한 번 만들어 둔다. 모드는
// 그 파일만 읽으므로 실행 중에 네트워크를 쓰지 않고, 원본 사이트가
// 바뀌거나 닫혀도 그대로 동작한다.
//
// 형식 (리틀엔디언):
//   u32 'CDIC'  u32 version=1
//   u32 cell    u32 cols  u32 rows  u32 count
//   count * { u32 아이템키, u32 칸번호 }
//   RGBA 픽셀 (cols*cell) * (rows*cell) * 4

struct IconAtlas {
    std::uint32_t cell = 0;
    std::uint32_t cols = 0;
    std::uint32_t rows = 0;
    std::size_t pixels_offset = 0;   // 버퍼 안의 픽셀 시작 위치
    // 키 오름차순으로 정렬해 둔다. 조회는 이분 탐색이다.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> mapping;

    std::uint32_t width() const { return cols * cell; }
    std::uint32_t height() const { return rows * cell; }
    std::size_t pixels_bytes() const {
        return static_cast<std::size_t>(width()) * height() * 4;
    }
    bool valid() const { return cell != 0 && cols != 0 && rows != 0; }
};

// 격자 한 칸이 이보다 크면 파일이 깨진 것으로 본다.
inline constexpr std::uint32_t kMaxIconCell = 256;
// 아틀라스 한 변의 상한. D3D12 텍스처 한도(16384)보다 넉넉히 아래로.
inline constexpr std::uint32_t kMaxIconSide = 8192;

// 머리말과 매핑을 읽는다. 버퍼가 모자라거나 값이 터무니없으면 false.
//
// 픽셀은 복사하지 않는다 - 20MB 를 두 번 들고 있을 이유가 없다.
// 호출자가 pixels_offset 부터 pixels_bytes() 만큼 쓰면 된다.
bool parse_icon_atlas(const std::uint8_t* data, std::size_t size,
                      IconAtlas* out);

// 키에 해당하는 칸 번호. 없으면 false.
bool find_icon_cell(const IconAtlas& atlas, std::uint32_t key,
                    std::uint32_t* cell_out);

}  // namespace cdtb::game
