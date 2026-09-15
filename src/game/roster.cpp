#include "game/roster.h"

#include <algorithm>

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
constexpr std::size_t kCharEquipInfo = 0x6A;    // u16 _equipInfo
constexpr std::size_t kCharOwnedMerc = 0x100;   // u16 _ownedMercenaryCharacterInfo
constexpr std::size_t kCharCountAble = 0x16E;   // u8  _isMercenaryCountAble

// 용병 레코드 (실측 2026-09-05)
constexpr std::size_t kMercType = 0x20;  // u8 _mercenaryType
constexpr std::size_t kMercPlayable = 0x22;  // u8 _isPlayable

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
        entry.row = i;
        if (kind == RosterKind::Mercenary) {
            // +0x00 이 포인터라 행 번호가 키다. 캐릭터 표 +0xBE 와 맞는다.
            entry.key = i;
            std::uint8_t t = 0;
            if (reader.read_value(record + kMercType, &t)) entry.merc_type = t;
            std::uint8_t playable = 0;
            if (reader.read_value(record + kMercPlayable, &playable))
                entry.merc_playable = playable != 0;
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
            // 현지화 엔티티는 +0x00 의 u32 전체다(roster.h 설명).
            reader.read_value(record, &entry.loc_entity);
            std::uint16_t row = 0xFFFF;
            if (reader.read_value(record + kCharMercRow, &row)) {
                entry.merc_row = row;
            }
            read_flag(reader, record + kCharCatchable, &entry.catchable);
            read_flag(reader, record + kCharUnique, &entry.unique);
            read_flag(reader, record + kCharHirable, &entry.hirable);
            reader.read_value(record + kCharEquipInfo, &entry.equip_info);
            reader.read_value(record + kCharOwnedMerc, &entry.owned_merc_row);
            std::uint8_t countable = 0;
            if (reader.read_value(record + kCharCountAble, &countable))
                entry.merc_countable = countable != 0;
        }
        catalog.push_back(std::move(entry));
    }
    *out = std::move(catalog);
    return true;
}

bool build_static_catalog(const mem::Reader& reader, const mem::Rtti& rtti,
                          const char* manager_class, RosterKind kind,
                          std::vector<RosterEntry>* out,
                          std::uintptr_t* manager_out) {
    if (out == nullptr || manager_class == nullptr) return false;
    if (manager_out != nullptr) *manager_out = 0;
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
    if (manager_out != nullptr) *manager_out = manager;
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
// 행 번호 -> 캐릭터 항목 위치. 널 슬롯이 있어 항목 순번과 행 번호가 어긋난다.
std::atomic<const std::vector<const RosterEntry*>*> g_char_rows{nullptr};
std::vector<std::unique_ptr<std::vector<const RosterEntry*>>> g_char_row_versions;
std::atomic<bool> g_ready{false};
// 표시명(현지화)이 실제로 붙었는가. 현지화가 카탈로그보다 늦게 올라오면
// 첫 빌드는 표시명 0개다 - 이 플래그가 false 인 동안 다음 틱에 다시 만든다.
std::atomic<bool> g_have_labels{false};

}  // namespace

std::uint32_t roster_name_suffix(const std::string& name) {
    std::size_t end = name.size();
    std::size_t begin = end;
    while (begin > 0 && name[begin - 1] >= '0' && name[begin - 1] <= '9') {
        --begin;
    }
    if (begin == end) return 0;              // 끝이 숫자가 아니다
    if (begin == 0) return 0;                // 전부 숫자면 이름이 아니다
    if (name[begin - 1] != '_') return 0;    // "_숫자" 꼴만 본다
    if (end - begin > 9) return 0;           // u32 를 넘길 만큼 길면 버린다
    std::uint32_t v = 0;
    for (std::size_t i = begin; i < end; ++i) {
        v = v * 10 + static_cast<std::uint32_t>(name[i] - '0');
    }
    return v;
}

