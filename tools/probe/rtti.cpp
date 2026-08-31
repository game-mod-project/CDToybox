#include "rtti.h"

#include <cstring>

namespace cdtb::probe {
namespace {

// x64 TypeDescriptor: { void* vftable; void* spare; char name[]; }
// name 이 오프셋 16에서 시작하므로, 문자열을 찾으면 16을 빼면 된다.
constexpr std::size_t kTypeDescriptorNameOffset = 16;

}  // namespace

bool Rtti::load_image() {
    if (!r_.attached() || r_.module_size() == 0) return false;
    image_.assign(r_.module_size(), 0);

    // 이미지 전체를 한 번에 읽으면 중간에 접근 불가 페이지가 있어
    // 실패한다. 페이지 단위로 나눠 읽고 실패한 곳은 0으로 남긴다.
    constexpr std::size_t kChunk = 0x10000;
    const std::uintptr_t base = r_.module_base();
    for (std::size_t off = 0; off < image_.size(); off += kChunk) {
        const std::size_t n =
            (off + kChunk <= image_.size()) ? kChunk : image_.size() - off;
        r_.read(base + off, image_.data() + off, n);
    }
    return true;
}

std::vector<Rtti::TypeInfo> Rtti::find_types(const std::string& substring,
                                             std::size_t max) const {
    std::vector<TypeInfo> out;
    if (image_.empty()) return out;

    // 데코레이트된 이름은 항상 ".?AV" 로 시작한다.
    static const char kPrefix[] = ".?AV";
    constexpr std::size_t kPrefixLen = 4;

    for (std::size_t i = 0; i + kPrefixLen < image_.size(); ++i) {
        if (std::memcmp(image_.data() + i, kPrefix, kPrefixLen) != 0) continue;

        // 널 종단까지 읽는다. 이름 길이에 상한을 둬 쓰레기를 거른다.
        std::size_t end = i;
        const std::size_t limit =
            (i + 512 < image_.size()) ? i + 512 : image_.size();
        while (end < limit && image_[end] != 0) ++end;
        if (end >= limit) continue;

        const std::string name(reinterpret_cast<const char*>(image_.data() + i),
                               end - i);
        if (name.find("@@") == std::string::npos) continue;
        if (!substring.empty() && name.find(substring) == std::string::npos) {
            continue;
        }
        if (i < kTypeDescriptorNameOffset) continue;

        TypeInfo ti;
        ti.name = name;
        ti.descriptor = r_.module_base() + i - kTypeDescriptorNameOffset;
        out.push_back(ti);
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<std::uintptr_t> Rtti::vtables_for(
    std::uintptr_t descriptor) const {
    std::vector<std::uintptr_t> out;
    if (image_.empty() || descriptor < r_.module_base()) return out;

    // COL.pTypeDescriptor 는 절대주소가 아니라 모듈 베이스로부터의 RVA다.
    const auto desc_rva =
        static_cast<std::uint32_t>(descriptor - r_.module_base());

    // 1) 이 RVA를 담고 있는 4바이트 위치를 찾는다. 그중 COL 형태를
    //    만족하는 것만 남긴다. COL 레이아웃(x64):
    //      +0 signature(0|1)  +4 offset  +8 cdOffset
    //      +12 pTypeDescriptor(RVA)  +16 pClassDescriptor(RVA)
    std::vector<std::uintptr_t> cols;
    for (std::size_t i = 0; i + 4 <= image_.size(); i += 4) {
        std::uint32_t v;
        std::memcpy(&v, image_.data() + i, 4);
        if (v != desc_rva) continue;
        if (i < 12) continue;

        std::uint32_t sig;
        std::memcpy(&sig, image_.data() + i - 12, 4);
        if (sig != 0 && sig != 1) continue;

        cols.push_back(r_.module_base() + i - 12);
    }
    if (cols.empty()) return out;

    // 2) COL 주소를 8바이트로 담고 있는 위치가 vtable[-1] 이다.
    for (std::size_t i = 0; i + 8 <= image_.size(); i += 8) {
        std::uint64_t v;
        std::memcpy(&v, image_.data() + i, 8);
        if (v == 0) continue;
        for (const auto col : cols) {
            if (v != col) continue;
            out.push_back(r_.module_base() + i + 8);
            break;
        }
    }
    return out;
}

std::vector<std::uintptr_t> Rtti::instances_of(std::uintptr_t vtable,
                                               std::size_t max) const {
    std::vector<std::uintptr_t> out;
    if (vtable == 0) return out;

    // 객체의 첫 8바이트가 vtable 포인터다. 힙(이미지가 아닌 쓰기 가능
    // 영역)에서 그 값을 찾는다.
    const auto regions = r_.regions();
    std::vector<std::uint8_t> buf;

    for (const auto& reg : regions) {
        if (!reg.writable || reg.is_image) continue;
        if (reg.size == 0 || reg.size > (256u << 20)) continue;

        buf.assign(reg.size, 0);
        if (!r_.read(reg.base, buf.data(), buf.size())) continue;

        for (std::size_t i = 0; i + 8 <= buf.size(); i += 8) {
            std::uint64_t v;
            std::memcpy(&v, buf.data() + i, 8);
            if (v == vtable) {
                out.push_back(reg.base + i);
                if (out.size() >= max) return out;
            }
        }
    }
    return out;
}

}  // namespace cdtb::probe
