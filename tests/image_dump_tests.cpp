#include "harness.h"
#include "mem/image_dump.h"
#include "mem/module.h"

#include <cstring>
#include <string>
#include <vector>

using namespace cdtb::mem;

namespace {

// 시험용 PE32+ 헤더를 짓는다. 본문은 0 이어도 상관없다 -
// make_dump_loadable 은 헤더만 본다.
struct Section {
    const char* name;
    std::uint32_t rva;
    std::uint32_t vsize;
    std::uint32_t raw_size;
    std::uint32_t flags;
};

constexpr std::size_t kLfanew = 0x80;
constexpr std::size_t kOpt = kLfanew + 4 + 20;
constexpr std::size_t kOptSize = 240;      // PE32+ 표준 (112 + 16*8)
constexpr std::size_t kSecTable = kOpt + kOptSize;

template <class T>
void put(std::vector<std::uint8_t>& b, std::size_t off, T v) {
    std::memcpy(b.data() + off, &v, sizeof(T));
}
template <class T>
T get(const std::vector<std::uint8_t>& b, std::size_t off) {
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

std::vector<std::uint8_t> build_pe(const std::vector<Section>& secs,
                                   std::size_t total,
                                   std::uint16_t magic = 0x20B,
                                   std::uint32_t sect_align = 0x1000,
                                   std::uint32_t file_align = 0x200) {
    std::vector<std::uint8_t> b(total, 0);
    b[0] = 0x4D;   // M
    b[1] = 0x5A;   // Z
    put<std::uint32_t>(b, 0x3C, static_cast<std::uint32_t>(kLfanew));
    std::memcpy(b.data() + kLfanew, "PE\0\0", 4);

    put<std::uint16_t>(b, kLfanew + 4 + 0, 0x8664);              // Machine
    put<std::uint16_t>(b, kLfanew + 4 + 2,
                       static_cast<std::uint16_t>(secs.size()));  // 섹션 수
    put<std::uint16_t>(b, kLfanew + 4 + 16,
                       static_cast<std::uint16_t>(kOptSize));

    put<std::uint16_t>(b, kOpt + 0, magic);
    put<std::uint32_t>(b, kOpt + 16, 0x1234);                    // 진입점
    put<std::uint64_t>(b, kOpt + 24, 0x140000000ull);            // ImageBase
    put<std::uint32_t>(b, kOpt + 32, sect_align);
    put<std::uint32_t>(b, kOpt + 36, file_align);
    put<std::uint32_t>(b, kOpt + 56, 0x00100000);                // SizeOfImage
    put<std::uint32_t>(b, kOpt + 60, 0x400);                     // SizeOfHeaders

    for (std::size_t i = 0; i < secs.size(); ++i) {
        const std::size_t sh = kSecTable + i * 40;
        const std::size_t n = std::strlen(secs[i].name);
        std::memcpy(b.data() + sh, secs[i].name, n > 8 ? 8 : n);
        put<std::uint32_t>(b, sh + 8, secs[i].vsize);
        put<std::uint32_t>(b, sh + 12, secs[i].rva);
        put<std::uint32_t>(b, sh + 16, secs[i].raw_size);
        put<std::uint32_t>(b, sh + 20, 0x400);   // 파일 배치의 옛 오프셋
        put<std::uint32_t>(b, sh + 36, secs[i].flags);
    }
    return b;
}

constexpr std::uint32_t kX = 0x20000000;
constexpr std::uint32_t kR = 0x40000000;

}  // namespace

TEST(dump_maps_file_offsets_onto_rvas) {
    auto b = build_pe({{".text", 0x1000, 0x2000, 0x1800, kX | kR},
                       {".data", 0x4000, 0x1000, 0x1000, kR}},
                      0x10000);
    DumpFixup fx;
    std::string err;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x180000000ull, &fx, &err));
    CHECK(err.empty());

    // 파일 오프셋이 곧 RVA 다. 이것이 이 변환의 전부다.
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 20), std::uint32_t{0x1000});
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 40 + 20), std::uint32_t{0x4000});

    // RawSize 는 VirtSize 를 SectionAlignment 로 올린 값이다.
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 16), std::uint32_t{0x2000});
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 40 + 16), std::uint32_t{0x1000});
}

