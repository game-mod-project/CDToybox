#include "mem/image_dump.h"

#include <cstring>

namespace cdtb::mem {
namespace {

// PE32+ 옵션 헤더 안쪽 오프셋.
constexpr std::size_t kOptMagic = 0;
constexpr std::size_t kOptEntry = 16;
constexpr std::size_t kOptImageBase = 24;
constexpr std::size_t kOptSectionAlign = 32;
constexpr std::size_t kOptFileAlign = 36;
constexpr std::size_t kOptSizeOfImage = 56;
constexpr std::size_t kOptSizeOfHeaders = 60;

constexpr std::uint16_t kPe32Plus = 0x20B;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::uint32_t kExecutable = 0x20000000;

template <class T>
T read_at(const std::uint8_t* p, std::size_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}

template <class T>
void write_at(std::uint8_t* p, std::size_t off, T v) {
    std::memcpy(p + off, &v, sizeof(T));
}

std::uint32_t align_up(std::uint64_t v, std::uint32_t a) {
    if (a == 0) return static_cast<std::uint32_t>(v);
    const std::uint64_t r = (v + a - 1) / a * a;
    return (r > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<std::uint32_t>(r);
}

}  // namespace

bool make_dump_loadable(std::uint8_t* buf, std::size_t size,
                        std::uint64_t actual_base, DumpFixup* out,
                        std::string* err) {
    auto fail = [&](const char* m) {
        if (err != nullptr) *err = m;
        return false;
    };

    if (buf == nullptr) return fail("버퍼가 널입니다");
    if (size < 0x40) return fail("이미지가 너무 작습니다");
    if (buf[0] != 'M' || buf[1] != 'Z') return fail("MZ 서명이 없습니다");

    const auto lfanew = read_at<std::uint32_t>(buf, 0x3C);
    if (lfanew < 0x40 || lfanew + 24 > size) {
        return fail("e_lfanew 가 이미지 밖을 가리킵니다");
    }
    if (std::memcmp(buf + lfanew, "PE\0\0", 4) != 0) {
        return fail("PE 서명이 없습니다");
    }

    const std::size_t coff = lfanew + 4;
    const auto nsec = read_at<std::uint16_t>(buf, coff + 2);
    const auto opt_size = read_at<std::uint16_t>(buf, coff + 16);
    const std::size_t opt = coff + 20;

    if (nsec == 0) return fail("섹션이 없습니다");
    if (nsec > 96) return fail("섹션 수가 터무니없습니다");
    if (opt + opt_size > size) return fail("옵션 헤더가 이미지 밖입니다");
    if (opt_size < 112) return fail("옵션 헤더가 너무 짧습니다");

    if (read_at<std::uint16_t>(buf, opt + kOptMagic) != kPe32Plus) {
        return fail("PE32+ (x64) 가 아닙니다");
    }

    const std::size_t sec_table = opt + opt_size;
    if (sec_table + std::size_t{nsec} * kSectionHeaderSize > size) {
        return fail("섹션 표가 이미지 밖입니다");
    }

    const auto sect_align = read_at<std::uint32_t>(buf, opt + kOptSectionAlign);
    if (sect_align == 0 || (sect_align & (sect_align - 1)) != 0) {
        return fail("SectionAlignment 가 2의 거듭제곱이 아닙니다");
    }

    // 파일 오프셋을 RVA 와 같게 만든다. 그러려면 정렬 단위도 같아야
    // 로더가 값을 유효하다고 본다.
    write_at<std::uint32_t>(buf, opt + kOptFileAlign, sect_align);
    write_at<std::uint64_t>(buf, opt + kOptImageBase, actual_base);

    // SizeOfHeaders 도 새 FileAlignment 의 배수여야 한다. 헤더는
    // 첫 섹션 앞에만 있으므로 올려도 겹치지 않는다.
    const auto headers = read_at<std::uint32_t>(buf, opt + kOptSizeOfHeaders);
    write_at<std::uint32_t>(buf, opt + kOptSizeOfHeaders,
                            align_up(headers, sect_align));

    if (out != nullptr) {
        out->image_base = actual_base;
        out->section_alignment = sect_align;
        out->size_of_image = read_at<std::uint32_t>(buf, opt + kOptSizeOfImage);
        out->entry_rva = read_at<std::uint32_t>(buf, opt + kOptEntry);
        out->sections.clear();
    }

    for (std::uint16_t i = 0; i < nsec; ++i) {
        std::uint8_t* sh = buf + sec_table + std::size_t{i} * kSectionHeaderSize;

        const auto vsize = read_at<std::uint32_t>(sh, 8);
        const auto rva = read_at<std::uint32_t>(sh, 12);
        const auto old_raw = read_at<std::uint32_t>(sh, 16);
        const auto flags = read_at<std::uint32_t>(sh, 36);

        // VirtualSize 0 인 섹션은 RawSize 가 실제 크기다.
        const std::uint32_t want = align_up(vsize != 0 ? vsize : old_raw,
                                            sect_align);

        // 덤프 끝에 걸리면 있는 만큼만 준다. 넘겨 두면 도구가 파일
        // 밖을 읽으려다 이미지를 통째로 거부한다.
        std::uint32_t raw = want;
        bool truncated = false;
        if (rva >= size) {
            raw = 0;
            truncated = true;
        } else if (std::uint64_t{rva} + want > size) {
            raw = static_cast<std::uint32_t>(size - rva);
            truncated = true;
        }

        write_at<std::uint32_t>(sh, 16, raw);           // SizeOfRawData
        write_at<std::uint32_t>(sh, 20, raw ? rva : 0);  // PointerToRawData

        if (out != nullptr) {
            char name[9] = {0};
            std::memcpy(name, sh, 8);
            std::size_t nlen = 0;
            while (nlen < 8 && name[nlen] != 0) ++nlen;
            DumpSection s;
            s.name.assign(name, nlen);
            s.rva = rva;
            s.virtual_size = vsize;
            s.raw_size = raw;
            s.characteristics = flags;
            s.executable = (flags & kExecutable) != 0;
            s.truncated = truncated;
            out->sections.push_back(std::move(s));
        }
    }

    return true;
}

}  // namespace cdtb::mem
