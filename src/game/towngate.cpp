#include "game/towngate.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/clan.h"
#include "game/reserveslot.h"   // kActorSub (액터 -> 컴포넌트 홀더, +0x68)
#include "game/roster.h"
#include "mem/rtti.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

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

TownPatch g_bak[kTownPatchMax];
int g_bak_n = 0;

// 확인된 매니저. 배치가 깨지면 버리고 다시 찾는다.
std::uintptr_t g_mgr = 0;

// 이 객체가 정말 `RegionInfoManager` 인가.
//
// **클래스 이름 대조를 뺄 수 없다.** 매니저 전역들이 한 구역에 줄지어 있고
// (지식 매니저가 바로 0x18 앞이다), 표 배치 검사만으로는 이웃 매니저도
// 통과한다. 그 상태로 쓰면 `KnowledgeInfo` 레코드의 +0x74/+0x75 를 뭉갠다.
bool manager_is_region(const mem::Reader& r, const mem::Rtti& rtti,
                       std::uintptr_t mgr) {
    if (mgr < 0x10000) return false;
    const std::string cls = rtti.class_of_object(mgr);
    if (cls != std::string(".?AV") + kRegionMgrClass + "@pa@@") return false;
    if (!looks_like_static_manager(r, mgr)) return false;
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!roster_header(r, mgr, &n, &recs)) return false;
    return region_count_plausible(n) && recs >= 0x10000;
}

// 매니저를 찾는다. 싼 길(고정 전역) 먼저, 안 맞으면 RTTI 인스턴스 탐색.
// 인스턴스 탐색은 힙 전수라 비싸므로 **고정 전역이 틀렸을 때만** 간다.
std::uintptr_t region_manager(const mem::Reader& r) {
    const mem::Rtti* rtti = clan_rtti();
    if (rtti == nullptr) return 0;

    if (g_mgr != 0 && manager_is_region(r, *rtti, g_mgr)) return g_mgr;
    g_mgr = 0;

    const auto cand = static_cast<std::uintptr_t>(
        rd64(r, r.module_base() + kRegionMgrGlobalRva));
    if (manager_is_region(r, *rtti, cand)) {
        g_mgr = cand;
        return g_mgr;
    }

    std::uintptr_t found = 0;
    if (find_static_manager(r, *rtti, kRegionMgrClass, &found) &&
        manager_is_region(r, *rtti, found)) {
        log::infof("구역 표: 고정 전역(+0x{:X})이 안 맞아 RTTI 로 찾았다 0x{:X}",
                   kRegionMgrGlobalRva, found);
        g_mgr = found;
        return g_mgr;
    }
    return 0;
}

std::uintptr_t record_at(const mem::Reader& r, std::uintptr_t recs,
                         std::uint32_t row) {
    const auto rec = static_cast<std::uintptr_t>(
        rd64(r, recs + static_cast<std::uintptr_t>(row) * 8));
    return rec >= 0x10000 ? rec : 0;
}

// 액터가 지금 겹쳐 들어와 있는 구역의 **행 번호**를 모은다.
int here_rows(const mem::Reader& r, std::uintptr_t actor, std::uint16_t* out,
              int max) {
    if (actor < 0x10000 || out == nullptr) return 0;
    const auto holder = static_cast<std::uintptr_t>(rd64(r, actor + kActorSub));
    if (holder < 0x10000) return 0;
    const auto state =
        static_cast<std::uintptr_t>(rd64(r, holder + kHolderRegionState));
    if (state < 0x10000) return 0;
    const auto list =
        static_cast<std::uintptr_t>(rd64(r, state + kRegionStateList));
    if (list < 0x10000) return 0;
    const auto begin = static_cast<std::uintptr_t>(rd64(r, list + kListBegin));
    const std::uint32_t n = rd32(r, list + kListCount);
    if (begin < 0x10000 || n == 0 || n > static_cast<std::uint32_t>(max)) {
        return 0;
    }
    int got = 0;
    for (std::uint32_t i = 0; i < n && got < max; ++i) {
        out[got++] = rd16(r, begin + static_cast<std::uintptr_t>(i) * kListStride);
    }
    return got;
}

}  // namespace

// --------------------------------------------------------- 순수 부분

bool town_row_gated(std::uint8_t is_town, std::uint8_t limit_vehicle_run) {
    return is_town != 0 || limit_vehicle_run != 0;
}

int town_row_plan(std::uint16_t row, std::uint8_t is_town,
                  std::uint8_t limit_vehicle_run, TownPatch out[2]) {
    if (out == nullptr) return 0;
    int n = 0;
    if (is_town != 0) {
        out[n++] = TownPatch{row, static_cast<std::uint8_t>(kRiIsTown), is_town};
    }
    if (limit_vehicle_run != 0) {
        out[n++] = TownPatch{row, static_cast<std::uint8_t>(kRiLimitVehicleRun),
                             limit_vehicle_run};
    }
    return n;
}

bool region_count_plausible(std::uint32_t count) {
    return count > 0 && count <= static_cast<std::uint32_t>(kRegionMaxRows);
}

bool region_row_in_table(std::uint32_t row, std::uint32_t count) {
    return row < count;
}

// --------------------------------------------------------- 읽기