TEST(dump_writes_actual_base_and_matching_alignment) {
    auto b = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX | kR}}, 0x10000);
    DumpFixup fx;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x7FF612340000ull, &fx,
                             nullptr));

    // 뜬 주소를 그대로 박아야 디스어셈블러의 절대주소가 맞는다.
    CHECK_EQ(get<std::uint64_t>(b, kOpt + 24), std::uint64_t{0x7FF612340000ull});
    CHECK_EQ(fx.image_base, std::uint64_t{0x7FF612340000ull});

    // FileAlignment == SectionAlignment 여야 오프셋=RVA 가 유효하다.
    CHECK_EQ(get<std::uint32_t>(b, kOpt + 36), std::uint32_t{0x1000});
    // SizeOfHeaders 도 새 정렬의 배수로 올라간다 (0x400 -> 0x1000).
    CHECK_EQ(get<std::uint32_t>(b, kOpt + 60), std::uint32_t{0x1000});
}

TEST(dump_reports_sections_with_attributes) {
    auto b = build_pe({{".rdata", 0x1000, 0x3000, 0x3000, kX | kR},
                       {".data", 0x4000, 0x1000, 0x1000, kR}},
                      0x10000);
    DumpFixup fx;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x140000000ull, &fx, nullptr));

    CHECK_EQ(fx.sections.size(), std::size_t{2});
    CHECK(fx.sections[0].name == std::string(".rdata"));
    CHECK(fx.sections[0].executable);
    CHECK(!fx.sections[1].executable);
    CHECK_EQ(fx.entry_rva, std::uint32_t{0x1234});
    CHECK_EQ(fx.section_alignment, std::uint32_t{0x1000});
}

TEST(dump_clamps_section_that_runs_past_the_dump) {
    // 덤프가 0x5000 까지밖에 없는데 섹션은 0x4000+0x4000 을 주장한다.
    // 넘겨 두면 도구가 파일 밖을 읽으려다 이미지를 통째로 거부한다.
    auto b = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX | kR},
                       {".big", 0x4000, 0x4000, 0x4000, kR}},
                      0x5000);
    DumpFixup fx;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x140000000ull, &fx, nullptr));

    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 40 + 16), std::uint32_t{0x1000});
    CHECK(fx.sections[1].truncated);
    CHECK(!fx.sections[0].truncated);
}

TEST(dump_drops_section_that_starts_past_the_dump) {
    auto b = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX | kR},
                       {".gone", 0x9000, 0x1000, 0x1000, kR}},
                      0x5000);
    DumpFixup fx;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x140000000ull, &fx, nullptr));

    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 40 + 16), std::uint32_t{0});
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 40 + 20), std::uint32_t{0});
    CHECK(fx.sections[1].truncated);
}

TEST(dump_uses_raw_size_when_virtual_size_is_zero) {
    // 오래된 링커는 VirtualSize 를 0 으로 두고 RawSize 만 쓴다.
    // 그대로 0 으로 만들면 그 섹션이 통째로 사라진다.
    auto b = build_pe({{".text", 0x1000, 0, 0x1800, kX | kR}}, 0x10000);
    DumpFixup fx;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x140000000ull, &fx, nullptr));
    CHECK_EQ(get<std::uint32_t>(b, kSecTable + 16), std::uint32_t{0x2000});
}