std::size_t apply_roster_labels(const mem::Reader& reader, const LocSystem& sys,
                               std::vector<RosterEntry>* entries) {
    if (entries == nullptr) return 0;
    std::size_t named = 0;
    for (auto& e : *entries) {
        // 레코드 키로 먼저, 안 되면 내부 이름 끝의 숫자로. 둘이
        // 일치하는 행도 많지만 어긋나는 행도 그만큼 많다.
        // 현지화 엔티티는 레코드 +0x00 의 u32 전체다(roster.h 설명).
        // 그것 하나면 된다 - 예전에 쓰던 "키로 찾고 안 되면 내부 이름
        // 끝자리 숫자로" 폴백은 걷어냈다. 그 폴백이 엉뚱한 이름을
        // 붙였다(실측 2026-09-09):
        //
        //   Animal_Baby_Wyvern_1  -> 엔티티 1     "클리프"(주인공)
        //   Animal_Wolf_Wild_30023 -> 엔티티 30023 "암탉"
        //
        // u32 로 고쳐 읽으니 각각 "새끼 와이번"·"대형 늑대" 가 나온다.
        std::uint32_t candidates[2] = {e.loc_entity, e.key};
        if (candidates[1] == candidates[0]) candidates[1] = 0;
        for (const std::uint32_t entity : candidates) {
            if (entity == 0) continue;
            std::string text;
            if (!resolve(reader, sys, loc_key(entity, kCharNameField), &text,
                         nullptr)) {
                continue;  // 없는 행이 많다. 조용히 넘긴다.
            }
            if (text.empty()) continue;
            e.label = std::move(text);
            ++named;
            break;
        }
    }
    return named;
}

bool read_spawn_table(const mem::Reader& reader, std::uintptr_t manager,
                      std::vector<std::uint32_t>* out) {
    if (out == nullptr) return false;
    out->clear();
    if (manager == 0) return false;
    std::uint32_t nonzero = 0, buckets = 0;
    std::uint64_t arr = 0;
    if (!reader.read_value(manager + 0x6C,
                           &nonzero) ||
        nonzero == 0) {
        return false;
    }
    if (!reader.read_value(manager + 0x68,
                           &buckets) ||
        buckets == 0 || buckets > kSpawnMaxBuckets) {
        return false;
    }
    if (!reader.read_value(manager + 0x78, &arr) ||
        arr == 0) {
        return false;
    }
    for (std::uint32_t b = 0; b < buckets; ++b) {
        const std::uintptr_t base =
            static_cast<std::uintptr_t>(arr) + b * kSpawnBucketStride;
        std::uint32_t n = 0;
        if (!reader.read_value(base, &n)) continue;
        if (n > kSpawnBucketMaxEntries) continue;  // 쓰레기 버킷은 건너뛴다
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t key = 0;
            if (!reader.read_value(base + 8 + static_cast<std::uintptr_t>(i) * 8,
                                   &key)) {
                break;
            }
            out->push_back(key);
        }
    }
    std::sort(out->begin(), out->end());
    out->erase(std::unique(out->begin(), out->end()), out->end());
    return !out->empty();
}

