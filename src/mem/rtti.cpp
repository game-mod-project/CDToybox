#include "mem/rtti.h"

#include <algorithm>
#include <cstring>

namespace cdtb::mem {
namespace {

// x64 TypeDescriptor: { void* vftable; void* spare; char name[]; }
// 이름이 오프셋 16에서 시작하므로, 문자열을 찾으면 16을 빼면 된다.
constexpr std::size_t kNameOffset = 16;

}  // namespace

bool Rtti::load_image(ImageLoad* stats) {
    if (stats != nullptr) *stats = ImageLoad{};

    const std::uintptr_t base = r_.module_base();
    const std::size_t size = r_.module_size();
    if (base == 0 || size == 0) return false;

    image_.assign(size, 0);
    // 한 번에 읽으면 중간에 접근 불가 페이지가 있어 실패한다.
    // 나눠 읽고 실패한 곳은 0으로 남긴다.
    constexpr std::size_t kChunk = 0x10000;
    for (std::size_t off = 0; off < size; off += kChunk) {
        const std::size_t n = (off + kChunk <= size) ? kChunk : size - off;
        const bool ok = r_.read(base + off, image_.data() + off, n);
        if (stats == nullptr) continue;
        ++stats->chunks;
        if (!ok) {
            ++stats->failed_chunks;
            stats->failed_bytes += n;
        }
    }
    return true;
}

void Rtti::ensure_index() const {
    std::call_once(index_once_, [this] {
        if (image_.empty()) return;

        // ① 타입 서술자. `.?AV...@@` 문자열 자리에서 이름과 서술자 주소를
        //    뽑는다. 이미지를 **한 번만** 훑는다.
        static const char kPrefix[] = ".?AV";
        constexpr std::size_t kPrefixLen = 4;
        types_.reserve(16384);
        for (std::size_t i = 0; i + kPrefixLen < image_.size(); ++i) {
            if (std::memcmp(image_.data() + i, kPrefix, kPrefixLen) != 0) {
                continue;
            }
            std::size_t end = i;
            const std::size_t limit =
                (i + 512 < image_.size()) ? i + 512 : image_.size();
            while (end < limit && image_[end] != 0) ++end;
            if (end >= limit) continue;

            std::string name(reinterpret_cast<const char*>(image_.data() + i),
                             end - i);
            if (name.find("@@") == std::string::npos) continue;
            if (i < kNameOffset) continue;

            types_.push_back(
                TypeInfo{std::move(name), r_.module_base() + i - kNameOffset});
        }

        // ② vtable -> 타입. `class_of_vtable` 이 되는 자리를 모은다.
        //    주소가 오름차순으로 쌓이므로 그대로 이분 탐색이 된다.
        const std::uintptr_t mb = r_.module_base();
        vtable_cls_.reserve(32768);
        std::vector<std::pair<const std::string*, std::size_t>> by_name;
        by_name.reserve(types_.size());
        for (std::size_t k = 0; k < types_.size(); ++k) {
            by_name.emplace_back(&types_[k].name, k);
        }
        std::sort(by_name.begin(), by_name.end(),
                  [](const auto& a, const auto& b) { return *a.first < *b.first; });

        for (std::size_t i = 8; i + 8 <= image_.size(); i += 8) {
            const std::uintptr_t vt = mb + i;
            const std::string cls = class_of_vtable(vt);
            if (cls.empty()) continue;
            const auto it =
                std::lower_bound(by_name.begin(), by_name.end(), cls,
                                 [](const auto& a, const std::string& b) {
                                     return *a.first < b;
                                 });
            if (it == by_name.end() || *it->first != cls) continue;
            vtable_cls_.emplace_back(vt, it->second);
        }
    });
}

bool Rtti::build_index() const {
    if (!types_.empty()) return false;
    ensure_index();
    return true;
}

Rtti::IndexStats Rtti::index_stats() const {
    IndexStats s;
    s.types = types_.size();
    s.vtables = vtable_cls_.size();
    s.ready = !types_.empty() || image_.empty();
    return s;
}

std::vector<Rtti::TypeInfo> Rtti::find_types(const std::string& substring,
                                             std::size_t max) const {
    std::vector<TypeInfo> out;
    if (image_.empty()) return out;
    ensure_index();
    for (const auto& t : types_) {
        if (!substring.empty() && t.name.find(substring) == std::string::npos) {
            continue;
        }
        out.push_back(t);
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

// 힙을 읽는 창. 예전에는 영역을 **통째로** 버퍼에 담았고, 그래서
// `reg.size > 256MB` 인 영역을 아예 건너뛰었다 - 버퍼가 그만큼 커지기 때문이다.
//
// **그 상한이 이 게임에서는 치명적이었다.** 힙이 29GB 이고 큰 영역이 흔한데,
// 거기 든 객체는 RTTI 탐색에 영영 안 보였다. 2026-09-18 실측: 수배 컴포넌트가
// 0x466ADF717A0 에 vtable·`+0x30` 까지 멀쩡한 채 있는데 `instances_of_class` 는
// 0곳을 돌려줬고, 화면은 "월드에 들어가면 저절로 잡습니다" 를 영원히 띄웠다.
// 같은 상한이 걸린 `heapfind`(29GB 중 11.4GB 만 훑었다)와 비교해 갈랐다.
//
// 창으로 자르면 메모리는 창 하나만 쓰고, 한 창이 읽기에 실패해도 **그 창만**
// 잃는다(예전에는 영역 하나가 통째로 날아갔다). 창 크기는 8의 배수라 8바이트
// 정렬 격자가 창 경계에서 어긋나지 않는다.
constexpr std::size_t kScanWindow = 32u << 20;

std::vector<std::uintptr_t> Rtti::instances_of_vtable(std::uintptr_t vtable,
                                                      std::size_t max) const {
    std::vector<std::uintptr_t> out;
    if (vtable == 0) return out;

    std::vector<std::uint8_t> buf;
    for (const auto& reg : r_.heap_regions()) {
        if (reg.size == 0) continue;
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);
        for (std::size_t done = 0; done < reg.size; done += kScanWindow) {
            const std::size_t n = (reg.size - done < kScanWindow)
                                      ? (reg.size - done)
                                      : kScanWindow;
            buf.resize(n);
            if (!r_.read(base + done, buf.data(), n)) continue;
            for (std::size_t i = 0; i + 8 <= n; i += 8) {
                std::uint64_t v;
                std::memcpy(&v, buf.data() + i, 8);
                if (v != vtable) continue;
                out.push_back(base + done + i);
                if (out.size() >= max) return out;
            }
        }
    }
    return out;
}

std::vector<Rtti::Found> Rtti::find_objects(const std::string& substring,
                                            std::size_t max) const {
    if (image_.empty()) return {};

    // vtable 해석은 색인에서 가져온다. 예전에는 여기서 이미지 전체를
    // 8바이트씩 훑으며 `class_of_vtable` 을 불렀는데, 이 함수를 부르는
    // 탐색마다 그 짓을 되풀이해 시작이 분 단위로 걸렸다.
    ensure_index();
    std::vector<std::pair<std::uintptr_t, const std::string*>> matching;
    matching.reserve(64);
    for (const auto& [vt, ti] : vtable_cls_) {
        const std::string& cls = types_[ti].name;
        if (cls.find(substring) == std::string::npos) continue;
        matching.emplace_back(vt, &cls);
    }
    return scan_heap(matching, max);
}

std::vector<Rtti::Found> Rtti::find_objects_of(const std::vector<std::string>& names,
                                               std::size_t max) const {
    if (image_.empty() || names.empty()) return {};
    // 이름이 전부 미리 모아져 있으면(prefetch) 힙을 다시 읽지 않는다 - 그 스냅숏을 주소 순으로.
    {
        std::lock_guard<std::mutex> lk(prefetch_mutex_);
        if (!prefetch_.empty()) {
            std::vector<Found> out;
            bool all = true;
            for (const auto& n : names) {
                const auto it = prefetch_.find(n);
                // 없거나 상한에 닿은 이름이 있으면 스냅숏이 모자랄 수 있다 - 걷는다(리뷰 P-1).
                if (it == prefetch_.end() ||
                    (prefetch_cap_ != 0 && it->second.size() >= prefetch_cap_)) {
                    all = false;
                    break;
                }
                for (const auto a : it->second) out.push_back(Found{a, n});
            }
            if (all) {
                std::sort(out.begin(), out.end(), [](const Found& x, const Found& y) {
                    return x.address < y.address;
                });
                if (out.size() > max) out.resize(max);
                return out;
            }
        }
    }
    ensure_index();
    std::vector<std::pair<std::uintptr_t, const std::string*>> matching;
    for (const auto& [vt, ti] : vtable_cls_) {
        const std::string& cls = types_[ti].name;
        for (const auto& n : names) {
            if (cls == n) {
                matching.emplace_back(vt, &cls);
                break;
            }
        }
    }
    return scan_heap(matching, max);
}

// 힙 영역을 한 번 훑어 matching 의 vtable 값을 담은 8바이트 자리를 전부 모은다. 영역 하나를
// 통째로 읽다 실패하면 그 영역은 건너뛴다 - 스캔이 매번 완전하지 않은 이유(game/equip.cpp
// 가 캐시로 메운다). max 는 전체 상한이라 여러 클래스를 모을 때는 넉넉히 준다. matching 은
// vtable 주소 오름차순으로 이분 탐색한다(색인 순서가 그렇다 - 아니면 여기서 정렬).
std::vector<Rtti::Found> Rtti::scan_heap(
    const std::vector<std::pair<std::uintptr_t, const std::string*>>& matching_in,
    std::size_t max, std::size_t per_class_max, std::size_t* capped_out) const {
    std::vector<Found> out;
    if (capped_out != nullptr) *capped_out = 0;
    if (matching_in.empty() || max == 0) return out;
    using Entry = std::pair<std::uintptr_t, const std::string*>;
    const auto by_vt = [](const Entry& a, const Entry& b) { return a.first < b.first; };
    std::vector<Entry> sorted;
    const std::vector<Entry>* matching = &matching_in;
    if (!std::is_sorted(matching_in.begin(), matching_in.end(), by_vt)) {
        sorted = matching_in;
        std::sort(sorted.begin(), sorted.end(), by_vt);
        matching = &sorted;
    }

    const std::uintptr_t mb = r_.module_base();
    const std::uintptr_t me = mb + r_.module_size();

    // 클래스별 상한: 같은 이름 포인터로 센다. 닿은 클래스는 건너뛰고, 전부 닿으면 끝낸다 -
    // 한 클래스의 가짜 후보가 다른 클래스를 굶기지 않게.
    std::unordered_map<const std::string*, std::size_t> per_class;
    std::size_t classes = 0, capped = 0;
    if (per_class_max != 0) {
        for (const auto& m : *matching) per_class.emplace(m.second, 0);
        classes = per_class.size();
    }

    std::vector<std::uint8_t> buf;
    for (const auto& reg : r_.heap_regions()) {
        if (reg.size == 0) continue;
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);
        for (std::size_t done = 0; done < reg.size; done += kScanWindow) {
        const std::size_t wn = (reg.size - done < kScanWindow)
                                   ? (reg.size - done)
                                   : kScanWindow;
        buf.resize(wn);
        if (!r_.read(base + done, buf.data(), wn)) continue;

        for (std::size_t i = 0; i + 8 <= wn; i += 8) {
            std::uint64_t v;
            std::memcpy(&v, buf.data() + i, 8);
            if (v < mb || v >= me) continue;
            const auto it = std::lower_bound(
                matching->begin(), matching->end(), v,
                [](const Entry& m, std::uintptr_t x) { return m.first < x; });
            if (it == matching->end() || it->first != v) continue;
            if (per_class_max != 0) {
                std::size_t& n = per_class[it->second];
                if (n >= per_class_max) continue;
                ++n;
                if (n == per_class_max) {
                    ++capped;
                    if (capped_out != nullptr) *capped_out = capped;
                }
            }
            out.push_back(Found{base + done + i, *it->second});
            if (out.size() >= max) return out;
            if (per_class_max != 0 && capped == classes) return out;
        }
        }
    }
    return out;
}

void Rtti::prefetch_instances(const std::vector<std::string>& names,
                              std::size_t max_per_class) const {
    std::unordered_map<std::string, std::vector<std::uintptr_t>> fresh;
    for (const auto& n : names) fresh.emplace(n, std::vector<std::uintptr_t>{});
    std::size_t objects = 0, capped = 0;
    if (!image_.empty() && !names.empty()) {
        ensure_index();
        std::vector<std::pair<std::uintptr_t, const std::string*>> matching;
        for (const auto& [vt, ti] : vtable_cls_) {
            const std::string& cls = types_[ti].name;
            if (fresh.find(cls) != fresh.end()) matching.emplace_back(vt, &cls);
        }
        const std::size_t total =
            max_per_class == 0 ? 4096 : max_per_class * names.size();
        for (const auto& f : scan_heap(matching, total, max_per_class, &capped)) {
            fresh[f.cls].push_back(f.address);
            ++objects;
        }
    }
    std::lock_guard<std::mutex> lk(prefetch_mutex_);
    prefetch_ = std::move(fresh);
    prefetch_objects_ = objects;
    prefetch_cap_ = max_per_class;
    prefetch_capped_ = capped;
}

void Rtti::clear_prefetch() const {
    std::lock_guard<std::mutex> lk(prefetch_mutex_);
    prefetch_.clear();
    prefetch_objects_ = 0;
    prefetch_cap_ = 0;
    prefetch_capped_ = 0;
}

Rtti::PrefetchStats Rtti::prefetch_stats() const {
    std::lock_guard<std::mutex> lk(prefetch_mutex_);
    return PrefetchStats{prefetch_.size(), prefetch_objects_, prefetch_capped_};
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
    // 미리 모아 둔 이름이면 힙을 다시 읽지 않는다(prefetch_instances 의 스냅숏). 단 스냅숏이
    // 상한에 닿았고 호출부가 그보다 많이 원하면 모자랄 수 있으니 걷는다(리뷰 P-1).
    {
        std::lock_guard<std::mutex> lk(prefetch_mutex_);
        const auto it = prefetch_.find(name);
        if (it != prefetch_.end() &&
            !(prefetch_cap_ != 0 && it->second.size() >= prefetch_cap_ && max > prefetch_cap_)) {
            out = it->second;
            if (out.size() > max) out.resize(max);
            return out;
        }
    }
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
