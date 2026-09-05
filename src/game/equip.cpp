#include "game/equip.h"

#include <cstring>

#include <windows.h>

#include <atomic>
#include <mutex>

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

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool wr8(std::uintptr_t a, std::uint8_t v) {
    __try {
        *reinterpret_cast<volatile std::uint8_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 한 entry 의 소켓 k 채우기/비우기. 잠김 거부, read-back 검증.
bool socket_fill_entry(const mem::Reader& r, std::uintptr_t entry, int k,
                       std::uint16_t gem) {
    if (k < 0 || k > 4) return false;
    const std::uintptr_t sp = rd64(r, entry + 0x60);
    if (!ptr_reads(r, sp)) return false;
    const std::uintptr_t rec = sp + static_cast<std::uintptr_t>(k) * 6;
    if (rd8(r, rec + 4) == 0xFF) return false;  // 잠긴 소켓
    const std::uint16_t mk = (gem == 0xFFFF) ? 0 : 0xFFFF;
    if (!wr16(rec, gem)) return false;
    if (!wr16(rec + 2, mk)) return false;
    return rd16(r, rec) == gem;
}

bool refine_set_entry(const mem::Reader& r, std::uintptr_t entry,
                      std::uint16_t lvl) {
    if (!wr16(entry + 0x0A, lvl)) return false;
    return rd16(r, entry + 0x0A) == lvl;
}

bool dye_set_entry(const mem::Reader& r, std::uintptr_t entry, int rec,
                   std::uint8_t rr, std::uint8_t gg, std::uint8_t bb) {
    const std::uintptr_t dp = rd64(r, entry + 0x78);
    const std::uint32_t dc = rd32(r, entry + 0x80);
    if (!ptr_reads(r, dp) || rec < 0 || rec >= static_cast<int>(dc) || dc > 12)
        return false;
    const std::uintptr_t a = dp + static_cast<std::uintptr_t>(rec) * 16;
    if (!wr8(a + 7, rr) || !wr8(a + 8, gg) || !wr8(a + 9, bb)) return false;
    return rd8(r, a + 7) == rr;
}

// 테이블에서 인스턴스 ID 로 entry 를 찾는다(빈 슬롯 제외).
std::uintptr_t entry_by_instance(const mem::Reader& r, const EquipTable& t,
                                 std::uint64_t inst) {
    if (inst == 0) return 0;
    const int lim = t.cnt < 64 ? static_cast<int>(t.cnt) : 64;
    for (int i = 0; i < lim; ++i) {
        const std::uintptr_t e =
            t.arr + static_cast<std::uintptr_t>(i) * t.stride;
        const std::uint32_t key = rd32(r, e + 0x08) & 0xFFFF;
        if (key != 0xFFFF && rd64(r, e + 0x00) == inst) return e;
    }
    return 0;
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


int collect_equip_tables(const mem::Rtti& rtti, const mem::Reader& reader,
                         std::vector<EquipTable>* out) {
    if (out == nullptr) return 0;
    out->clear();
    // 서버·클라 장비 컴포넌트를 한 번의 힙 스캔으로 모두 찾는다(부분일치).
    const auto objs = rtti.find_objects("EquipSlotActorComponent", 64);
    std::vector<WornPiece> tmp;
    for (const auto& o : objs) {
        EquipTable t;
        if (!find_equip_table(reader, o.address, &t)) continue;
        if (!read_worn_gear(reader, t, &tmp) || tmp.size() < 3) continue;
        bool dup = false;
        for (const auto& e : *out) {
            if (e.arr == t.arr) { dup = true; break; }
        }
        if (!dup) out->push_back(t);
    }
    return static_cast<int>(out->size());
}

bool read_player_worn(const mem::Rtti& rtti, const mem::Reader& reader,
                      EquipTable* table_out, std::vector<WornPiece>* pieces_out) {
    std::vector<EquipTable> tabs;
    collect_equip_tables(rtti, reader, &tabs);
    EquipTable best;
    std::size_t bestN = 0;
    std::vector<WornPiece> bestPieces, tmp;
    for (const auto& t : tabs) {
        if (read_worn_gear(reader, t, &tmp) && tmp.size() > bestN) {
            bestN = tmp.size();
            best = t;
            bestPieces = tmp;
        }
    }
    if (bestN == 0) return false;
    if (table_out) *table_out = best;
    if (pieces_out) *pieces_out = std::move(bestPieces);
    return true;
}

// -------------------------------------------------------------------- 캐시
namespace {
std::mutex g_eq_mutex;
std::vector<EquipTable> g_eq_tables;   // both-realms 테이블(발견 캐시)
std::vector<WornPiece> g_eq_pieces;    // 플레이어 착용장비
bool g_eq_ready = false;
std::atomic<bool> g_eq_refresh{false};
}  // namespace

void equip_discover(const mem::Rtti& rtti, const mem::Reader& reader) {
    std::vector<EquipTable> tabs;
    collect_equip_tables(rtti, reader, &tabs);
    EquipTable pt;
    std::vector<WornPiece> pieces;
    const bool ok = read_player_worn(rtti, reader, &pt, &pieces);
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    g_eq_tables = std::move(tabs);
    if (ok) {
        g_eq_pieces = std::move(pieces);
        g_eq_ready = true;
    }
}

bool equip_snapshot(std::vector<WornPiece>* out) {
    if (out == nullptr) return false;
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    if (!g_eq_ready) return false;
    *out = g_eq_pieces;
    return true;
}

bool equip_ready() {
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    return g_eq_ready;
}

void equip_request_refresh() {
    g_eq_refresh.store(true, std::memory_order_release);
}
bool equip_take_refresh() {
    return g_eq_refresh.exchange(false, std::memory_order_acq_rel);
}

// op: 0=socket 1=refine 2=dye
static int eq_write_all(const mem::Reader& reader, std::uint64_t instance,
                        int op, int a, std::uint16_t b, std::uint8_t g,
                        std::uint8_t bl) {
    std::vector<EquipTable> tabs;
    {
        std::lock_guard<std::mutex> lk(g_eq_mutex);
        tabs = g_eq_tables;
    }
    int wrote = 0;
    for (const auto& t : tabs) {
        const std::uintptr_t e = entry_by_instance(reader, t, instance);
        if (e == 0) continue;
        bool ok = false;
        if (op == 0) ok = socket_fill_entry(reader, e, a, b);
        else if (op == 1) ok = refine_set_entry(reader, e, b);
        else if (op == 2) ok = dye_set_entry(reader, e, a,
                                              static_cast<std::uint8_t>(b), g, bl);
        if (ok) ++wrote;
    }
    return wrote;
}

int eq_write_socket(const mem::Reader& reader, std::uint64_t instance, int k,
                    std::uint16_t gem) {
    return eq_write_all(reader, instance, 0, k, gem, 0, 0);
}

int eq_write_refine(const mem::Reader& reader, std::uint64_t instance,
                    std::uint16_t level) {
    return eq_write_all(reader, instance, 1, 0, level, 0, 0);
}

int eq_write_dye(const mem::Reader& reader, std::uint64_t instance, int rec,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return eq_write_all(reader, instance, 2, rec, r, g, b);
}

}  // namespace cdtb::game
