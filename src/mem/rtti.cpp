#include "mem/rtti.h"

#include <cstring>

namespace cdtb::mem {
namespace {

// x64 TypeDescriptor: { void* vftable; void* spare; char name[]; }
// 이름이 오프셋 16에서 시작하므로, 문자열을 찾으면 16을 빼면 된다.
constexpr std::size_t kNameOffset = 16;

}  // namespace

bool Rtti::load_image() {
    const std::uintptr_t base = r_.module_base();
    const std::size_t size = r_.module_size();
    if (base == 0 || size == 0) return false;

    image_.assign(size, 0);
    // 한 번에 읽으면 중간에 접근 불가 페이지가 있어 실패한다.
    // 나눠 읽고 실패한 곳은 0으로 남긴다.
    constexpr std::size_t kChunk = 0x10000;
    for (std::size_t off = 0; off < size; off += kChunk) {
        const std::size_t n = (off + kChunk <= size) ? kChunk : size - off;
        r_.read(base + off, image_.data() + off, n);
    }
    return true;
}

std::vector<Rtti::TypeInfo> Rtti::find_types(const std::string& substring,
                                             std::size_t max) const {
    std::vector<TypeInfo> out;
    if (image_.empty()) return out;

    static const char kPrefix[] = ".?AV";
    constexpr std::size_t kPrefixLen = 4;

    for (std::size_t i = 0; i + kPrefixLen < image_.size(); ++i) {
        if (std::memcmp(image_.data() + i, kPrefix, kPrefixLen) != 0) continue;

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
        if (i < kNameOffset) continue;

        out.push_back(TypeInfo{name, r_.module_base() + i - kNameOffset});
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<std::uintptr_t> Rtti::vtables_for(
    std::uintptr_t descriptor) const {
    std::vector<std::uintptr_t> out;
    const std::uintptr_t mb = r_.module_base();
    if (image_.empty() || descriptor < mb) return out;

    // COL.pTypeDescriptor 는 절대주소가 아니라 모듈 베이스 기준 RVA다.
    const auto desc_rva = static_cast<std::uint32_t>(descriptor - mb);

    std::vector<std::uintptr_t> cols;
    for (std::size_t i = 12; i + 4 <= image_.size(); i += 4) {
        std::uint32_t v;
        std::memcpy(&v, image_.data() + i, 4);
        if (v != desc_rva) continue;

        std::uint32_t sig;
        std::memcpy(&sig, image_.data() + i - 12, 4);
        if (sig != 0 && sig != 1) continue;
        cols.push_back(mb + i - 12);
    }
    if (cols.empty()) return out;

    for (std::size_t i = 0; i + 8 <= image_.size(); i += 8) {
        std::uint64_t v;
        std::memcpy(&v, image_.data() + i, 8);
        if (v == 0) continue;
        for (const auto col : cols) {
            if (v != col) continue;
            out.push_back(mb + i + 8);
            break;
        }
    }
    return out;
}

std::string Rtti::class_of_vtable(std::uintptr_t vtable) const {
    const std::uintptr_t mb = r_.module_base();
    if (image_.empty() || vtable < mb + 8) return {};

    const std::size_t slot = vtable - 8 - mb;
    if (slot + 8 > image_.size()) return {};

    std::uint64_t col = 0;
    std::memcpy(&col, image_.data() + slot, 8);
    if (col < mb) return {};

    const std::size_t col_off = col - mb;
    if (col_off + 16 > image_.size()) return {};

    std::uint32_t sig = 0, desc_rva = 0;
    std::memcpy(&sig, image_.data() + col_off, 4);
    std::memcpy(&desc_rva, image_.data() + col_off + 12, 4);
    if (sig != 0 && sig != 1) return {};
    if (desc_rva == 0 || desc_rva + kNameOffset >= image_.size()) return {};

    const std::size_t name_off = desc_rva + kNameOffset;
    std::size_t end = name_off;
    const std::size_t limit =
        (name_off + 512 < image_.size()) ? name_off + 512 : image_.size();
    while (end < limit && image_[end] != 0) ++end;
    if (end >= limit) return {};

    return std::string(reinterpret_cast<const char*>(image_.data() + name_off),
                       end - name_off);
}

std::string Rtti::class_of_object(std::uintptr_t object) const {
    std::uint64_t vt = 0;
    if (!r_.read_value(object, &vt)) return {};
    return class_of_vtable(static_cast<std::uintptr_t>(vt));
}

std::vector<std::uintptr_t> Rtti::instances_of_vtable(std::uintptr_t vtable,
                                                      std::size_t max) const {
    std::vector<std::uintptr_t> out;
    if (vtable == 0) return out;

    std::vector<std::uint8_t> buf;
    for (const auto& reg : r_.heap_regions()) {
        if (reg.size == 0 || reg.size > (256u << 20)) continue;

        buf.assign(reg.size, 0);
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);
        if (!r_.read(base, buf.data(), buf.size())) continue;

        for (std::size_t i = 0; i + 8 <= buf.size(); i += 8) {
            std::uint64_t v;
            std::memcpy(&v, buf.data() + i, 8);
            if (v != vtable) continue;
            out.push_back(base + i);
            if (out.size() >= max) return out;
        }
    }
    return out;
}

std::vector<Rtti::Found> Rtti::find_objects(const std::string& substring,
                                            std::size_t max) const {
    std::vector<Found> out;
    if (image_.empty()) return out;

    const std::uintptr_t mb = r_.module_base();
    const std::uintptr_t me = mb + r_.module_size();

    // vtable 해석을 미리 캐시한다. 힙을 훑을 때마다 RTTI를 되짚으면
    // 너무 느리다.
    std::vector<std::pair<std::uintptr_t, std::string>> matching;
    for (std::size_t i = 8; i + 8 <= image_.size(); i += 8) {
        const auto cls = class_of_vtable(mb + i);
        if (cls.empty()) continue;
        if (cls.find(substring) == std::string::npos) continue;
        matching.emplace_back(mb + i, cls);
    }
    if (matching.empty()) return out;

    std::vector<std::uint8_t> buf;
    for (const auto& reg : r_.heap_regions()) {
        if (reg.size == 0 || reg.size > (256u << 20)) continue;
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);

        buf.assign(reg.size, 0);
        if (!r_.read(base, buf.data(), buf.size())) continue;

        for (std::size_t i = 0; i + 8 <= buf.size(); i += 8) {
            std::uint64_t v;
            std::memcpy(&v, buf.data() + i, 8);
            if (v < mb || v >= me) continue;
            for (const auto& m : matching) {
                if (v != m.first) continue;
                out.push_back(Found{base + i, m.second});
                break;
            }
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
    constexpr std::size_t kLen = 7;   // REX + opcode + ModRM + disp32

    for (std::size_t i = 0; i + kLen <= image_.size(); ++i) {
        const std::uint8_t rex = image_[i];
        if (rex < 0x48 || rex > 0x4F) continue;

        const std::uint8_t op = image_[i + 1];
        if (op != 0x8B && op != 0x8D) continue;

        const std::uint8_t modrm = image_[i + 2];
        if ((modrm & 0xC7) != 0x05) continue;

        std::int32_t disp;
        std::memcpy(&disp, image_.data() + i + 3, 4);

        const std::uintptr_t insn = mb + i;
        if (insn + kLen + static_cast<std::intptr_t>(disp) != target) continue;

        Xref x;
        x.at = insn;
        x.opcode = op;
        x.reg = static_cast<std::uint8_t>(((modrm >> 3) & 7) |
                                          (((rex >> 2) & 1) << 3));
        out.push_back(x);
        if (out.size() >= max) break;
    }
    return out;
}

std::vector<Rtti::Ref> Rtti::find_refs(std::uintptr_t target,
                                       std::size_t max) const {
    std::vector<Ref> out;
    if (image_.empty() || target == 0) return out;

    const std::uintptr_t mb = r_.module_base();
    const std::uintptr_t me = mb + r_.module_size();
    std::vector<std::uint8_t> buf;

    for (const auto& reg : r_.heap_regions()) {
        if (reg.size == 0 || reg.size > (256u << 20)) continue;
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);

        buf.assign(reg.size, 0);
        if (!r_.read(base, buf.data(), buf.size())) continue;

        for (std::size_t i = 0; i + 8 <= buf.size(); i += 8) {
            std::uint64_t v;
            std::memcpy(&v, buf.data() + i, 8);
            if (v != target) continue;

            Ref ref;
            ref.slot = base + i;

            // 앞쪽으로 훑어 vtable이 보이는 지점을 소유 객체의 시작으로
            // 본다. 힙이 조밀하면 무관한 객체를 집을 수 있으므로
            // 범위를 좁게 둔다. 확정이 아니라 단서다.
            constexpr std::size_t kBack = 0x100;
            for (std::size_t back = 0; back <= kBack && back <= i; back += 8) {
                std::uint64_t cand;
                std::memcpy(&cand, buf.data() + i - back, 8);
                if (cand < mb || cand >= me) continue;
                const auto cls =
                    class_of_vtable(static_cast<std::uintptr_t>(cand));
                if (cls.empty()) continue;
                ref.owner = base + i - back;
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

std::vector<std::uintptr_t> Rtti::instances_of_class(const std::string& name,
                                                     std::size_t max) const {
    std::vector<std::uintptr_t> out;
    for (const auto& t : find_types(name, 32)) {
        if (t.name != name) continue;   // 완전 일치만
        for (const auto vt : vtables_for(t.descriptor)) {
            for (const auto a : instances_of_vtable(vt, max)) {
                out.push_back(a);
                if (out.size() >= max) return out;
            }
        }
        break;
    }
    return out;
}

}  // namespace cdtb::mem