TownGateState town_gate_state(const mem::Reader& reader,
                              std::uintptr_t player_actor) {
    TownGateState s;
    s.on = g_bak_n > 0;

    const std::uintptr_t mgr = region_manager(reader);
    if (mgr == 0) {
        std::snprintf(s.note, sizeof s.note, "%s",
                      clan_rtti() == nullptr
                          ? "RTTI 색인을 기다립니다 (월드 진입 전)"
                          : "구역 표를 못 찾았습니다 (게임이 갱신됐을 수 있습니다)");
        return s;
    }
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!roster_header(reader, mgr, &n, &recs)) {
        std::snprintf(s.note, sizeof s.note, "%s", "구역 표 머리를 못 읽었습니다");
        return s;
    }
    s.ready = true;
    s.rows = static_cast<int>(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uintptr_t rec = record_at(reader, recs, i);
        if (rec == 0) continue;
        if (rd8(reader, rec + kRiIsTown) != 0) ++s.towns;
        if (rd8(reader, rec + kRiLimitVehicleRun) != 0) ++s.runlimits;
    }

    if (player_actor < 0x10000) return s;

    std::uint16_t rows[kRegionHereMax] = {};
    s.here_rows = here_rows(reader, player_actor, rows, kRegionHereMax);
    for (int i = 0; i < s.here_rows; ++i) {
        if (!region_row_in_table(rows[i], n)) continue;
        const std::uintptr_t rec = record_at(reader, recs, rows[i]);
        if (rec == 0 || rd8(reader, rec + kRiIsTown) == 0) continue;
        s.here_town = true;
        const std::string name = read_engine_string(
            reader, static_cast<std::uintptr_t>(rd64(reader, rec + kRiStringKey)));
        std::snprintf(s.here_name, sizeof s.here_name, "%s", name.c_str());
        break;
    }

    // (2)번 갈래. 실측에선 늘 0 이었지만 화면에 같이 보여 준다 - 0 이 아닌
    // 자리를 만나면 그때 알아야 한다.
    const auto holder =
        static_cast<std::uintptr_t>(rd64(reader, player_actor + kActorSub));
    const auto comp =
        holder >= 0x10000
            ? static_cast<std::uintptr_t>(rd64(reader, holder + kHolderTownComp))
            : 0;
    if (comp >= 0x10000) {
        s.town_counter = static_cast<int>(rd32(reader, comp + kTownCounter));
        if (s.town_counter != 0) s.here_town = true;
    }
    return s;
}

// --------------------------------------------------------- 쓰기

bool town_gate_free(const mem::Reader& reader, bool on) {
    if (!on) {
        town_gate_teardown();
        return true;
    }
    if (g_bak_n > 0) return true;

    const std::uintptr_t mgr = region_manager(reader);
    if (mgr == 0) return false;
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!roster_header(reader, mgr, &n, &recs)) return false;
    if (!region_count_plausible(n)) return false;

    int done = 0;
    bool capped = false;
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uintptr_t rec = record_at(reader, recs, i);
        if (rec == 0) continue;
        const std::uint8_t town = rd8(reader, rec + kRiIsTown);
        const std::uint8_t run = rd8(reader, rec + kRiLimitVehicleRun);
        if (!town_row_gated(town, run)) continue;
        TownPatch plan[2];
        const int pn =
            town_row_plan(static_cast<std::uint16_t>(i), town, run, plan);
        for (int k = 0; k < pn; ++k) {
            if (g_bak_n >= kTownPatchMax) {
                capped = true;
                break;
            }
            // 원본을 먼저 적는다 - 쓰다 실패해도 되돌릴 수 있어야 한다.
            g_bak[g_bak_n++] = plan[k];
            const std::uint8_t zero = 0;
            if (mem::safe_write_bytes(rec + plan[k].off, &zero, sizeof zero)) {
                ++done;
            }
        }
        if (capped) break;
    }
    if (done == 0) {
        town_gate_teardown();
        log::warnf("마을 판정: 한 건도 못 썼다 - 표를 그대로 둔다");
        return false;
    }
    if (capped) {
        log::warnf("마을 판정: 자국 상한 {}개에 걸렸다 - 남은 행은 그대로다",
                   kTownPatchMax);
    }
    log::infof("마을 판정: {}칸을 풀었다 (_isTown·_limitVehicleRun -> 0)", done);
    return true;
}

void town_gate_teardown() {
    if (g_bak_n == 0) return;
    const mem::LocalReader reader;
    const std::uintptr_t mgr = region_manager(reader);
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    const bool have = mgr != 0 && roster_header(reader, mgr, &n, &recs);

    int back = 0, skipped = 0;
    for (int i = g_bak_n - 1; i >= 0; --i) {
        const TownPatch& b = g_bak[i];
        if (!have || !region_row_in_table(b.row, n)) {
            ++skipped;
            continue;
        }
        const std::uintptr_t rec = record_at(reader, recs, b.row);
        if (rec == 0) {
            ++skipped;
            continue;
        }
        // 지금 값이 우리가 쓴 0 이 아니면 남이 바꾼 것이다 - 덮지 않는다.
        if (rd8(reader, rec + b.off) != 0) {
            ++skipped;
            continue;
        }
        if (mem::safe_write_bytes(rec + b.off, &b.old, sizeof b.old)) ++back;
    }
    g_bak_n = 0;
    if (skipped > 0) {
        log::warnf("마을 판정 되돌리기: {}칸 복구, {}칸은 건너뛰었다", back,
                   skipped);
    } else {
        log::infof("마을 판정 되돌리기: {}칸 복구", back);
    }
}

}  // namespace cdtb::game
