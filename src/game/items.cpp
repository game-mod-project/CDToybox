#include "game/items.h"

#include <atomic>
#include <cstring>
#include <memory>

#include "core/log.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// --- 매니저 (`pa::ItemInfoManager`) ---
constexpr std::size_t kIndexPtr = 0x28;     // (u32 키, u32 용도미상) 쌍의 표
constexpr std::size_t kCountField = 0x30;   // u32 개수
constexpr std::size_t kRecordsPtr = 0x58;   // 레코드 포인터 배열

// --- 레코드 (0x500 바이트) ---
constexpr std::size_t kRecKey = 0x00;       // u32 키
constexpr std::size_t kRecMaxStack = 0x18;  // u32 최대 스택
constexpr std::size_t kRecNameKey = 0x28;   // u64 이름 현지화 키
constexpr std::size_t kRecCategory = 0xA3;  // u8  분류 (74종)
constexpr std::size_t kRecGrade = 0x210;    // u8  등급 (0=없음, 1..5)
constexpr std::size_t kRecSockets = 0x238;    // u32 소켓 칸 수
constexpr std::size_t kRecTemperCap = 0x250;  // u32 담금질 상한+1

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
        reader.read_value(e.record + kRecMaxStack, &e.max_stack);
        reader.read_value(e.record + kRecCategory, &e.category);

        // 게임은 담금질을 "상한 - 1" 까지만 받는다. 상한이 0 인
        // 아이템(재료 등)은 그대로 빼면 0xFFFFFFFF 가 되므로 0 으로
        // 둔다 - 담금질이 없는 것이다.
        std::uint32_t cap = 0;
        reader.read_value(e.record + kRecTemperCap, &cap);
        e.max_temper = (cap == 0) ? 0 : cap - 1;

        // 담금질과 달리 상한 그대로다 - 게임이 `>=` 로 검사한다.
        reader.read_value(e.record + kRecSockets, &e.max_sockets);

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
        entry.max_stack = e.max_stack;
        entry.max_temper = e.max_temper;
        entry.max_sockets = e.max_sockets;
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

const std::vector<ItemCatalogEntry> kEmptyCatalog;

// 목록은 바꿔 끼우기만 한다. 그리는 쪽이 참조를 쥔 채로 프레임을
// 도는데 그 밑에서 vector 를 갈아엎으면 죽는다. 옛 판은 그대로
// 살려 둔다 - 많아야 두 판이다.
std::atomic<const std::vector<ItemCatalogEntry>*> g_catalog{&kEmptyCatalog};
std::vector<std::unique_ptr<std::vector<ItemCatalogEntry>>> g_versions;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_named{false};

}  // namespace

bool should_rebuild_catalog(bool have_catalog, bool names_resolved,
                            bool loc_available) {
    if (!have_catalog) return true;
    if (names_resolved) return false;
    // 현지화가 아직 없으면 다시 만들어도 결과가 같다.
    return loc_available;
}

bool discover_items(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_named.load(std::memory_order_acquire)) return true;

    // 표가 아직 안 올라왔을 수 있다. 재시도 루프에서 부르므로 못
    // 찾은 것은 로그를 남기지 않는다 - 매번 남으면 잡음이 된다.
    std::uintptr_t manager = 0;
    if (!find_item_manager(rtti, reader, &manager)) return false;

    LocSystem sys;
    const bool has_loc = find_loc_system(rtti, reader, &sys);
    const bool have = g_ready.load(std::memory_order_acquire);
    if (!should_rebuild_catalog(have, false, has_loc)) return false;

    auto built = std::make_unique<std::vector<ItemCatalogEntry>>();
    if (!build_item_catalog(reader, manager, sys, built.get())) {
        log::errorf("아이템 표: 목록을 만들지 못했다 (매니저 0x{:X})", manager);
        return false;
    }

    std::size_t named = 0;
    for (const auto& e : *built) {
        if (!e.name.empty()) ++named;
    }

    // 목록을 먼저 채우고 나서 준비 플래그를 세운다. 그리는 쪽은
    // 플래그를 먼저 보므로 반쯤 채워진 목록을 읽지 않는다.
    const std::size_t total = built->size();
    const auto* p = built.get();
    g_versions.push_back(std::move(built));
    g_catalog.store(p, std::memory_order_release);
    g_ready.store(true, std::memory_order_release);
    if (named > 0) g_named.store(true, std::memory_order_release);
    log::infof("아이템 표: {}개, 이름 풀린 것 {}개{}", total, named,
               named == 0 ? " - 현지화를 기다렸다 다시 만든다" : "");
    return named > 0;
}

