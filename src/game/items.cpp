#include "game/items.h"

namespace cdtb::game {
namespace {

// --- 매니저 (`pa::ItemInfoManager`) ---
constexpr std::size_t kIndexPtr = 0x28;     // (u32 키, u32 용도미상) 쌍의 표
constexpr std::size_t kCountField = 0x30;   // u32 개수
constexpr std::size_t kRecordsPtr = 0x58;   // 레코드 포인터 배열

// --- 레코드 (0x500 바이트) ---
constexpr std::size_t kRecKey = 0x00;       // u32 키
constexpr std::size_t kRecNameKey = 0x28;   // u64 이름 현지화 키

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
        items.push_back(e);
    }
    *out = std::move(items);
    return true;
}

}  // namespace cdtb::game
