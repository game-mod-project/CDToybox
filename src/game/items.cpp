#include "game/items.h"

#include <atomic>

#include "core/log.h"

namespace cdtb::game {
namespace {

// --- 매니저 (`pa::ItemInfoManager`) ---
constexpr std::size_t kIndexPtr = 0x28;     // (u32 키, u32 용도미상) 쌍의 표
constexpr std::size_t kCountField = 0x30;   // u32 개수
constexpr std::size_t kRecordsPtr = 0x58;   // 레코드 포인터 배열

// --- 레코드 (0x500 바이트) ---
constexpr std::size_t kRecKey = 0x00;       // u32 키
constexpr std::size_t kRecNameKey = 0x28;   // u64 이름 현지화 키
constexpr std::size_t kRecCategory = 0xA3;  // u8  분류 (74종)
constexpr std::size_t kRecGrade = 0x210;    // u8  등급 (0=없음, 1..5)

constexpr const char* kManagerClass = ".?AVItemInfoManager@pa@@";

// 개수와 레코드 배열을 함께 읽고 앞뒤가 맞는지 본다.
bool read_header(const mem::Reader& r, std::uintptr_t manager,
                 std::uint32_t* count, std::uintptr_t* records) {
    std::uint32_t n = 0;
    std::uint64_t recs = 0;
    if (!r.read_value(manager + kCountField, &n)) return false;
    if (!r.read_value(manager + kRecordsPtr, &recs)) return false;
    // 후보를 잘못 집으면 개수가 쓰레기값이 된다. 그대로 믿고
    // 할당하면 메모리를 통째로 먹는다.
    if (n == 0 || n > kMaxItemCount || recs == 0) return false;
    *count = n;
    *records = static_cast<std::uintptr_t>(recs);
    return true;
}

}  // namespace

const char* grade_label(std::uint8_t grade) {
    switch (grade) {
        case 0: return "-";
        case 1: return "T1";
        case 2: return "T2";
        case 3: return "T3";
        case 4: return "T4";
        case 5: return "T5";
        default: return "?";
    }
}

bool looks_like_item_manager(const mem::Reader& reader,
                             std::uintptr_t manager) {
    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;

    std::uint64_t index = 0;
    if (!reader.read_value(manager + kIndexPtr, &index) || index == 0) {
        return false;
    }

    // 색인 표의 첫 키와 첫 레코드의 키가 같아야 한다. 두 배열이
    // 서로를 확인해 주므로 우연히 맞기 어렵다.
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

bool find_item_manager(const mem::Rtti& rtti, const mem::Reader& reader,
                       std::uintptr_t* out) {
    if (out == nullptr) return false;
    for (const auto addr : rtti.instances_of_class(kManagerClass, 32)) {
        if (looks_like_item_manager(reader, addr)) {
            *out = addr;
            return true;
        }
    }
    return false;
}

bool read_item_table(const mem::Reader& reader, std::uintptr_t manager,
                     std::vector<ItemEntry>* out, std::size_t max) {
    if (out == nullptr) return false;

    std::uint32_t count = 0;
    std::uintptr_t records = 0;
    if (!read_header(reader, manager, &count, &records)) return false;

    std::size_t limit = count;
    if (max != 0 && max < limit) limit = max;

    std::vector<ItemEntry> items;
    items.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i) {
        std::uint64_t record = 0;
        if (!reader.read_value(records + i * 8, &record) || record == 0) {
            continue;   // 빈 슬롯. 나머지는 계속 읽는다.
        }
        ItemEntry e;
        e.record = static_cast<std::uintptr_t>(record);
        if (!reader.read_value(e.record + kRecKey, &e.key)) continue;
        if (!reader.read_value(e.record + kRecNameKey, &e.name_key)) continue;
        // 없으면 0 으로 둔다. 등급 0 은 '등급 없음' 이라 뜻이 맞는다.
        reader.read_value(e.record + kRecGrade, &e.grade);
        reader.read_value(e.record + kRecCategory, &e.category);
        items.push_back(e);
    }
    *out = std::move(items);
    return true;
}

bool build_item_catalog(const mem::Reader& reader, std::uintptr_t manager,
                        const LocSystem& sys,
                        std::vector<ItemCatalogEntry>* out) {
    if (out == nullptr) return false;

    std::vector<ItemEntry> raw;
    if (!read_item_table(reader, manager, &raw, 0)) return false;

    const bool has_loc = sys.valid();
    std::vector<ItemCatalogEntry> catalog;
    catalog.reserve(raw.size());
    for (const auto& e : raw) {
        ItemCatalogEntry entry;
        entry.key = e.key;
        entry.name_key = e.name_key;
        entry.grade = e.grade;
        entry.category = e.category;
        if (has_loc) {
            // 못 풀려도 항목은 남긴다. 키는 있는 아이템이다.
            resolve(reader, sys, e.name_key, &entry.name, nullptr);
        }
        catalog.push_back(std::move(entry));
    }
    *out = std::move(catalog);
    return true;
}

// --------------------------------------------------- 모드용 배경 탐색

namespace {

std::vector<ItemCatalogEntry> g_catalog;
std::atomic<bool> g_ready{false};

}  // namespace

bool discover_items(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_ready.load(std::memory_order_acquire)) return true;

    // 표가 아직 안 올라왔을 수 있다. 재시도 루프에서 부르므로 못
    // 찾은 것은 로그를 남기지 않는다 - 매번 남으면 잡음이 된다.
    std::uintptr_t manager = 0;
    if (!find_item_manager(rtti, reader, &manager)) return false;

    LocSystem sys;
    if (!find_loc_system(rtti, reader, &sys)) {
        log::warnf("아이템 표: 현지화 시스템이 없다 - 이름 없이 키만 낸다");
    }

    std::vector<ItemCatalogEntry> catalog;
    if (!build_item_catalog(reader, manager, sys, &catalog)) {
        log::errorf("아이템 표: 목록을 만들지 못했다 (매니저 0x{:X})", manager);
        return false;
    }

    std::size_t named = 0;
    for (const auto& e : catalog) {
        if (!e.name.empty()) ++named;
    }

    // 목록을 먼저 채우고 나서 준비 플래그를 세운다. 그리는 쪽은
    // 플래그를 먼저 보므로 반쯤 채워진 목록을 읽지 않는다.
    g_catalog = std::move(catalog);
    g_ready.store(true, std::memory_order_release);
    log::infof("아이템 표: {}개, 이름 풀린 것 {}개", g_catalog.size(), named);
    return true;
}

bool items_ready() { return g_ready.load(std::memory_order_acquire); }

const std::vector<ItemCatalogEntry>& item_catalog() { return g_catalog; }

}  // namespace cdtb::game
