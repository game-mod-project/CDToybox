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

std::string Rtti::class_of_vtable(std::uintptr_t vtable) const {
    if (image_.empty() || vtable < r_.module_base()) return {};
    const std::uintptr_t col_slot = vtable - 8;
    if (col_slot < r_.module_base()) return {};

    const std::size_t off = col_slot - r_.module_base();
    if (off + 8 > image_.size()) return {};

    std::uint64_t col = 0;
    std::memcpy(&col, image_.data() + off, 8);
    if (col < r_.module_base()) return {};

    const std::size_t col_off = col - r_.module_base();
    if (col_off + 16 > image_.size()) return {};

    std::uint32_t sig = 0, desc_rva = 0;
    std::memcpy(&sig, image_.data() + col_off, 4);
    std::memcpy(&desc_rva, image_.data() + col_off + 12, 4);
    if (sig != 0 && sig != 1) return {};
    if (desc_rva == 0 || desc_rva + kTypeDescriptorNameOffset >= image_.size()) {
        return {};
    }

    const std::size_t name_off = desc_rva + kTypeDescriptorNameOffset;
    std::size_t end = name_off;
    const std::size_t limit = (name_off + 512 < image_.size())
                                  ? name_off + 512
                                  : image_.size();
    while (end < limit && image_[end] != 0) ++end;
    if (end >= limit) return {};

    return std::string(reinterpret_cast<const char*>(image_.data() + name_off),
                       end - name_off);
}

std::string Rtti::class_of_object(std::uintptr_t object) const {
    std::uint64_t vt = 0;
    if (!r_.read(object, &vt, sizeof(vt))) return {};
    return class_of_vtable(static_cast<std::uintptr_t>(vt));
}

std::vector<Rtti::Ref> Rtti::find_refs(std::uintptr_t target,
                                       std::size_t max) const {
    std::vector<Ref> out;
    if (image_.empty() || target == 0) return out;

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
            if (v != target) continue;

            Ref ref;
            ref.slot = reg.base + i;

            // 앞쪽으로 훑어 vtable이 보이는 지점을 소유 객체의 시작으로
            // 본다. 객체는 vtable 포인터로 시작하므로 대개 맞는다.
            // 완전한 방법은 아니지만 사슬을 따라가기에는 충분하다.
            constexpr std::size_t kBack = 0x400;
            for (std::size_t back = 0; back <= kBack && back <= i; back += 8) {
                std::uint64_t cand;
                std::memcpy(&cand, buf.data() + i - back, 8);
                if (cand < r_.module_base()) continue;
                if (cand >= r_.module_base() + r_.module_size()) continue;
                const auto cls =
                    class_of_vtable(static_cast<std::uintptr_t>(cand));
                if (cls.empty()) continue;
                ref.owner = reg.base + i - back;
                ref.offset = back;
                ref.owner_class = cls;
                break;
            }
            out.push_back(ref);
            if (out.size() >= max) return out;
        }
    }
    return out;
}

std::vector<std::uintptr_t> Rtti::find_qword(std::uint64_t value,
                                             std::size_t max) const {
    std::vector<std::uintptr_t> out;
    if (image_.empty()) return out;
    for (std::size_t i = 0; i + 8 <= image_.size(); i += 8) {
        std::uint64_t v;
        std::memcpy(&v, image_.data() + i, 8);
        if (v != value) continue;
        out.push_back(r_.module_base() + i);
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<Rtti::Xref> Rtti::find_xrefs(std::uintptr_t target,
                                         std::size_t max) const {
    std::vector<Xref> out;
    if (image_.empty()) return out;

    const std::uintptr_t mb = r_.module_base();
    // 명령 길이는 7바이트로 고정된 형태만 다룬다. REX + 8B/8D + ModRM +
    // disp32. 이것으로 전역 접근의 대부분이 잡힌다.
    constexpr std::size_t kLen = 7;

    for (std::size_t i = 0; i + kLen <= image_.size(); ++i) {
        const std::uint8_t rex = image_[i];
        if (rex < 0x48 || rex > 0x4F) continue;   // REX.W 계열

        const std::uint8_t op = image_[i + 1];
        if (op != 0x8B && op != 0x8D) continue;

        const std::uint8_t modrm = image_[i + 2];
        if ((modrm & 0xC7) != 0x05) continue;     // mod=00, rm=101 → RIP 상대

        std::int32_t disp;
        std::memcpy(&disp, image_.data() + i + 3, 4);

        const std::uintptr_t insn = mb + i;
        const std::uintptr_t dst =
            insn + kLen + static_cast<std::intptr_t>(disp);
        if (dst != target) continue;

        Xref x;
        x.at = insn;
        x.opcode = op;
        // REX.R 이 reg 필드의 상위 비트를 준다.
        x.reg = static_cast<std::uint8_t>(((modrm >> 3) & 7) |
                                          (((rex >> 2) & 1) << 3));
        out.push_back(x);
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<Rtti::Found> Rtti::find_objects(const std::string& substring,
                                            std::size_t max) const {
    std::vector<Found> out;
    if (image_.empty()) return out;

    // 1) 모듈 안의 vtable 후보를 미리 해석해 캐시한다. 힙을 훑을 때마다
    //    RTTI를 되짚으면 너무 느리다.
    const std::uintptr_t mb = r_.module_base();
    const std::uintptr_t me = mb + r_.module_size();

    std::vector<std::pair<std::uintptr_t, std::string>> matching;
    for (std::size_t i = 8; i + 8 <= image_.size(); i += 8) {
        const std::uintptr_t vt = mb + i;
        const auto cls = class_of_vtable(vt);
        if (cls.empty()) continue;
        if (cls.find(substring) == std::string::npos) continue;
        matching.emplace_back(vt, cls);
    }
    if (matching.empty()) return out;

    // 2) 힙에서 그 vtable을 첫 슬롯으로 갖는 객체를 찾는다.
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
            if (v < mb || v >= me) continue;
            for (const auto& m : matching) {
                if (v != m.first) continue;
                out.push_back(Found{reg.base + i, m.second});
                break;
            }
            if (out.size() >= max) return out;
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
