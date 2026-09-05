#include "game/equip.h"

#include <cstring>

#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// MGRCHAIN (Nexus 3209 CT 그대로). 코어 전역 접근 사이트의 모양.
//   48 8B 05 <rel32 G> ... 48 8B ?? 68 48 8B ?? B8 00 00 00
// rel32=+3, pm=u8(+0x0F), blk=u8(+0x32), mo=u32(+0x36).
constexpr const char* kMgrChain =
    "48 8B 05 ?? ?? ?? ?? 48 8D 54 24 ?? 48 8B 48 ?? E8 ?? ?? ?? ?? 90 44 38 "
    "7C 24 ?? 0F 84 ?? ?? ?? ?? 48 8B ?? 24 ?? 48 85 ?? 0F 84 ?? ?? ?? ?? 48 "
    "8B ?? 68 48 8B ?? B8 00 00 00";

bool vp(std::uintptr_t p) {
    return p > 0x100000000ULL && p < 0x7FFFFFFFFFFFULL;
}

std::uint64_t rd64(const mem::Reader& r, std::uintptr_t a) {
    std::uint64_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint32_t rd32(const mem::Reader& r, std::uintptr_t a) {
    std::uint32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint16_t rd16(const mem::Reader& r, std::uintptr_t a) {
    std::uint16_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint8_t rd8(const mem::Reader& r, std::uintptr_t a) {
    std::uint8_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
bool ptr_reads(const mem::Reader& r, std::uintptr_t p) {
    if (!vp(p)) return false;
    std::uint64_t v = 0;
    return r.read(p, &v, sizeof(v));
}

// arr 을 stride 로 아이템 값 배열처럼 점수화. 빈칸(0xFFFF)도 decode 로 친다.
void equip_array_score(const mem::Reader& r, std::uintptr_t arr,
                       std::uint32_t stride, int n, int* good, int* real) {
    *good = 0;
    *real = 0;
    if (!ptr_reads(r, arr)) return;
    const int lim = n < 64 ? n : 64;
    for (int i = 0; i < lim; ++i) {
        const std::uintptr_t e = arr + static_cast<std::uintptr_t>(i) * stride;
        const std::uint32_t key = rd32(r, e + 0x08) & 0xFFFF;
        if (key == 0xFFFF) {
            ++*good;
        } else {
            if (key == 0 || key > 0x4000) break;
            if (rd64(r, e + 0x10) == 0) break;
            if (rd64(r, e + 0x00) == 0) break;
            ++*good;
            ++*real;
        }
    }
}

// 점유 엔트리의 서로 다른 슬롯 태그 수. 착용장비 식별의 핵심.
int equip_tag_score(const mem::Reader& r, std::uintptr_t arr,
                    std::uint32_t stride, int cnt) {
    if (stride < 8) return 0;
    bool seen[32] = {false};
    int n = 0;
    const int lim = cnt < 64 ? cnt : 64;
    for (int i = 0; i < lim; ++i) {
        const std::uintptr_t e = arr + static_cast<std::uintptr_t>(i) * stride;
        const std::uint32_t key = rd32(r, e + 0x08) & 0xFFFF;
        if (key != 0 && key != 0xFFFF) {
            const std::uint32_t tag = rd32(r, e + (stride - 8)) & 0xFFFF;
            if (tag <= 31 && !seen[tag]) {
                seen[tag] = true;
                ++n;
            }
        }
    }
    return n;
}

}  // namespace

bool resolve_equip_globals(const mem::Rtti& rtti, const mem::Reader& reader,
                           EquipGlobals* out) {
    if (out == nullptr) return false;
    const auto parsed = mem::parse_pattern(kMgrChain);
    if (!parsed) return false;
    const auto& img = rtti.image();
    const mem::Range range{img.data(), img.size()};
    const auto hits = mem::find_all(range, *parsed, 16);
    if (hits.empty()) return false;

    const std::uintptr_t base = reader.module_base();
    EquipGlobals g;
    bool first = true;
    for (const std::uint8_t* m : hits) {
        const std::size_t off = static_cast<std::size_t>(m - img.data());
        std::int32_t rel = 0;
        std::memcpy(&rel, m + 3, sizeof(rel));
        const std::uintptr_t grva =
            off + 7 + static_cast<std::uintptr_t>(static_cast<std::int64_t>(rel));
        const std::uintptr_t gabs = base + grva;
        const std::uint32_t pm = m[0x0F];
        const std::uint32_t blk = m[0x32];
        std::uint32_t mo = 0;
        std::memcpy(&mo, m + 0x36, sizeof(mo));
        if (first) {
            g.g = gabs;
            g.pm = pm;
            g.blk = blk;
            g.mo = mo;
            first = false;
        } else if (g.g != gabs || g.pm != pm || g.blk != blk || g.mo != mo) {
            return false;  // 사이트마다 다르면 해석 실패 (자기검증)
        }
    }
    *out = g;
    return g.g != 0;
}

std::uintptr_t equip_player_actor(const mem::Reader& reader,
                                  const EquipGlobals& g) {
    if (!g.ok()) return 0;
    const std::uintptr_t w = rd64(reader, g.g);
    if (!vp(w)) return 0;
    const std::uintptr_t pm = rd64(reader, w + g.pm);
    if (!vp(pm)) return 0;
    const std::uintptr_t actor = rd64(reader, pm + 0x50);
    return vp(actor) ? actor : 0;
}

std::uintptr_t equip_component(const mem::Reader& reader, std::uintptr_t actor,
                               std::uint32_t blk) {
    if (!vp(actor)) return 0;
    const std::uintptr_t sub = rd64(reader, actor + blk);
    if (vp(sub)) {
        const std::uintptr_t comp = rd64(reader, sub + 0x38);
        if (vp(comp) && rd64(reader, comp + 0x08) == actor) return comp;
        // 서브 안에서 백참조 검색
        for (std::uint32_t o = 0; o < 0x400; o += 8) {
            const std::uintptr_t p = rd64(reader, sub + o);
            if (vp(p) && rd64(reader, p + 0x08) == actor) return p;
        }
    }
    // 액터 안에서 백참조 검색
    for (std::uint32_t o = 0; o < 0x400; o += 8) {
        const std::uintptr_t p = rd64(reader, actor + o);
        if (vp(p) && rd64(reader, p + 0x08) == actor) return p;
    }
    return 0;
}

bool find_equip_table(const mem::Reader& reader, std::uintptr_t comp,
                      EquipTable* out) {
    if (out == nullptr || !vp(comp)) return false;
    static const std::uint32_t STR[] = {0xD0, 0xC8, 0xC0, 0xD8};
    static const std::uint32_t AO[] = {0x08, 0x00, 0x10, 0x18};
    static const std::uint32_t CO[] = {0x10, 0x08, 0x18, 0x0C, 0x20};
    EquipTable best;
    long bestScore = 0;
    for (std::uint32_t o = 0; o <= 0x3F8; o += 8) {
        const std::uintptr_t p = rd64(reader, comp + o);
        if (!ptr_reads(reader, p)) continue;
        // 직접 배열
        for (std::uint32_t st : STR) {
            int good = 0, real = 0;
            equip_array_score(reader, p, st, 16, &good, &real);
            if (good >= 8 && real > 0) {
                const long score =
                    static_cast<long>(equip_tag_score(reader, p, st, good)) *
                        100 +
                    real;
                if (score > bestScore) {
                    best = EquipTable{p, static_cast<std::uint32_t>(good), st};
                    bestScore = score;
                }
            }
        }
        // desc -> arr/cnt
        for (std::uint32_t ao : AO) {
            const std::uintptr_t a = rd64(reader, p + ao);
            if (!ptr_reads(reader, a)) continue;
            for (std::uint32_t co : CO) {
                const std::uint32_t c = rd32(reader, p + co);
                if (c < 1 || c > 64) continue;
                for (std::uint32_t st : STR) {
                    int good = 0, real = 0;
                    equip_array_score(reader, a, st, static_cast<int>(c), &good,
                                      &real);
                    if (good == static_cast<int>(c) && real > 0) {
                        const long score =
                            static_cast<long>(
                                equip_tag_score(reader, a, st, c)) *
                                100 +
                            real;
                        if (score > bestScore) {
                            best = EquipTable{a, c, st};
                            bestScore = score;
                        }
                    }
                }
            }
        }
    }
    if (bestScore == 0) return false;
    *out = best;
    return true;
}

int socket_unlocked(const mem::Reader& reader, std::uintptr_t entry) {
    const std::uintptr_t sp = rd64(reader, entry + 0x60);
    if (!ptr_reads(reader, sp)) return -1;
    int n = 0;
    for (int k = 0; k < 5; ++k) {
        const std::uintptr_t r = sp + static_cast<std::uintptr_t>(k) * 6;
        const std::uint8_t ix = rd8(reader, r + 4);
        const std::uint16_t mk = rd16(reader, r + 2);
        if (ix == 0xFF) break;
        if (ix != k) return -1;
        if (mk != 0xFFFF && mk != 0) return -1;
        ++n;
    }
    return n;
}

bool read_worn_gear(const mem::Reader& reader, const EquipTable& t,
                    std::vector<WornPiece>* out) {
    if (out == nullptr || !vp(t.arr) || t.stride < 8) return false;
    out->clear();
    const int lim = t.cnt < 64 ? static_cast<int>(t.cnt) : 64;
    for (int i = 0; i < lim; ++i) {
        const std::uintptr_t e =
            t.arr + static_cast<std::uintptr_t>(i) * t.stride;
        const std::uint32_t key = rd32(reader, e + 0x08) & 0xFFFF;
        if (key == 0 || key == 0xFFFF) continue;
        WornPiece w;
        w.entry = e;
        w.instance = rd64(reader, e + 0x00);
        w.key = key;
        w.refine = rd16(reader, e + 0x0A);
        w.slot_tag = static_cast<std::uint16_t>(rd32(reader, e + (t.stride - 8)) &
                                                0xFFFF);
        const std::uintptr_t sp = rd64(reader, e + 0x60);
        if (ptr_reads(reader, sp)) {
            for (int k = 0; k < 5; ++k) {
                const std::uintptr_t r = sp + static_cast<std::uintptr_t>(k) * 6;
                WornSocket s;
                s.gem = rd16(reader, r + 0);
                s.marker = rd16(reader, r + 2);
                s.index = rd8(reader, r + 4);
                w.sockets[k] = s;
                if (s.index != 0xFF && s.index == k) ++w.unlocked;
            }
        }
        out->push_back(w);
    }
    return true;
}

}  // namespace cdtb::game
