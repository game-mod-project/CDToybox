#include "game/roster.h"

#include <atomic>
#include <memory>
#include <utility>

#include "core/log.h"

namespace cdtb::game {
namespace {

// StaticInfoManager2 컨테이너 배치. 아이템 매니저와 같다.
constexpr std::size_t kIndexPtr = 0x28;    // (u32 키, u32 …) 쌍의 표
constexpr std::size_t kCountField = 0x30;  // u32 개수
constexpr std::size_t kRecordsPtr = 0x58;  // 레코드 포인터 배열

// 레코드 (탈것·용병·캐릭터 공통, fields.py 로 확인)
constexpr std::size_t kRecKey = 0x00;      // u32 키
constexpr std::size_t kRecStringKey = 0x08;  // u64 _stringKey (이름)

bool read_header(const mem::Reader& r, std::uintptr_t manager,
                 std::uint32_t* count, std::uintptr_t* records) {
    std::uint32_t n = 0;
    std::uint64_t recs = 0;
    if (!r.read_value(manager + kCountField, &n)) return false;
    if (!r.read_value(manager + kRecordsPtr, &recs)) return false;
    if (n == 0 || n > kMaxRosterCount || recs == 0) return false;
    *count = n;
    *records = static_cast<std::uintptr_t>(recs);
    return true;
}

}  // namespace

bool looks_like_static_manager(const mem::Reader& reader,
                               std::uintptr_t manager) {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;

    std::uint64_t index = 0;
    if (!reader.read_value(manager + kIndexPtr, &index) || index == 0) {
        return false;
    }
    std::uint32_t index_key = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(index), &index_key)) {
        return false;
    }
    std::uint64_t first = 0;
    if (!reader.read_value(records, &first) || first == 0) return false;
    std::uint32_t record_key = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(first) + kRecKey,
                           &record_key)) {
        return false;
    }
    return index_key == record_key;
}

bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, const LocSystem& sys,
                          std::vector<RosterEntry>* out) {
    if (out == nullptr || manager_class == nullptr) return false;

    std::uintptr_t manager = 0;
    for (const auto addr : rtti.instances_of_class(manager_class, 32)) {
        if (looks_like_static_manager(reader, addr)) {
            manager = addr;
            break;
        }
    }
    if (manager == 0) return false;

    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;

    const bool has_loc = sys.valid();
    std::vector<RosterEntry> catalog;
    catalog.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint64_t rec = 0;
        if (!reader.read_value(records + static_cast<std::uintptr_t>(i) * 8,
                               &rec) ||
            rec == 0) {
            continue;  // 널 슬롯은 건너뛴다
        }
        const auto record = static_cast<std::uintptr_t>(rec);
        RosterEntry entry;
        if (!reader.read_value(record + kRecKey, &entry.key)) continue;
        reader.read_value(record + kRecStringKey, &entry.name_key);
        if (has_loc && entry.name_key != 0) {
            // 못 풀려도 항목은 남긴다 - 키는 있는 것이다.
            resolve(reader, sys, entry.name_key, &entry.name, nullptr);
        }
        catalog.push_back(std::move(entry));
    }
    *out = std::move(catalog);
    return true;
}

// --------------------------------------------------- 모드용 배경 탐색

namespace {

constexpr const char* kVehicleClass = ".?AVVehicleInfoManager@pa@@";
constexpr const char* kMercenaryClass = ".?AVMercenaryInfoManager@pa@@";
constexpr const char* kCharacterClass = ".?AVCharacterInfoManager@pa@@";

const std::vector<RosterEntry> kEmpty;

// 아이템 목록과 같은 방식 - 그리는 쪽이 참조를 쥔 채 프레임을 도므로
// 옛 판을 살려 두고 포인터만 바꿔 낀다.
struct Cache {
    std::atomic<const std::vector<RosterEntry>*> live{&kEmpty};
    std::vector<std::unique_ptr<std::vector<RosterEntry>>> versions;
    void swap(std::vector<RosterEntry>&& built) {
        auto held = std::make_unique<std::vector<RosterEntry>>(std::move(built));
        const std::vector<RosterEntry>* p = held.get();
        versions.push_back(std::move(held));
        live.store(p, std::memory_order_release);
    }
};

Cache g_vehicle;
Cache g_mercenary;
Cache g_character;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_named{false};

}  // namespace

bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader) {
    LocSystem sys;
    const bool loc_ok = find_loc_system(rtti, reader, &sys) && sys.valid();

    // 이미 이름까지 풀렸으면 다시 만들지 않는다.
    if (g_ready.load(std::memory_order_acquire) &&
        g_named.load(std::memory_order_acquire)) {
        return true;
    }

    std::vector<RosterEntry> v, m, c;
    const bool ok_v = build_static_catalog(reader, rtti, kVehicleClass, sys, &v);
    const bool ok_m =
        build_static_catalog(reader, rtti, kMercenaryClass, sys, &m);
    const bool ok_c =
        build_static_catalog(reader, rtti, kCharacterClass, sys, &c);
    // 셋 다 올라와야 준비로 본다. 하나라도 아직이면 재시도.
    if (!ok_v || !ok_m || !ok_c) return false;

    const std::size_t vn = v.size(), mn = m.size(), cn = c.size();
    g_vehicle.swap(std::move(v));
    g_mercenary.swap(std::move(m));
    g_character.swap(std::move(c));
    g_ready.store(true, std::memory_order_release);
    if (loc_ok && !g_named.load(std::memory_order_acquire)) {
        g_named.store(true, std::memory_order_release);
        log::infof("로스터: 탈것 {}개, 용병 {}개, 캐릭터 {}개 (이름 풀림)", vn,
                   mn, cn);
    } else if (!loc_ok) {
        log::infof("로스터: 탈것 {}개, 용병 {}개, 캐릭터 {}개 (이름 대기)", vn,
                   mn, cn);
    }
    return true;
}

bool roster_ready() { return g_ready.load(std::memory_order_acquire); }
bool roster_named() { return g_named.load(std::memory_order_acquire); }

const std::vector<RosterEntry>& vehicle_catalog() {
    return *g_vehicle.live.load(std::memory_order_acquire);
}
const std::vector<RosterEntry>& mercenary_catalog() {
    return *g_mercenary.live.load(std::memory_order_acquire);
}
const std::vector<RosterEntry>& character_catalog() {
    return *g_character.live.load(std::memory_order_acquire);
}

}  // namespace cdtb::game