bool items_ready() { return g_ready.load(std::memory_order_acquire); }

bool items_named() { return g_named.load(std::memory_order_acquire); }

const std::vector<ItemCatalogEntry>& item_catalog() {
    return *g_catalog.load(std::memory_order_acquire);
}

// ------------------------------------- 아이템 키 <-> 짧은 식별자 대응표

namespace {

// --- 표 (전역이 가리키는 객체의 +0x68) ---
constexpr std::size_t kMapAtObject = 0x68;
constexpr std::size_t kMapCountField = 0x04;      // u32 개수
constexpr std::size_t kMapCapacityField = 0x08;   // u32 해시 용량
constexpr std::size_t kMapRecCountField = 0x0C;   // u32 레코드 개수
constexpr std::size_t kMapSlotsPtr = 0x10;        // 해시 슬롯 배열
constexpr std::size_t kMapRecordsPtr = 0x18;      // 레코드 포인터 배열

// --- 레코드 (16바이트) ---
// +0x00 의 u32 는 순번이 아니다(실측: 순번 5915 의 레코드가 946).
// 무엇인지 모르므로 읽지 않는다.
constexpr std::size_t kRecItemKey = 0x04;  // u32 아이템 키

// 변환 함수 본문 40바이트. ?? 는 disp32 두 개다.
//
//   8B 44 24 30           mov   eax,[rsp+0x30]      스트림에서 읽은 키
//   48 8D 54 24 40        lea   rdx,[rsp+0x40]
//   48 8B 0D ?? ?? ?? ??  mov   rcx,[rip+disp]      <- 전역
//   48 83 C1 68           add   rcx,0x68            표는 객체의 +0x68
//   89 44 24 40           mov   [rsp+0x40],eax
//   E8 ?? ?? ?? ??        call  조회
//   48 85 C0              test  rax,rax
//   74 0E                 jz    없음
//   0F B7 00              movzx eax,word ptr [rax]  레코드의 첫 u16
//   66 89 03              mov   [rbx],ax            그것이 짧은 식별자
constexpr const char* kConvertBodyPattern =
    "8B 44 24 30 48 8D 54 24 40 48 8B 0D ?? ?? ?? ?? 48 83 C1 68 "
    "89 44 24 40 E8 ?? ?? ?? ?? 48 85 C0 74 0E 0F B7 00 66 89 03";

// 패턴 안에서 전역을 가리키는 disp32 의 위치와, 그 명령의 끝.
constexpr std::size_t kMapGlobalDispAt = 12;
constexpr std::size_t kMapGlobalInsnEnd = 16;

}  // namespace

std::vector<std::uint64_t> find_item_key_map_rvas(
    const std::vector<std::uint8_t>& image, std::size_t max) {
    std::vector<std::uint64_t> out;
    if (image.size() < kMapGlobalInsnEnd) return out;

    const auto pattern = mem::parse_pattern(kConvertBodyPattern);
    if (!pattern) return out;

    const mem::Range range{image.data(), image.size()};
    for (const auto* hit : mem::find_all(range, *pattern, max)) {
        const std::size_t off = static_cast<std::size_t>(hit - image.data());
        std::int32_t disp = 0;
        std::memcpy(&disp, image.data() + off + kMapGlobalDispAt, sizeof(disp));

        const std::int64_t rva =
            static_cast<std::int64_t>(off + kMapGlobalInsnEnd) + disp;
        // 이미지 밖을 가리키면 후보가 아니다.
        if (rva < 0 || static_cast<std::uint64_t>(rva) + 8 > image.size()) {
            continue;
        }
        out.push_back(static_cast<std::uint64_t>(rva));
    }
    return out;
}

