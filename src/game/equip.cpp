#include "game/equip.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <chrono>
#include <string>

#include "core/log.h"
#include "core/write_log.h"
#include "game/equip_bag.h"
#include "game/actors.h"
#include "game/items.h"
#include "game/inventory.h"
#include "game/player.h"
#include "game/roster.h"

namespace cdtb::game {
namespace {

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
    if (rd8(r, rec + 4) == kSocketLocked) return false;  // 잠긴 소켓
    const std::uint16_t mk = (gem == 0xFFFF) ? 0 : 0xFFFF;
    if (!wr16(rec, gem)) return false;
    if (!wr16(rec + 2, mk)) return false;
    return rd16(r, rec) == gem;
}

// 잠긴 칸을 연다. **이미 열린 칸과 박힌 보석은 안 건드린다.**
//
// 게임의 지급 코드(0x234F930)가 하는 것과 똑같이 쓴다:
//   레코드 +0x70 = 열 칸 수,  칸[k][4] = k
// 그 둘이 락의 전부다 - 검증도 체크섬도 서버 토큰도 없다(실측 2026-09-08,
// specs/2026-09-07-socket-grant-unlock-research.md 3.2·9절).
//
// 꼬리 바이트 `[5]` 는 판마다 달라지는 값이라 **같은 벡터의 살아 있는 칸에서
// 가져온다.** 열린 칸이 하나도 없으면 기댈 데가 없어 상수를 쓰는데, 게임이
// 로드할 때 다시 매기므로 문제되지 않는다(실측: 어긋난 값도 그대로 동작).
//
// 연 칸 수를 돌려준다. 아무것도 안 열었으면 0.
int socket_unlock_entry(const mem::Reader& r, std::uintptr_t entry, int want) {
    if (want < 1 || want > 5) return 0;
    const std::uintptr_t sp = rd64(r, entry + 0x60);
    if (!ptr_reads(r, sp)) return 0;

    const int cur = static_cast<int>(rd8(r, entry + 0x70));
    if (cur >= want || cur > 5) return 0;

    // 살아 있는 꼬리 값을 찾는다. 없으면 상수.
    std::uint8_t tail = kSocketOpenTail;
    for (int k = 0; k < cur; ++k) {
        const std::uintptr_t rec = sp + static_cast<std::uintptr_t>(k) * 6;
        if (rd8(r, rec + 4) == static_cast<std::uint8_t>(k)) {
            tail = rd8(r, rec + 5);
            break;
        }
    }

    int opened = 0;
    for (int k = cur; k < want; ++k) {
        const std::uintptr_t rec = sp + static_cast<std::uintptr_t>(k) * 6;
        if (!wr16(rec + 0, 0xFFFF)) break;      // 보석 없음
        if (!wr16(rec + 2, 0x0000)) break;      // 빈 칸 표시
        if (!wr8(rec + 4, static_cast<std::uint8_t>(k))) break;   // 열림
        if (!wr8(rec + 5, tail)) break;
        if (rd8(r, rec + 4) != static_cast<std::uint8_t>(k)) break;
        ++opened;
    }
    if (opened == 0) return 0;

    const std::uint8_t n = static_cast<std::uint8_t>(cur + opened);
    if (!wr8(entry + 0x70, n)) return 0;
    if (rd8(r, entry + 0x70) != n) return 0;
    return opened;
}

bool temper_set_entry(const mem::Reader& r, std::uintptr_t entry,
                      std::uint16_t lvl) {
    if (!wr16(entry + 0x0A, lvl)) return false;
    return rd16(r, entry + 0x0A) == lvl;
}

bool sharpness_set_entry(const mem::Reader& r, std::uintptr_t entry,
                         std::uint16_t lvl) {
    if (!wr16(entry + 0x58, lvl)) return false;
    return rd16(r, entry + 0x58) == lvl;
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

// 착용장비 식별용: 서로 다른 슬롯 태그 수(0..21). 진짜 착용 테이블은 슬롯마다
// 하나씩이라 조각 수와 거의 같고, 잡음/보관 배열은 훨씬 작다.
int distinct_tags(const std::vector<WornPiece>& ps) {
    bool seen[64] = {false};
    int n = 0;
    for (const auto& w : ps) {
        if (w.slot_tag < 64 && !seen[w.slot_tag]) {
            seen[w.slot_tag] = true;
            ++n;
        }
    }
    return n;
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
        // 담금질·연마·소켓 - 인벤토리 레코드와 같은 자리라 가방 쪽과 한 함수로 읽는다.
        read_level_and_sockets(reader, e, &w);
        w.slot_tag = static_cast<std::uint16_t>(rd32(reader, e + (t.stride - 8)) &
                                                0xFFFF);
        // 염색 레코드: entry+0x78 벡터, +0x80 개수(<=12), 16바이트/레코드.
        // zone 은 +6, RGB 는 +7/8/9. rec 는 both-realms 쓰기 인자로 쓴다.
        const std::uintptr_t dp = rd64(reader, e + 0x78);
        const std::uint32_t dc = rd32(reader, e + 0x80);
        if (ptr_reads(reader, dp) && dc > 0 && dc <= 12) {
            for (std::uint32_t d = 0; d < dc; ++d) {
                const std::uintptr_t a =
                    dp + static_cast<std::uintptr_t>(d) * 16;
                WornDye dy;
                dy.rec = static_cast<int>(d);
                dy.zone = rd8(reader, a + 6);
                dy.r = rd8(reader, a + 7);
                dy.g = rd8(reader, a + 8);
                dy.b = rd8(reader, a + 9);
                w.dyes.push_back(dy);
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
    // 상한을 넉넉히. 월드에 장비 컴포넌트가 128개+ 있어(NPC·동행 다수) 64 로
    // 자르면 힙 순서에 따라 플레이어 comp 가 빠져 목록이 NPC 로 샜다.
    const auto objs = rtti.find_objects("EquipSlotActorComponent", 512);
    std::vector<WornPiece> tmp;
    for (const auto& o : objs) {
        EquipTable t;
        if (!find_equip_table(reader, o.address, &t)) continue;
        t.comp = o.address;   // 액터 앵커용(comp+0x08 백참조)
        if (!read_worn_gear(reader, t, &tmp) || tmp.size() < 3) continue;
        bool dup = false;
        for (const auto& e : *out) {
            if (e.arr == t.arr) { dup = true; break; }
        }
        if (!dup) out->push_back(t);
    }
    return static_cast<int>(out->size());
}

// 여러 테이블 중 플레이어 착용 테이블을 고른다. "조각 수 최다"는 보관/잡음
// 배열에 밀려 불안정했다. 대신 **서로 다른 슬롯 태그 수**로 고른다(진짜
// 착용은 슬롯마다 하나). prefer_arr 가 여전히 좋은 후보면 그대로 유지해
// 매 주기 목록이 튀지 않게 한다.
// 테이블의 캐릭터 행. comp+0x08 이 액터 매니저가 세는 캐릭터 객체라 actor_character_row 로
// 푼다(실측 2026-09-12: 서버 조각 21/16/11 → 행 0/5/3). 못 풀면 kEquipAutoCharacter.
static std::uint16_t table_character_row(const mem::Reader& reader,
                                         const EquipTable& t) {
    if (t.comp == 0) return kEquipAutoCharacter;
    const std::uintptr_t ch =
        static_cast<std::uintptr_t>(rd64(reader, t.comp + 0x08));
    std::uint16_t row = kEquipAutoCharacter;
    if (!vp(ch) || !actor_character_row(reader, ch, &row)) return kEquipAutoCharacter;
    return row;
}

bool pick_player_table(const mem::Reader& reader,
                       const std::vector<EquipTable>& tabs,
                       std::uintptr_t prefer_arr, EquipTable* table_out,
                       std::vector<WornPiece>* pieces_out,
                       std::uint16_t want_row) {
    // 고른 캐릭터가 있으면 그 캐릭터의 테이블(서버·클라)만 후보로 삼는다. 월드에 없으면
    // 아래 자동(정신력 풀 + 조각 최다)으로 돌아간다.
    if (want_row != kEquipAutoCharacter) {
        std::vector<EquipTable> mine;
        for (const auto& t : tabs) {
            if (table_character_row(reader, t) == want_row) mine.push_back(t);
        }
        if (!mine.empty() &&
            pick_player_table(reader, mine, prefer_arr, table_out, pieces_out,
                              kEquipAutoCharacter)) {
            return true;
        }
    }
    EquipTable best;
    int bestScore = 0;
    std::vector<WornPiece> bestPieces, tmp;
    for (const auto& t : tabs) {
        if (!read_worn_gear(reader, t, &tmp)) continue;
        // 진짜 착용 테이블은 슬롯마다 하나라 서로 다른 슬롯태그가 조각 수와
        // 거의 같다. 잡음(전부 슬롯0, index 반복)은 distinct=1 이라 걸러진다.
        const int dt = distinct_tags(tmp);
        const int n = static_cast<int>(tmp.size());
        if (n == 0 || dt * 2 < n) continue;  // 태그가 조각 수의 절반 미만이면 잡음
        // 플레이어 식별: (1) 정신력 풀 보유(char_is_player) - 단 동행(companion)
        // 도 풀 게이지가 있어 참이 된다. (2) 그중 플레이어는 **착용 조각이 가장
        // 많다**(전 슬롯 착용, 실측 18 vs 동행 11). 그래서 정신력-풀 게이트를
        // 강하게 주되, 그 안에서는 조각 수(dt)로 가른다. prefer 는 **동률일 때만**
        // 깨는 +1 로 둔다(예전 +1000 은 먼저 잡힌 동행을 고정시켜 목록이 동행으로
        // 새는 원인이었다).
        int score = dt;
        if (t.comp != 0) {
            const std::uintptr_t ch = rd64(reader, t.comp + 0x08);
            if (char_is_player(reader, ch)) score += 10000;
        }
        // 동률 안정화만. 되살린 표(missed > 0)는 못 받는다 - 지역 이동으로 새 표가 잡혔는데
        // 옛 표가 되살아나 prefer 로 이기면 표시가 옛 표에 고착된다(리뷰 K-1). 생 스캔 결과가
        // tabs 앞쪽이라 동률이면 생 스캔이 이긴다.
        if (t.arr == prefer_arr && dt >= 3 && t.missed == 0) score += 1;
        if (t.stride == 0xD0) score += 1;                 // 확정 stride 우대
        if (score > bestScore) {
            bestScore = score;
            best = t;
            bestPieces = tmp;
        }
    }
    if (bestScore == 0) return false;
    if (table_out) *table_out = best;
    if (pieces_out) *pieces_out = std::move(bestPieces);
    return true;
}

bool read_player_worn(const mem::Rtti& rtti, const mem::Reader& reader,
                      EquipTable* table_out, std::vector<WornPiece>* pieces_out) {
    std::vector<EquipTable> tabs;
    collect_equip_tables(rtti, reader, &tabs);
    return pick_player_table(reader, tabs, 0, table_out, pieces_out,
                             kEquipAutoCharacter);
}

// -------------------------------------------------------------------- 캐시
namespace {
std::mutex g_eq_mutex;
// 장비 표를 **언제부터** 못 읽고 있나(0 이면 멀쩡하다). **g_eq_mutex 가 지킨다.**
// 예전 주석은 "분석 스레드 한 곳에서만 읽고 쓴다" 고 단언했는데 사실이 아니었다 -
// equip_refresh_pieces 는 분석 스레드(camera.cpp)뿐 아니라 장비 창의 렌더 스레드에서도
// 여덟 곳에서 불린다(equip_panel.cpp). 락 밖에서 읽고 쓰면 데이터 경쟁이고, 한쪽이
// 시계를 지워 재탐색이 영영 안 돌거나 멀쩡한 표를 버리게 된다.
// 지역 이동의 순간적인 실패로 재탐색을 부르지 않을 만큼 넉넉히 둔다.
std::chrono::steady_clock::time_point g_eq_dead_since;
constexpr auto kEquipDeadFor = std::chrono::seconds(10);
std::vector<EquipTable> g_eq_tables;   // both-realms 테이블(발견 캐시)
std::vector<WornPiece> g_eq_pieces;    // 플레이어 착용장비
EquipTable g_eq_player_table;           // 플레이어 테이블(빠른 재읽기용)
std::vector<EquipCharacter> g_eq_chars; // 월드의 플레이어형 캐릭터(행 오름차순)
std::uint16_t g_eq_current_row = kEquipAutoCharacter;   // 캐시된 테이블의 캐릭터
std::uint16_t g_eq_resolved_want = kEquipAutoCharacter; // 마지막 발견이 소화한 선택
std::uint16_t g_eq_log_row = kEquipAutoCharacter;       // 로그 판정용: 마지막 발견의 표시 행
bool g_eq_log_ok = false;                                // 로그 판정용: 마지막 발견 성공 여부
bool g_eq_ready = false;
std::atomic<bool> g_eq_refresh{false};
std::atomic<std::uint16_t> g_eq_want_row{kEquipAutoCharacter};   // 렌더 스레드가 고른다
}  // namespace

void equip_discover(const mem::Rtti& rtti, const mem::Reader& reader) {
    std::vector<EquipTable> tabs;
    collect_equip_tables(rtti, reader, &tabs);   // both-realms 쓰기 대상 전체
    std::uintptr_t prefer = 0;
    std::vector<EquipTable> known;
    std::vector<EquipCharacter> prev_chars;
    std::uint16_t prev_row = kEquipAutoCharacter;
    bool prev_ok = false;
    {
        std::lock_guard<std::mutex> lk(g_eq_mutex);
        prefer = g_eq_player_table.arr;   // 이전 선택 유지(동률 안정화)
        known = g_eq_tables;
        prev_chars = g_eq_chars;
        prev_row = g_eq_log_row;   // 실패도 반영된 로그용 상태(리뷰 K-3)
        prev_ok = g_eq_log_ok;
    }
    // 힙 스캔은 매번 완전하지 않다(영역 하나를 통째로 읽다 실패하면 그 영역 전부를 건너뛴다 -
    // 실측 2026-09-12: 잇단 스캔이 26/29/28/16개였고 웅카 서버 테이블이 한 번은 빠져, 첫 일괄
    // 쓰기가 한쪽 realm 에만 갔다). 전에 찾아 둔 테이블은 지금도 같은 자리에서 검증되면
    // 남긴다 - 후보 목록에서 캐릭터가 사라졌다 나타났다 하지 않고, both-realms 쓰기가
    // 양쪽을 다 찾는다. 검증: vtable 이 아직 EquipSlotActorComponent 이고(class_of_object),
    // 스캔과 같은 조건(find_equip_table 로 같은 arr·stride + 실제아이템 >= 3)을 지난다.
    // 읽기는 SEH 로 감싸여 풀린 페이지는 false 로 떨어지고, 커밋된 채 재사용된 자리는
    // vtable·구조 점수·조각 수가 거른다(리뷰 K-2·O-2). 되살린 표는 연속 kReviveMaxMisses 회를
    // 넘기면 버리고 한 번에 kReviveMax 개까지만 - 캐시가 자라지 않게(리뷰 K-4).
    constexpr int kReviveMaxMisses = 15;   // 20초 주기면 5분
    constexpr std::size_t kReviveMax = 64;
    std::size_t revived = 0;
    {
        std::vector<WornPiece> chk;
        for (const auto& k : known) {
            if (revived >= kReviveMax) break;
            bool present = false;
            for (const auto& t : tabs) {
                if (t.arr == k.arr) {
                    present = true;
                    break;
                }
            }
            if (present) continue;
            if (k.missed + 1 > kReviveMaxMisses) continue;
            if (rtti.class_of_object(k.comp).find("EquipSlotActorComponent") ==
                std::string::npos) {
                continue;
            }
            EquipTable again;
            if (!find_equip_table(reader, k.comp, &again) || again.arr != k.arr ||
                again.stride != k.stride) {
                continue;
            }
            again.comp = k.comp;
            again.missed = k.missed + 1;
            if (!read_worn_gear(reader, again, &chk) || chk.size() < 3) continue;
            tabs.push_back(again);
            ++revived;
        }
    }
    // 캐릭터 후보: 정신력 풀이 있고 착용 테이블로 보이는 것 중 행을 푼 것. realm 마다 하나씩
    // 오므로 행으로 합치고 조각 수는 큰 쪽을 둔다.
    std::vector<EquipCharacter> chars;
    std::vector<WornPiece> tmp;
    for (const auto& t : tabs) {
        if (t.comp == 0) continue;
        const std::uintptr_t ch =
            static_cast<std::uintptr_t>(rd64(reader, t.comp + 0x08));
        if (!char_is_player(reader, ch)) continue;
        if (!read_worn_gear(reader, t, &tmp)) continue;
        const int dt = distinct_tags(tmp);
        const int n = static_cast<int>(tmp.size());
        if (n == 0 || dt * 2 < n) continue;
        const std::uint16_t row = table_character_row(reader, t);
        if (row == kEquipAutoCharacter) continue;
        // 정신력 풀은 동행(companion)에도 있다 - 플레이어블(주인공·Mercenary_Main)만 후보다
        // (리뷰 E-1: 7조각짜리 동반자(행 5654)가 콤보에 들고, 고르면 치트·낙사 앵커까지
        // 그쪽으로 옮겨갔다).
        if (!is_playable_character_row(reader, row)) continue;
        EquipCharacter* found = nullptr;
        for (auto& c : chars) {
            if (c.row == row) {
                found = &c;
                break;
            }
        }
        if (found == nullptr) {
            chars.push_back(EquipCharacter{row, dt});
        } else if (dt > found->pieces) {
            found->pieces = dt;
        }
    }
    std::sort(chars.begin(), chars.end(),
              [](const EquipCharacter& a, const EquipCharacter& b) {
                  return a.row < b.row;
              });
    // 고른 캐릭터(없으면 자동 = 정신력 풀 보유 + 착용 조각 최다, pick_player_table 이 점수화).
    const std::uint16_t want = g_eq_want_row.load(std::memory_order_relaxed);
    EquipTable pt;
    std::vector<WornPiece> pieces;
    const bool ok = pick_player_table(reader, tabs, prefer, &pt, &pieces, want);
    // 가방에도 있는 장비(게임 "비활성화")는 가방 레코드 값을 보인다(equip_bag.h).
    if (ok) apply_bag_truth(reader, bag_index(reader, inventory_component()), &pieces);
    const std::uint16_t cur = ok ? table_character_row(reader, pt) : kEquipAutoCharacter;
    // 후보나 표시 캐릭터, 성공 여부가 바뀌면 한 줄 남긴다(20초 주기 재탐색은 조용히 - 실패가
    // 이어져도 한 번만) - "콤보에 웅카가 없다" 를 로그로 가릴 수 있게.
    bool changed = chars.size() != prev_chars.size() || cur != prev_row || ok != prev_ok;
    for (std::size_t i = 0; !changed && i < chars.size(); ++i) {
        changed = chars[i].row != prev_chars[i].row || chars[i].pieces != prev_chars[i].pieces;
    }
    if (changed) {
        const auto label = [](std::uint16_t row) -> std::string {
            if (row == kEquipAutoCharacter) return "자동";
            const RosterEntry* e = character_by_row(row);
            return e != nullptr && !e->display().empty() ? e->display()
                                                          : "행 " + std::to_string(row);
        };
        std::string list;
        for (const auto& c : chars) {
            if (!list.empty()) list += " ";
            list += label(c.row) + "(" + std::to_string(c.pieces) + ")";
        }
        log::infof("장비 캐릭터 후보 {}개: {} - 선택 {} → 표시 {} (테이블 {}개, 캐시에서 되살림 "
                   "{}개{})",
                   chars.size(), list, label(want), ok ? label(cur) : "없음", tabs.size(),
                   revived, ok ? "" : ", 착용 테이블 못 찾음");
    }
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    g_eq_log_row = cur;
    g_eq_log_ok = ok;
    g_eq_tables = std::move(tabs);
    g_eq_chars = std::move(chars);
    g_eq_resolved_want = want;   // 실패해도 "이 선택을 봤다" 는 남긴다(창의 갱신 중 표시)
    if (ok) {
        g_eq_player_table = pt;
        g_eq_pieces = std::move(pieces);
        g_eq_current_row = cur;
        g_eq_ready = true;
    }
}

void equip_refresh_pieces(const mem::Reader& reader) {
    EquipTable pt;
    {
        std::lock_guard<std::mutex> lk(g_eq_mutex);
        if (!g_eq_ready) return;
        pt = g_eq_player_table;
    }
    std::vector<WornPiece> pieces;
    if (!read_worn_gear(reader, pt, &pieces)) {
        // **실패를 센다.** 게임은 세이브를 불러올 때 컴포넌트를 새로 만든다(인벤토리
        // 에서 실측했다). 예전에는 여기서 조용히 돌아가기만 해서 g_eq_ready 가 참인
        // 채 죽은 표를 들고 있었고, 그러면 camera 의 `!equip_ready()` 재탐색이 영영
        // 안 돌았다. 플레이어 치트가 그것을 따라가므로 화면이 -1/-1 을 그렸다
        // (사용자 화면 확인 2026-09-13).
        //
        // 지역 이동 중에는 잠깐 못 읽을 수 있으니 **경과 시간**으로 잰다.
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lk(g_eq_mutex);
        if (g_eq_dead_since.time_since_epoch().count() == 0) {
            g_eq_dead_since = now;
            return;
        }
        if (now - g_eq_dead_since < kEquipDeadFor) return;
        g_eq_dead_since = {};
        log::warnf("장비 컴포넌트 0x{:X} 를 계속 못 읽는다 - 다시 찾는다", pt.comp);
        g_eq_ready = false;   // camera 의 !equip_ready() 가 재탐색을 돌린다
        return;
    }
    apply_bag_truth(reader, bag_index(reader, inventory_component()), &pieces);
    // 어느 스레드든 **한 번 성공하면** 표는 살아 있다 - 시계를 비운다.
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    g_eq_dead_since = {};
    g_eq_pieces = std::move(pieces);
}

std::uintptr_t equip_player_comp() {
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    return g_eq_ready ? g_eq_player_table.comp : 0;
}

std::vector<EquipCharacter> equip_characters() {
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    return g_eq_chars;
}

void equip_select_character(std::uint16_t row) {
    g_eq_want_row.store(row, std::memory_order_relaxed);
}

std::uint16_t equip_selected_character() {
    return g_eq_want_row.load(std::memory_order_relaxed);
}

std::uint16_t equip_current_character() {
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    return g_eq_ready ? g_eq_current_row : kEquipAutoCharacter;
}

std::uint16_t equip_resolved_character() {
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    return g_eq_resolved_want;
}

void equip_tables_copy(std::vector<EquipTable>* out) {
    if (out == nullptr) return;
    std::lock_guard<std::mutex> lk(g_eq_mutex);
    *out = g_eq_tables;
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

EqBagIndex eq_bag_index(const mem::Reader& reader) {
    EqBagIndex idx;
    idx.server = bag_index(reader, inventory_component());
    idx.client = bag_index(reader, inventory_component_client());
    return idx;
}

// op: 0=socket 1=temper 2=dye 3=unlock 4=sharpness
static EqWriteResult eq_write_all(const mem::Reader& reader, std::uint64_t instance,
                                  int op, int a, std::uint16_t b, std::uint8_t g,
                                  std::uint8_t bl, const EqBagIndex* bag_in) {
    std::vector<EquipTable> tabs;
    {
        std::lock_guard<std::mutex> lk(g_eq_mutex);
        tabs = g_eq_tables;
    }
    const auto apply = [&](std::uintptr_t e) {
        if (op == 0) return socket_fill_entry(reader, e, a, b);
        if (op == 1) return temper_set_entry(reader, e, b);
        if (op == 2) {
            return dye_set_entry(reader, e, a, static_cast<std::uint8_t>(b), g, bl);
        }
        if (op == 3) return socket_unlock_entry(reader, e, a) > 0;
        if (op == 4) return sharpness_set_entry(reader, e, b);
        return false;
    };
    EqWriteResult res;
    for (const auto& t : tabs) {
        const std::uintptr_t e = entry_by_instance(reader, t, instance);
        if (e != 0 && apply(e)) ++res.realms;
    }
    // 가방에도 있는 장비(게임 "비활성화")는 가방 레코드가 진짜다 - 서버·클라 인벤토리
    // 레코드에도 쓴다(2026-09-22 실측: 장비 표에만 쓴 소켓 5칸이 착용 변경 뒤 가방 레코드의
    // 2칸으로 돌아갔다). 염색은 가방 레코드의 +0x78 자리를 확인하지 않아 장비 표에만 쓴다.
    if (op != 2) {
        // 색인을 안 받았으면 이번 쓰기만을 위해 만든다(한 칸 편집이라 한 번이다).
        EqBagIndex local;
        if (bag_in == nullptr) local = eq_bag_index(reader);
        const EqBagIndex& idx = (bag_in != nullptr) ? *bag_in : local;
        for (const auto* m : {&idx.server, &idx.client}) {
            const auto it = m->find(instance);
            if (it == m->end()) continue;
            // 색인이 낡았을 수 있다(일괄 작업 도중 로드 등) - 레코드가 아직 그 인스턴스인지 본다.
            if (rd64(reader, it->second) != instance) continue;
            res.in_bag = true;
            if (apply(it->second)) ++res.bag;
        }
    }
    // 게임 메모리 쓰기는 예외 없이 남긴다. 이전값은 realm 마다 달라 안 읽는다.
    static const char* const kWhat[5] = {"장비 소켓", "장비 담금질", "장비 염색",
                                         "장비 소켓 열기", "장비 연마"};
    std::string after;
    if (op == 0) after = "칸 " + std::to_string(a) + " 보석 순번 " + std::to_string(b);
    else if (op == 1) after = "담금질 " + std::to_string(b);
    else if (op == 4) after = "연마 " + std::to_string(b);
    else if (op == 2) after = "zone 레코드 " + std::to_string(a) + " -> " +
                              std::to_string(static_cast<int>(b)) + "," +
                              std::to_string(static_cast<int>(g)) + "," +
                              std::to_string(static_cast<int>(bl));
    else after = "소켓 " + std::to_string(a) + "칸";
    after += " (" + std::to_string(res.realms) + " realm" +
             (res.bag > 0 ? ", 가방 " + std::to_string(res.bag) : std::string()) + ")";
    log_write(kWhat[op], static_cast<std::uintptr_t>(instance), "-", after);
    return res;
}

EqWriteResult eq_write_socket(const mem::Reader& reader, std::uint64_t instance, int k,
                              std::uint16_t gem, const EqBagIndex* bag) {
    return eq_write_all(reader, instance, 0, k, gem, 0, 0, bag);
}

EqWriteResult eq_write_temper(const mem::Reader& reader, std::uint64_t instance,
                              std::uint16_t level, const EqBagIndex* bag) {
    return eq_write_all(reader, instance, 1, 0, level, 0, 0, bag);
}

EqWriteResult eq_write_sharpness(const mem::Reader& reader, std::uint64_t instance,
                                 std::uint16_t level, const EqBagIndex* bag) {
    return eq_write_all(reader, instance, 4, 0, level, 0, 0, bag);
}

int eq_write_dye(const mem::Reader& reader, std::uint64_t instance, int rec,
                 std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return eq_write_all(reader, instance, 2, rec, r, g, b, nullptr).realms;
}

EqWriteResult eq_unlock_sockets(const mem::Reader& reader, std::uint64_t instance,
                                int want, const EqBagIndex* bag) {
    return eq_write_all(reader, instance, 3, want, 0, 0, 0, bag);
}

int socket_unlock_record(const mem::Reader& reader, std::uintptr_t record,
                         int want) {
    if (record == 0) return 0;
    const int before = static_cast<int>(rd8(reader, record + 0x70));
    const int opened = socket_unlock_entry(reader, record, want);
    if (opened > 0) {
        log_write("인벤 소켓 열기", record, std::to_string(before) + "칸",
                  std::to_string(before + opened) + "칸 (연 칸 " +
                      std::to_string(opened) + ")");
    } else {
        log_write("인벤 소켓 열기", record, std::to_string(before) + "칸",
                  "못 열음");
    }
    return opened;
}

}  // namespace cdtb::game