bool discover_roster(const mem::Rtti& rtti, const mem::Reader& reader) {
    // 표시명이 아직 안 붙었으면(현지화 지연) 다시 만든다. 목록 자체는 g_ready 로
    // 이미 보이지만, 라벨이 붙을 때까지 재빌드한다(아이템 표와 동일한 재시도).
    if (g_ready.load(std::memory_order_acquire) &&
        g_have_labels.load(std::memory_order_acquire)) {
        return true;
    }

    std::vector<RosterEntry> v, m, c;
    const bool ok_v = build_static_catalog(reader, rtti, kVehicleClass,
                                           RosterKind::Vehicle, &v);
    std::uintptr_t char_manager = 0;
    const bool ok_c = build_static_catalog(reader, rtti, kCharacterClass,
                                           RosterKind::Character, &c,
                                           &char_manager);
    if (!ok_v || !ok_c) return false;
    // 용병 표는 느슨한 판정이라 못 찾아도 준비로 친다. 없으면 동반자
    // 탭의 타입 이름만 비고 목록은 그려진다.
    const bool ok_m = build_static_catalog(reader, rtti, kMercenaryClass,
                                           RosterKind::Mercenary, &m);

    // 표시명. 현지화가 아직 안 올라왔으면 내부 이름만으로 간다 -
    // 목록을 못 그리는 것보다 낫다.
    LocSystem sys;
    std::size_t labeled = 0;
    const bool has_loc = find_loc_system(rtti, reader, &sys);
    if (has_loc) {
        labeled += apply_roster_labels(reader, sys, &v);
        labeled += apply_roster_labels(reader, sys, &c);
    }

    // 소환 표를 읽어 캐릭터마다 표시한다. 못 읽어도 목록은 그대로 산다.
    std::vector<std::uint32_t> spawnable;
    std::size_t spawn_marked = 0;
    if (read_spawn_table(reader, char_manager, &spawnable)) {
        for (auto& e : c) {
            if (std::binary_search(spawnable.begin(), spawnable.end(), e.key)) {
                e.spawnable = true;
                ++spawn_marked;
            }
        }
        log::infof("소환 표 {}개 - 캐릭터 {}행이 소환 가능", spawnable.size(),
                   spawn_marked);
    } else {
        log::warnf("소환 표를 못 읽었다 (CharacterInfoManager 0x{:X}) - 소환 가능 "
                   "표시가 빈다",
                   char_manager);
    }

    const std::size_t vn = v.size(), cn = c.size(), mn = m.size();
    std::size_t companions = 0;
    for (const auto& e : c) {
        if (e.is_companion()) ++companions;
    }
    g_vehicle.swap(std::move(v));
    g_character.swap(std::move(c));
    {
        const auto& chars = character_catalog();
        std::uint32_t max_row = 0;
        for (const auto& e : chars) max_row = (e.row > max_row) ? e.row : max_row;
        auto rows = std::make_unique<std::vector<const RosterEntry*>>(max_row + 1, nullptr);
        for (const auto& e : chars) (*rows)[e.row] = &e;
        const auto* p = rows.get();
        g_char_row_versions.push_back(std::move(rows));
        g_char_rows.store(p, std::memory_order_release);
    }
    if (ok_m) g_mercenary.swap(std::move(m));
    // 표시명이 하나라도 붙었거나 현지화 시스템이 준비됐으면 재시도를 멈춘다.
    // (현지화가 아직이면 has_loc=false 라 다음 틱에 다시 만든다.)
    g_have_labels.store(labeled > 0 || has_loc, std::memory_order_release);
    g_ready.store(true, std::memory_order_release);
    log::infof(
        "로스터: 탈것 {}개, 캐릭터 {}개(동반자 {}개), 용병 타입 {}개, 표시명 {}개",
        vn, cn, companions, ok_m ? mn : 0, labeled);
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

const RosterEntry* character_by_row(std::uint32_t row) {
    const auto* rows = g_char_rows.load(std::memory_order_acquire);
    if (rows == nullptr || row >= rows->size()) return nullptr;
    return (*rows)[row];
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

bool is_playable_merc_row(std::uint16_t row) {
    return mercenary_type_of_row(row) == kMercTypeMain;
}

CompanionGroup companion_group_of_row(std::uint16_t merc_row) {
    if (merc_row == 0xFFFF) return CompanionGroup::Unknown;
    switch (mercenary_type_of_row(merc_row)) {
        case 1: case 6: case 7: case 8: case 9: case 12:
            return CompanionGroup::People;
        case 2: case 3: case 4: case 5:
            return CompanionGroup::Mount;
        case 10: case 11:
            return CompanionGroup::System;
        default:
            return CompanionGroup::Unknown;
    }
}

namespace {
// 전역 하나를 사슬대로 따라가 캐릭터 행을 낸다. 못 읽으면 0xFFFF.
std::uint16_t session_char_row(const mem::Reader& reader, std::uint64_t rva) {
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return 0xFFFF;
    std::uintptr_t p = 0;
    if (!reader.read_value(base + rva, &p) || p == 0) return 0xFFFF;
    if (!reader.read_value(p, &p) || p == 0) return 0xFFFF;
    if (!reader.read_value(p + 8, &p) || p == 0) return 0xFFFF;
    if (!reader.read_value(p + 0x28, &p) || p == 0) return 0xFFFF;
    std::uint16_t row = 0xFFFF;
    if (!reader.read_value(p + kSessionCharRowOff, &row)) return 0xFFFF;
    return row;
}
}  // namespace

std::uint16_t main_character_row(const mem::Reader& reader) {
    const std::size_t n = character_catalog().size();
    for (std::uint64_t rva : {kSessionGlobalRvaA, kSessionGlobalRvaB}) {
        const std::uint16_t row = session_char_row(reader, rva);
        // 표 안의 행이어야 진짜다. 아니면 다른 전역을 본다.
        if (row != 0xFFFF && (n == 0 || row < n)) return row;
    }
    return 0xFFFF;
}

bool is_playable_character_row(const mem::Reader& reader, std::uint32_t row) {
    if (row == 0xFFFF) return false;
    if (row == kProtagonistRow || row == main_character_row(reader)) return true;
    const RosterEntry* e = character_by_row(row);
    return e != nullptr && is_playable_merc_row(e->merc_row);
}


std::vector<std::string> roster_scan_classes() {
    return {kVehicleClass, kMercenaryClass, kCharacterClass};
}

}  // namespace cdtb::game
