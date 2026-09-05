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

// 레코드 (탈것·캐릭터 공통, 실측 2026-09-04)
constexpr std::size_t kRecKey = 0x00;        // u16 키 (첫 u32 의 하위 16비트)
constexpr std::size_t kRecStringKey = 0x08;  // 엔진 문자열 객체 포인터

// 캐릭터 레코드의 동반자 필드 (실측 2026-09-05)
constexpr std::size_t kCharMercRow = 0xBE;     // u16 _mercenaryInfo (행 번호)
constexpr std::size_t kCharCatchable = 0x148;  // u8 _isCatchable
constexpr std::size_t kCharUnique = 0x14B;     // u8 _isUnique
constexpr std::size_t kCharHirable = 0x156;    // u8 _isHirable

// 용병 레코드 (실측 2026-09-05)
constexpr std::size_t kMercType = 0x20;  // u8 _mercenaryType

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

bool read_flag(const mem::Reader& r, std::uintptr_t at, bool* out) {
    std::uint8_t v = 0;
    if (!r.read_value(at, &v)) return false;
    *out = (v != 0);
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

bool looks_like_mercenary_manager(const mem::Reader& reader,
                                  std::uintptr_t manager) {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;
    if (count > kMaxMercenaryRows) return false;
    std::uint64_t first = 0;
    if (!reader.read_value(records, &first) || first == 0) return false;
    std::uint64_t str_obj = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(first) + kRecStringKey,
                           &str_obj) ||
        str_obj == 0) {
        return false;
    }
    return !read_engine_string(reader, static_cast<std::uintptr_t>(str_obj))
                .empty();
}

bool find_static_manager(const mem::Reader& reader, const mem::Rtti& rtti,
                         const char* manager_class, std::uintptr_t* out) {
    if (out == nullptr || manager_class == nullptr) return false;
    for (const auto addr : rtti.instances_of_class(manager_class, 32)) {
        if (looks_like_static_manager(reader, addr)) {
            *out = addr;
            return true;
        }
    }
    return false;
}

bool roster_header(const mem::Reader& reader, std::uintptr_t manager,
                   std::uint32_t* count, std::uintptr_t* records) {
    return read_header(reader, manager, count, records);
}

bool build_catalog_from_manager(const mem::Reader& reader,
                                std::uintptr_t manager, RosterKind kind,
                                std::vector<RosterEntry>* out) {
    if (out == nullptr) return false;
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
        if (kind == RosterKind::Mercenary) {
            // +0x00 이 포인터라 행 번호가 키다. 캐릭터 표 +0xBE 와 맞는다.
            entry.key = i;
            std::uint8_t t = 0;
            if (reader.read_value(record + kMercType, &t)) entry.merc_type = t;
        } else {
            std::uint16_t key = 0;
            if (!read_u16_key(reader, record + kRecKey, &key)) continue;
            entry.key = key;
        }
        std::uint64_t str_obj = 0;
        if (reader.read_value(record + kRecStringKey, &str_obj)) {
            entry.name =
                read_engine_string(reader, static_cast<std::uintptr_t>(str_obj));
        }
        if (kind == RosterKind::Character) {
            std::uint16_t row = 0xFFFF;
            if (reader.read_value(record + kCharMercRow, &row)) {
                entry.merc_row = row;
            }
            read_flag(reader, record + kCharCatchable, &entry.catchable);
            read_flag(reader, record + kCharUnique, &entry.unique);
            read_flag(reader, record + kCharHirable, &entry.hirable);
        }
        catalog.push_back(std::move(entry));
    }
    *out = std::move(catalog);
    return true;
}

bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, RosterKind kind,
                          std::vector<RosterEntry>* out) {
    if (out == nullptr || manager_class == nullptr) return false;
    std::uintptr_t manager = 0;
    if (kind == RosterKind::Mercenary) {
        for (const auto addr : rtti.instances_of_class(manager_class, 32)) {
            if (looks_like_mercenary_manager(reader, addr)) {
                manager = addr;
                break;
            }
        }
        if (manager == 0) return false;
    } else if (!find_static_manager(reader, rtti, manager_class, &manager)) {
        return false;
    }
    return build_catalog_from_manager(reader, manager, kind, out);
}

bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class,
                          std::vector<RosterEntry>* out) {
    return build_static_catalog(reader, rtti, manager_class,
                                RosterKind::Vehicle, out);
}

// --------------------------------------------------- 이름 규칙

bool roster_is_wild(const std::string& name) {
    return name.find("_Wild") != std::string::npos;
}

std::string roster_species(const std::string& name) {
    // "Animal_Lumif_Wild_32501" -> "Animal_Lumif"
    // "Animal_Ayut_Domestic_Saddle_32348" -> "Animal_Ayut"
    // "Riding_Wolf_1000" -> "Riding_Wolf"
    static const char* const kSuffixes[] = {
        "_Wild",   "_Domestic", "_Quest",  "_Saddle", "_Bagpack", "_WagonConnecter",
        "_HorseArmor", "_Trade", "_Unique", "_Scout",  "_Boss",    "_Circus",
    };
    std::string s = name;
    // 뒤의 숫자 꼬리 떼기
    while (!s.empty() && s.back() >= '0' && s.back() <= '9') s.pop_back();
    if (!s.empty() && s.back() == '_') s.pop_back();
    // 상태 접미사 이후는 전부 떼기 (가장 앞에 나오는 것 기준)
    std::size_t cut = std::string::npos;
    for (const char* suf : kSuffixes) {
        const std::size_t p = s.find(suf);
        if (p != std::string::npos && p > 0 && p < cut) cut = p;
    }
    if (cut != std::string::npos) s.resize(cut);
    while (!s.empty() && s.back() == '_') s.pop_back();
    return s;
}

// --------------------------------------------------- 모드용 배경 탐색

namespace {

constexpr const char* kVehicleClass = ".?AVVehicleInfoManager@pa@@";
constexpr const char* kMercenaryClass = ".?AVMercenaryInfoManager@pa@@";
constexpr const char* kCharacterClass = ".?AVCharacterInfoManager@pa@@";

const std::vector<RosterEntry> kEmpty;
const std::string kEmptyName;

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

    std::vector<RosterEntry> v, m, c;
    const bool ok_v = build_static_catalog(reader, rtti, kVehicleClass,
                                           RosterKind::Vehicle, &v);
    const bool ok_c = build_static_catalog(reader, rtti, kCharacterClass,
                                           RosterKind::Character, &c);
    if (!ok_v || !ok_c) return false;
    // 용병 표는 느슨한 판정이라 못 찾아도 준비로 친다. 없으면 동반자
    // 탭의 타입 이름만 비고 목록은 그려진다.
    const bool ok_m = build_static_catalog(reader, rtti, kMercenaryClass,
                                           RosterKind::Mercenary, &m);

    const std::size_t vn = v.size(), cn = c.size(), mn = m.size();
    std::size_t companions = 0;
    for (const auto& e : c) {
        if (e.is_companion()) ++companions;
    }
    g_vehicle.swap(std::move(v));
    g_character.swap(std::move(c));
    if (ok_m) g_mercenary.swap(std::move(m));
    g_ready.store(true, std::memory_order_release);
    log::infof("로스터: 탈것 {}개, 캐릭터 {}개(동반자 {}개), 용병 타입 {}개",
               vn, cn, companions, ok_m ? mn : 0);
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

const std::string& mercenary_type_name(std::uint16_t row) {
    for (const auto& e : mercenary_catalog()) {
        if (e.key == row) return e.name;
    }
    return kEmptyName;
}

std::uint8_t mercenary_type_of_row(std::uint16_t row) {
    for (const auto& e : mercenary_catalog()) {
        if (e.key == row) return e.merc_type;
    }
    return 0;
}

}  // namespace cdtb::game