TEST(dump_rejects_non_pe_input) {
    std::vector<std::uint8_t> junk(0x1000, 0xCC);
    std::string err;
    CHECK(!make_dump_loadable(junk.data(), junk.size(), 0x140000000ull, nullptr,
                              &err));
    CHECK(!err.empty());

    // MZ 는 맞지만 PE 서명이 없는 경우.
    junk[0] = 0x4D;
    junk[1] = 0x5A;
    put<std::uint32_t>(junk, 0x3C, 0x80);
    err.clear();
    CHECK(!make_dump_loadable(junk.data(), junk.size(), 0x140000000ull, nullptr,
                              &err));
    CHECK(!err.empty());
}

TEST(dump_rejects_pe32_and_bad_alignment) {
    auto b32 = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX}}, 0x10000, 0x10B);
    std::string err;
    CHECK(!make_dump_loadable(b32.data(), b32.size(), 0x140000000ull, nullptr,
                              &err));
    CHECK(err.find("PE32+") != std::string::npos);

    // SectionAlignment 가 2의 거듭제곱이 아니면 오프셋=RVA 를 못 만든다.
    auto bad = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX}}, 0x10000, 0x20B,
                        0x1500);
    err.clear();
    CHECK(!make_dump_loadable(bad.data(), bad.size(), 0x140000000ull, nullptr,
                              &err));
    CHECK(!err.empty());
}

TEST(dump_rejects_truncated_header) {
    auto b = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX}}, 0x10000);
    // 섹션 표가 이미지 밖에 놓이도록 크기만 줄여 넘긴다.
    std::string err;
    CHECK(!make_dump_loadable(b.data(), kSecTable + 8, 0x140000000ull, nullptr,
                              &err));
    CHECK(!err.empty());
}

TEST(dump_leaves_body_bytes_alone) {
    auto b = build_pe({{".text", 0x1000, 0x1000, 0x1000, kX | kR}}, 0x10000);
    b[0x1000] = 0x48;
    b[0x1001] = 0x8B;
    b[0x1002] = 0xC4;
    CHECK(make_dump_loadable(b.data(), b.size(), 0x140000000ull, nullptr,
                             nullptr));
    CHECK_EQ(b[0x1000], std::uint8_t{0x48});
    CHECK_EQ(b[0x1001], std::uint8_t{0x8B});
    CHECK_EQ(b[0x1002], std::uint8_t{0xC4});
}

// 합성 헤더만으로는 진짜 PE 를 만났을 때를 못 본다. 테스트 실행
// 파일 자신이 메모리에 매핑된 PE 이므로 그것으로 한 바퀴 돌린다.
// 게임에 붙는 것과 같은 모양의 입력이다 - 모듈 베이스부터
// SizeOfImage 까지의 바이트.
TEST(dump_fixup_runs_on_a_real_mapped_module) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    if (!m.has_value()) return;

    std::vector<std::uint8_t> img(m->base, m->base + m->size);
    DumpFixup fx;
    std::string err;
    CHECK(make_dump_loadable(img.data(), img.size(),
                             reinterpret_cast<std::uint64_t>(m->base), &fx,
                             &err));
    CHECK(err.empty());
    CHECK(!fx.sections.empty());
    CHECK_EQ(fx.image_base, reinterpret_cast<std::uint64_t>(m->base));

    bool any_exec = false;
    bool entry_covered = false;
    for (const auto& sc : fx.sections) {
        if (sc.executable) any_exec = true;
        // 고친 파일 범위가 덤프 안에 들어야 한다. 넘으면 도구가
        // 파일 밖을 읽으려다 이미지를 통째로 거부한다.
        CHECK(static_cast<std::size_t>(sc.rva) + sc.raw_size <= img.size());
        if (fx.entry_rva >= sc.rva &&
            fx.entry_rva < sc.rva + sc.raw_size) {
            entry_covered = true;
        }
    }
    CHECK(any_exec);
    CHECK(entry_covered);

    // 헤더만 고쳤으므로 본문은 매핑된 것과 같아야 한다.
    const std::size_t probe = fx.sections.back().rva;
    if (probe + 16 <= img.size()) {
        CHECK(std::memcmp(img.data() + probe, m->base + probe, 16) == 0);
    }
}
