#include "game/roster.h"

#include <atomic>
#include <memory>
#include <utility>

#include "core/log.h"

namespace cdtb::game {
namespace {

// StaticInfoManager2 컨테이너 배치. 아이템 매니저와 같다.
constexpr std::size_t kCountField = 0x30;  // u32 개수
constexpr std::size_t kIndexPtr = 0x28;    // 색인 표 포인터
constexpr std::size_t kRecordsPtr = 0x58;  // 레코드 포인터 배열

// 레코드 (탈것·용병·캐릭터 공통, 실측 2026-09-04)
constexpr std::size_t kRecKey = 0x00;        // u16 키 (첫 u32 의 하위 16비트)
constexpr std::size_t kRecStringKey = 0x08;  // 엔진 문자열 객체 포인터

// 엔진 문자열 객체
constexpr std::size_t kStrData = 0x00;    // char* UTF-8
constexpr std::size_t kStrLength = 0x08;  // u32 길이
constexpr std::uint32_t kMaxNameLen = 200;

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

// 색인 항목과 레코드 첫 칸의 **u16 키**를 읽는다. 키 타입이 G(u16)
// 라, 첫 u32 의 하위 16비트가 키이고 상위 16비트는 다른 필드다 -
// u32 전체로 비교하면 어긋난다(실측).
bool read_u16_key(const mem::Reader& r, std::uintptr_t at, std::uint16_t* out) {
    std::uint32_t v = 0;
    if (!r.read_value(at, &v)) return false;
    *out = static_cast<std::uint16_t>(v & 0xFFFF);
    return true;
}

}  // namespace

std::string read_engine_string(const mem::Reader& reader,
                               std::uintptr_t string_obj) {
    if (string_obj == 0) return {};
    std::uint64_t data = 0;
    std::uint32_t len = 0;
    if (!reader.read_value(string_obj + kStrData, &data) || data == 0) return {};
    if (!reader.read_value(string_obj + kStrLength, &len)) return {};
    if (len == 0 || len > kMaxNameLen) return {};
    std::string s(len, '\0');
    if (!reader.read(static_cast<std::uintptr_t>(data), s.data(), len)) {
        return {};
    }
    return s;
}

bool looks_like_static_manager(const mem::Reader& reader,
                               std::uintptr_t manager) {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;

    std::uint64_t index = 0;
    if (!reader.read_value(manager + kIndexPtr, &index) || index == 0) {
        return false;
    }
    std::uint16_t index_key = 0;
    if (!read_u16_key(reader, static_cast<std::uintptr_t>(index), &index_key)) {
        return false;
    }
    std::uint64_t first = 0;
    if (!reader.read_value(records, &first) || first == 0) return false;
    std::uint16_t record_key = 0;
    if (!read_u16_key(reader, static_cast<std::uintptr_t>(first) + kRecKey,
                      &record_key)) {
        return false;
    }
    return index_key == record_key;
}

bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class,
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
        std::uint16_t key = 0;
        if (!read_u16_key(reader, record + kRecKey, &key)) continue;
        entry.key = key;
        std::uint64_t str_obj = 0;
        if (reader.read_value(record + kRecStringKey, &str_obj)) {
            entry.name =
                read_engine_string(reader, static_cast<std::uintptr_t>(str_obj));
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

}  // namespace

bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_ready.load(std::memory_order_acquire)) return true;

    std::vector<RosterEntry> v, c;
    // 탈것·캐릭터는 레코드 +0x00 이 키라 그대로 읽힌다. 용병은 키가
    // +0x00 이 아니고(포인터) 색인도 정렬/해시라 단순 오프셋으로 안
    // 나온다 - 키 구조 조사가 끝나야 붙인다(Tier 1b). 준비 판정은 이
    // 둘로만 한다.
    const bool ok_v = build_static_catalog(reader, rtti, kVehicleClass, &v);
    const bool ok_c = build_static_catalog(reader, rtti, kCharacterClass, &c);
    if (!ok_v || !ok_c) return false;

    const std::size_t vn = v.size(), cn = c.size();
    g_vehicle.swap(std::move(v));
    g_character.swap(std::move(c));
    g_ready.store(true, std::memory_order_release);
    log::infof("로스터: 탈것 {}개, 캐릭터 {}개 (내부 이름). 용병은 후속", vn,
               cn);
    return true;
}

bool roster_ready() { return g_ready.load(std::memory_order_acquire); }

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