namespace {

// 후보 하나를 따라가 표의 앞뒤가 맞는지 본다.
bool read_candidate(const mem::Reader& reader, std::uint64_t rva,
                    ItemKeyMap* out) {
    ItemKeyMap m;
    m.global = reader.module_base() + static_cast<std::uintptr_t>(rva);

    // 전역은 실행 중에 채워진다. 캐시한 이미지가 아니라 실제
    // 메모리에서 읽는다. 비어 있으면 표가 아직 없는 것이다.
    std::uint64_t object = 0;
    if (!reader.read_value(m.global, &object) || object == 0) return false;
    m.object = static_cast<std::uintptr_t>(object);
    m.table = m.object + kMapAtObject;

    std::uint64_t slots = 0, records = 0;
    if (!reader.read_value(m.table + kMapCountField, &m.count)) return false;
    if (!reader.read_value(m.table + kMapCapacityField, &m.capacity)) {
        return false;
    }
    if (!reader.read_value(m.table + kMapRecCountField, &m.record_count)) {
        return false;
    }
    if (!reader.read_value(m.table + kMapSlotsPtr, &slots)) return false;
    if (!reader.read_value(m.table + kMapRecordsPtr, &records)) return false;

    // 잘못 집으면 용량이 쓰레기값이 된다. 그대로 믿고 할당하면
    // 메모리를 통째로 먹는다. 실측은 6,810 / 8,088 이다.
    if (m.capacity == 0 || m.capacity > kMaxItemCount) return false;
    if (m.count == 0 || m.count > m.capacity) return false;
    if (m.record_count == 0 || m.record_count > kMaxItemCount) return false;
    if (slots == 0 || records == 0) return false;

    m.slots = static_cast<std::uintptr_t>(slots);
    m.records = static_cast<std::uintptr_t>(records);
    *out = m;
    return true;
}

// 후보 상한. 실측 34곳이라 넉넉하다.
constexpr std::size_t kMaxCandidates = 512;

}  // namespace

bool find_item_key_map(const mem::Reader& reader,
                       const std::vector<std::uint8_t>& image,
                       std::uint32_t expected_count, ItemKeyMap* out) {
    if (out == nullptr || expected_count == 0) return false;

    ItemKeyMap found;
    std::size_t hits = 0;
    for (const auto rva : find_item_key_map_rvas(image, kMaxCandidates)) {
        ItemKeyMap m;
        if (!read_candidate(reader, rva, &m)) continue;
        if (m.count != expected_count) continue;
        // 둘 이상이면 고를 수 없다. 첫 번째로 만족하고 끝내지 않는다.
        if (++hits > 1) return false;
        found = m;
    }
    if (hits != 1) return false;
    *out = found;
    return true;
}

bool read_item_key_map(const mem::Reader& reader, const ItemKeyMap& map,
                       std::vector<ItemKeyPair>* out) {
    if (out == nullptr || map.records == 0) return false;
    if (map.record_count == 0 || map.record_count > kMaxItemCount) return false;

    std::vector<std::uint64_t> ptrs(map.record_count, 0);
    if (!reader.read(map.records, ptrs.data(), ptrs.size() * 8)) return false;

    std::vector<ItemKeyPair> pairs;
    pairs.reserve(map.record_count);
    for (std::size_t i = 0; i < ptrs.size(); ++i) {
        if (ptrs[i] == 0) continue;   // 널 슬롯. 나머지는 계속 읽는다.
        const auto rec = static_cast<std::uintptr_t>(ptrs[i]);

        std::uint32_t key = 0;
        if (!reader.read_value(rec + kRecItemKey, &key)) continue;

        ItemKeyPair p;
        p.key = key;
        // 순번이 곧 인벤토리가 저장하는 값이다. 건너뛴 칸이 있어도
        // 앞으로 당기지 않는다 - 당기면 뒤가 통째로 어긋난다.
        p.id = static_cast<std::uint32_t>(i);
        pairs.push_back(p);
    }
    *out = std::move(pairs);
    return true;
}

}  // namespace cdtb::game
