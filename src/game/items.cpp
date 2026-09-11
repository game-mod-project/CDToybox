#include "game/items.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>

#include "core/log.h"
#include "core/write_log.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// --- 매니저 (`pa::ItemInfoManager`) ---
constexpr std::size_t kIndexPtr = 0x28;     // (u32 키, u32 용도미상) 쌍의 표
constexpr std::size_t kCountField = 0x30;   // u32 개수
constexpr std::size_t kRecordsPtr = 0x58;   // 레코드 포인터 배열

// --- 레코드 (0x500 바이트) ---
//
// 이름은 게임이 스스로 알려 준다. 역직렬화 함수의 실패 메시지가
// "ItemInfo의 _maxEndurance를 읽어들이는데 실패했다" 꼴이라
// 필드 이름과 오프셋을 짝지을 수 있다 - `tools/rtti/fields.py` 가
// 그것을 뽑는다. 실측 109개.
constexpr std::size_t kRecKey = 0x00;       // u32 키
constexpr std::size_t kRecMaxStack = 0x18;  // u32 _maxStackCount
constexpr std::size_t kRecNameKey = 0x28;   // u64 이름 현지화 키
constexpr std::size_t kRecEquipType = 0x42;  // u16 _equipTypeInfo (FFFF=장비 아님)
constexpr std::size_t kRecCategory = 0xA3;  // u8  _itemType (74종)
constexpr std::size_t kRecGrade = 0x210;    // u8  _itemTier (0=없음, 1..5)
constexpr std::size_t kRecSockets = 0x238;    // u32 소켓 칸 수 (이름 없음)
constexpr std::size_t kRecSharpness = 0x2E8;  // i16 _SharpnessData 의 상한
// _enchantDataList 는 {ptr +0x248, u32 개수 +0x250} 다. 담금질은
// 0..개수-1 이라 게임이 `담금질 <= [+0x250] - 1` 로 검사한다.
constexpr std::size_t kRecTemperCap = 0x250;  // u32 담금질 상한+1
constexpr std::size_t kRecMaxEndurance = 0x400;  // u16 _maxEndurance
// _repairDataList 는 {ptr +0x408, u32 개수 +0x410} 다.
constexpr std::size_t kRecRepairCount = 0x410;   // u32

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

        // 소켓 갈래를 가르는 값. 못 읽으면 0xFFFF(=장비 아님)로 두어
        // 소켓을 안 싣는 쪽으로 기운다.
        if (!reader.read_value(e.record + kRecEquipType, &e.equip_type)) {
            e.equip_type = 0xFFFF;
        }

        // 0xFFFF 면 내구도가 없는 아이템이다 - 담금질의 _equipTypeInfo
        // (+0x42) 와 같은 표기법이다.
        reader.read_value(e.record + kRecMaxEndurance, &e.max_endurance);
        reader.read_value(e.record + kRecRepairCount, &e.repair_entries);
        reader.read_value(e.record + kRecSharpness, &e.max_sharpness);

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
        entry.max_endurance = e.max_endurance;
        entry.repair_entries = e.repair_entries;
        entry.max_sharpness = e.max_sharpness;
        entry.equip_type = e.equip_type;
        entry.record = e.record;
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
// 소켓 상한 올리기는 화면 스레드에서 오고 목록 만들기는 분석
// 스레드에서 온다. 옛 판을 담아 두는 이 vector 만 겹치므로 여기만 잠근다.
std::mutex g_versions_mutex;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_named{false};
std::atomic<std::size_t> g_named_count{0};
std::atomic<std::size_t> g_total_count{0};

// 옛 판을 살려 둔 채 새 판으로 바꿔 끼운다. 그리는 쪽이 참조를 쥔 채로
// 프레임을 돌기 때문에 갈아엎으면 안 된다.
void publish_catalog(std::unique_ptr<std::vector<ItemCatalogEntry>> built) {
    const auto* p = built.get();
    {
        std::lock_guard<std::mutex> lk(g_versions_mutex);
        g_versions.push_back(std::move(built));
    }
    g_catalog.store(p, std::memory_order_release);
}

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
    g_total_count.store(total, std::memory_order_release);
    g_named_count.store(named, std::memory_order_release);
    publish_catalog(std::move(built));
    g_ready.store(true, std::memory_order_release);
    if (named > 0) g_named.store(true, std::memory_order_release);
    log::infof("아이템 표: {}개, 이름 풀린 것 {}개{}", total, named,
               named == 0 ? " - 현지화를 기다렸다 다시 만든다" : "");
    return named > 0;
}

bool items_ready() { return g_ready.load(std::memory_order_acquire); }

bool items_named() { return g_named.load(std::memory_order_acquire); }

std::size_t items_named_count() {
    return g_named_count.load(std::memory_order_acquire);
}

std::size_t items_total_count() {
    return g_total_count.load(std::memory_order_acquire);
}

const std::vector<ItemCatalogEntry>& item_catalog() {
    return *g_catalog.load(std::memory_order_acquire);
}

const ItemCatalogEntry* ItemKeyIndex::find(
    const std::vector<ItemCatalogEntry>& cat, std::uint32_t key) {
    if (built_data != cat.data() || built_size != cat.size()) {
        map.clear();
        map.reserve(cat.size());
        for (const auto& e : cat) map.emplace(e.key, &e);   // 먼저 온 것이 남는다
        built_data = cat.data();
        built_size = cat.size();
    }
    const auto it = map.find(key);
    return it == map.end() ? nullptr : it->second;
}

namespace {
// 화면 스레드와 명령 파일 스레드가 같이 부를 수 있어 잠근다.
std::mutex g_key_index_mutex;
ItemKeyIndex g_key_index;
}  // namespace

const ItemCatalogEntry* item_by_key(std::uint32_t key) {
    if (key == 0 || !items_ready()) return nullptr;
    std::lock_guard<std::mutex> lk(g_key_index_mutex);
    return g_key_index.find(item_catalog(), key);
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

// 맵 테이블에 접근하는 코드 관용구로 전역을 뽑는다:
//   48 8B /r [rip+disp32]   mov  reg, [전역]
//   48 83 /0 68             add  reg, 0x68     (테이블은 객체의 +0x68)
// 변환 함수 본문 패턴(kConvertBodyPattern)은 스택 오프셋에 의존해
// 업데이트로 깨졌다. 이 11바이트 관용구는 스택과 무관해 더 튼튼하고,
// 맵을 쓰는 여러 곳 중 하나만 맞아도 전역이 나온다. 이미지가 RVA
// 인덱스라 오프셋이 곧 명령의 RVA 다.
std::vector<std::uint64_t> load_add68_globals(
    const std::vector<std::uint8_t>& image) {
    std::vector<std::uint64_t> out;
    if (image.size() < 12) return out;
    // mov [rip] 의 modrm: mod=00, reg, rm=101. reg<<3 | 5.
    auto mov_reg = [](std::uint8_t m) -> int {
        if ((m & 0xC7) != 0x05) return -1;   // mod=00, rm=101
        return (m >> 3) & 7;
    };
    // add reg,imm8 의 modrm: mod=11, /0, rm=reg. 0xC0 | reg.
    auto add_reg = [](std::uint8_t m) -> int {
        if ((m & 0xF8) != 0xC0) return -1;
        return m & 7;
    };
    for (std::size_t i = 0; i + 11 <= image.size(); ++i) {
        if (image[i] != 0x48 || image[i + 1] != 0x8B) continue;
        const int r1 = mov_reg(image[i + 2]);
        if (r1 < 0) continue;
        if (image[i + 7] != 0x48 || image[i + 8] != 0x83 ||
            image[i + 10] != 0x68) {
            continue;
        }
        const int r2 = add_reg(image[i + 9]);
        if (r2 != r1) continue;   // 같은 레지스터라야 로드-후-더하기다
        std::int32_t disp = 0;
        std::memcpy(&disp, image.data() + i + 3, sizeof(disp));
        const std::int64_t rva = static_cast<std::int64_t>(i + 7) + disp;
        if (rva < 0 || static_cast<std::uint64_t>(rva) + 8 > image.size()) {
            continue;
        }
        out.push_back(static_cast<std::uint64_t>(rva));
    }
    // 같은 전역이 여러 곳에서 쓰인다. 한 번만 본다.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

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

    // 후보를 두 갈래로 모은다: 변환 함수 본문 패턴(옛 방식)과, 더
    // 튼튼한 load+add0x68 관용구. 업데이트로 앞엎것이 깨져도
    // 뒷엎것이 맵 전역을 잡는다. 합쳐서 개수가 맞는 유일한 것을 고른다.
    std::vector<std::uint64_t> rvas =
        find_item_key_map_rvas(image, kMaxCandidates);
    for (const auto rva : load_add68_globals(image)) rvas.push_back(rva);
    std::sort(rvas.begin(), rvas.end());
    rvas.erase(std::unique(rvas.begin(), rvas.end()), rvas.end());

    ItemKeyMap found;
    std::size_t hits = 0;
    for (const auto rva : rvas) {
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

// 대응표 테이블은 ItemInfoManager 자신의 +0x68 이다(실측: 맵 객체의
// RTTI 가 .?AVItemInfoManager 이고 매니저 +0x30 아이템 개수와 +0x6C
// 맵 개수가 둘 다 6813). 매니저는 RTTI 로 이미 찾으므로, 스캔·패턴
// 없이 즉시 읽는다. 업데이트에도 견딘다(클래스 이름은 안 바뀐다).
bool find_item_key_map_from_manager(const mem::Reader& reader,
                                    std::uintptr_t manager,
                                    std::uint32_t expected_count,
                                    ItemKeyMap* out) {
    if (out == nullptr || manager == 0 || expected_count == 0) return false;

    ItemKeyMap m;
    m.global = 0;
    m.object = manager;
    m.table = manager + kMapAtObject;

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

    if (m.count != expected_count) return false;
    if (m.capacity < m.count || m.capacity > kMaxItemCount) return false;
    if (m.record_count == 0 || m.record_count > kMaxItemCount) return false;
    if (slots == 0 || records == 0) return false;

    m.slots = static_cast<std::uintptr_t>(slots);
    m.records = static_cast<std::uintptr_t>(records);
    *out = m;
    return true;
}

namespace {

// 후보 table 자리의 레코드 몇 개를 따라가 키가 실제 아이템인지 본다.
// 이 의미 검증이 우연히 개수만 같은 다른 해시맵을 걸러 낸다.
bool records_look_like_items(const mem::Reader& reader,
                             std::uintptr_t records,
                             std::uint32_t record_count,
                             const std::vector<std::uint32_t>& sorted_keys) {
    if (records == 0 || record_count == 0 || sorted_keys.empty()) return false;

    const std::uint32_t probe = record_count < 64 ? record_count : 64;
    std::size_t checked = 0, present = 0;
    for (std::uint32_t i = 0; i < probe; ++i) {
        std::uint64_t rec = 0;
        if (!reader.read_value(records + i * 8, &rec) || rec == 0) continue;
        std::uint32_t key = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(rec) + kRecItemKey,
                               &key)) {
            continue;
        }
        ++checked;
        if (std::binary_search(sorted_keys.begin(), sorted_keys.end(), key)) {
            ++present;
        }
    }
    // 적어도 8개를 봤고 전부 아이템 키여야 한다. 하나라도 어긋나면
    // 다른 표다 - 아이템 대응표는 키가 전부 표에 있다(실측 누락 0).
    return checked >= 8 && present == checked;
}

}  // namespace

bool find_item_key_map_by_scan(const mem::Reader& reader,
                               std::uint32_t expected_count,
                               const std::vector<std::uint32_t>& sorted_keys,
                               ItemKeyMap* out) {
    if (out == nullptr || expected_count == 0) return false;

    // 큰 영역도 통째로 복사하지 않고 64MB 청크로 훑는다. 512MB 를
    // 건너뛰던 옛 코드는 맵 객체가 큰 힙 영역에 있으면 못 찾아
    // 대응표가 안 올라왔다(인벤토리·지급 패널이 막혔다). 맵 구조가
    // 청크 경계에 걸리지 않게 kOverlap 만큼 겹쳐 읽는다.
    constexpr std::size_t kChunk = 64u << 20;
    constexpr std::size_t kOverlap = 0x40;
    std::vector<std::uint8_t> buf;
    for (const auto& reg : reader.heap_regions()) {
        const auto base = reinterpret_cast<std::uintptr_t>(reg.begin);
        if (reg.size == 0) continue;
        for (std::size_t off = 0; off < reg.size; off += kChunk - kOverlap) {
            const std::size_t len =
                (reg.size - off < kChunk) ? (reg.size - off) : kChunk;
            if (len < 0x20) break;
            buf.resize(len);
            if (!reader.read(base + off, buf.data(), len)) continue;

            // 개수는 table+0x04 에 있다. table 은 8정렬(객체 16정렬
            // +0x68)이라 개수 u32 의 주소는 8로 나눠 4가 남는다.
            for (std::size_t i = 4; i + 0x20 <= len; i += 8) {
                std::uint32_t count = 0;
                std::memcpy(&count, buf.data() + i, 4);
                if (count != expected_count) continue;

                const std::size_t t = i - 4;   // table 시작
                std::uint32_t capacity = 0, record_count = 0;
                std::uint64_t slots = 0, records = 0;
                std::memcpy(&capacity, buf.data() + t + kMapCapacityField, 4);
                std::memcpy(&record_count, buf.data() + t + kMapRecCountField, 4);
                std::memcpy(&slots, buf.data() + t + kMapSlotsPtr, 8);
                std::memcpy(&records, buf.data() + t + kMapRecordsPtr, 8);

                if (capacity < count || capacity > kMaxItemCount) continue;
                if (record_count == 0 || record_count > kMaxItemCount) continue;
                if (slots == 0 || records == 0) continue;

                if (!records_look_like_items(
                        reader, static_cast<std::uintptr_t>(records),
                        record_count, sorted_keys)) {
                    continue;
                }

                ItemKeyMap m;
                m.table = base + off + t;
                m.object = m.table - kMapAtObject;
                m.global = 0;   // 전역이 아니라 객체를 직접 찾았다
                m.slots = static_cast<std::uintptr_t>(slots);
                m.records = static_cast<std::uintptr_t>(records);
                m.count = count;
                m.capacity = capacity;
                m.record_count = record_count;
                *out = m;
                return true;
            }
        }
    }
    return false;
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

// ------------------------------------- 모드용 대응표 캐시 (키 -> 순번)
//
// 소켓 지급에 쓴다. 보관함 파일은 보석의 아이템 키를 들고 있는데
// 게임에 보내는 6바이트는 순번으로 시작한다. 순번은 표에서의 위치라
// 게임이 갱신되면 달라지므로, 지급할 때 지금 표에서 다시 찾는다.

std::uint32_t find_item_id(const std::vector<ItemKeyPair>& sorted,
                           std::uint32_t key) {
    const auto it = std::lower_bound(
        sorted.begin(), sorted.end(), key,
        [](const ItemKeyPair& a, std::uint32_t k) { return a.key < k; });
    if (it == sorted.end() || it->key != key) return kNoItemId;
    return it->id;
}

namespace {

const std::vector<ItemKeyPair> kEmptyIds;

// 목록과 같은 이유로 바꿔 끼우기만 한다. 그리는 쪽이 참조를 쥔 채
// 프레임을 도는데 그 밑에서 vector 를 갈아엎으면 죽는다.
std::atomic<const std::vector<ItemKeyPair>*> g_ids{&kEmptyIds};
std::vector<std::unique_ptr<std::vector<ItemKeyPair>>> g_id_versions;
std::atomic<bool> g_ids_ready{false};

}  // namespace

bool discover_item_ids(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_ids_ready.load(std::memory_order_acquire)) return true;
    // 후보를 개수로 가리므로 아이템 표가 먼저 있어야 한다.
    if (!items_ready()) return false;
    const auto& cat = item_catalog();
    const auto count = static_cast<std::uint32_t>(cat.size());
    if (count == 0) return false;

    // 가장 빠르고 튼튼한 길: 대응표는 ItemInfoManager 의 +0x68 이다.
    // 매니저를 RTTI 로 찾아 바로 읽는다(스캔 없이 즉시).
    ItemKeyMap map;
    bool found = false;
    std::uintptr_t manager = 0;
    if (find_item_manager(rtti, reader, &manager)) {
        found = find_item_key_map_from_manager(reader, manager, count, &map);
    }
    // 만약을 위한 폴백: 매니저 레이아웃이 바뀌면 패턴·힙 스캔으로.
    if (!found && !find_item_key_map(reader, rtti.image(), count, &map)) {
        std::vector<std::uint32_t> keys;
        keys.reserve(cat.size());
        for (const auto& e : cat) keys.push_back(e.key);
        std::sort(keys.begin(), keys.end());
        if (!find_item_key_map_by_scan(reader, count, keys, &map)) return false;
        log::infof("아이템 대응표: 매니저+0x68 실패, 힙 스캔으로 찾았다 "
                   "(객체 0x{:X})", map.object);
    }

    auto built = std::make_unique<std::vector<ItemKeyPair>>();
    if (!read_item_key_map(reader, map, built.get()) || built->empty()) {
        return false;
    }
    std::sort(built->begin(), built->end(),
              [](const ItemKeyPair& a, const ItemKeyPair& b) {
                  return a.key < b.key;
              });

    const std::size_t n = built->size();
    const auto* p = built.get();
    g_id_versions.push_back(std::move(built));
    g_ids.store(p, std::memory_order_release);
    g_ids_ready.store(true, std::memory_order_release);
    log::infof("아이템 대응표: {}개 (키 -> 순번)", n);
    return true;
}

bool item_ids_ready() { return g_ids_ready.load(std::memory_order_acquire); }

std::uint32_t item_id_for_key(std::uint32_t key) {
    return find_item_id(*g_ids.load(std::memory_order_acquire), key);
}

void make_socket_bytes(std::uint16_t gem_id, std::uint8_t out[6]) {
    if (out == nullptr) return;
    // 채움 표시: 보석이 있으면 0xFFFF, 빈 칸이면 0x0000.
    const std::uint16_t marker = (gem_id == 0xFFFF) ? 0x0000u : 0xFFFFu;
    std::memcpy(out, &gem_id, sizeof(gem_id));
    std::memcpy(out + 2, &marker, sizeof(marker));
    out[4] = 0x00;               // 게임이 칸 번호로 덮어쓴다
    out[5] = kSocketOpenTail;    // 열린 칸은 전부 0x04 (실측)
}

bool socket_bytes_for_key(std::uint32_t gem_key, std::uint8_t out[6]) {
    if (out == nullptr) return false;
    const std::uint32_t id = item_id_for_key(gem_key);
    if (id == kNoItemId || id > 0xFFFF) return false;
    make_socket_bytes(static_cast<std::uint16_t>(id), out);
    return true;
}

std::int16_t max_sharpness_for(std::uint32_t item_key) {
    if (!items_ready()) return 0;
    for (const auto& e : item_catalog()) {
        if (e.key == item_key) return e.max_sharpness;
    }
    return 0;
}

std::uint32_t socket_room(std::uint32_t max_sockets, std::uint32_t max_stack,
                          std::uint16_t equip_type) {
    // 장비가 아니면 소켓수>0 이 오류 갈래다(0x2A7022C).
    if (equip_type == 0xFFFF) return 0;
    // 겹치는 아이템도 같은 갈래로 간다(판별자가 max_stack>1 을 본다).
    if (max_stack > 1) return 0;
    return max_sockets;
}

std::uint32_t socket_room_for(std::uint32_t item_key) {
    if (!items_ready()) return 0;
    for (const auto& e : item_catalog()) {
        if (e.key != item_key) continue;
        return socket_room(e.max_sockets, e.max_stack, e.equip_type);
    }
    return 0;
}

// ------------------------------------------------- 소켓 상한 올리기

namespace {

// 되돌리려고 기억해 두는 원본.
struct CapSaved {
    std::uintptr_t record = 0;
    std::uint32_t key = 0;
    std::uint32_t original = 0;
    std::uint32_t applied = 0;
};
std::mutex g_cap_mutex;
std::vector<CapSaved> g_cap_saved;
std::vector<SocketCapRule> g_cap_rules;
std::atomic<bool> g_cap_on{false};

// 인프로세스 직접 쓰기. **주입 DLL 전용**이다(equip.cpp 와 같은 규약).
bool cap_wr32(std::uintptr_t at, std::uint32_t v) {
    __try {
        *reinterpret_cast<volatile std::uint32_t*>(at) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 규칙에서 이 아이템이 받을 값. 없으면 0(=안 건드림).
std::uint32_t want_for(const std::vector<SocketCapRule>& rules,
                       std::uint8_t cat, std::uint16_t etype) {
    for (const auto& r : rules) {
        if (r.part.category == cat && r.part.equip_type == etype) {
            return r.want;
        }
    }
    return 0;
}

// 지금 목록을 베껴 소켓 상한만 갈아 끼운 새 판을 낸다. 이름을 다시
// 풀지 않으므로 값싸다(표를 다시 걷는 재생성이 필요 없다).
// `caps` 는 키 -> 새 상한.
void republish_caps(const std::vector<std::pair<std::uint32_t, std::uint32_t>>&
                        caps) {
    const auto& cur = item_catalog();
    if (cur.empty() || caps.empty()) return;
    auto built = std::make_unique<std::vector<ItemCatalogEntry>>(cur);
    for (auto& e : *built) {
        for (const auto& [key, cap] : caps) {
            if (key != e.key) continue;
            e.max_sockets = cap;
            break;
        }
    }
    publish_catalog(std::move(built));
}

}  // namespace

bool socket_cap_target(std::uint32_t max_sockets, std::uint16_t equip_type,
                       std::uint32_t want) {
    if (want == 0) return false;
    if (equip_type == 0xFFFF) return false;   // 장비가 아니다
    return max_sockets < want;
}

std::vector<SocketPartInfo> socket_parts() {
    std::vector<SocketPartInfo> out;
    if (!items_ready()) return out;
    for (const auto& e : item_catalog()) {
        if (e.equip_type == 0xFFFF) continue;   // 장비만
        SocketPartInfo* p = nullptr;
        for (auto& x : out) {
            if (x.part.category == e.category &&
                x.part.equip_type == e.equip_type) {
                p = &x;
                break;
            }
        }
        if (p == nullptr) {
            out.push_back(SocketPartInfo{SocketPart{e.category, e.equip_type},
                                         0, 0, 0, {}});
            p = &out.back();
        }
        ++p->count;
        if (e.max_sockets > 0) ++p->with_socket;
        if (e.max_sockets > p->table_cap) p->table_cap = e.max_sockets;
        if (p->sample.empty() && !e.name.empty()) p->sample = e.name;
    }
    std::sort(out.begin(), out.end(),
              [](const SocketPartInfo& a, const SocketPartInfo& b) {
                  if (a.part.category != b.part.category) {
                      return a.part.category < b.part.category;
                  }
                  return a.part.equip_type < b.part.equip_type;
              });
    return out;
}

bool socket_cap_active() { return g_cap_on.load(std::memory_order_acquire); }

std::vector<SocketCapRule> socket_cap_rules() {
    std::lock_guard<std::mutex> lk(g_cap_mutex);
    return g_cap_rules;
}

SocketCapResult socket_cap_apply(const mem::Reader& reader,
                                 const std::vector<SocketCapRule>& rules) {
    SocketCapResult r;
    if (!items_ready()) return r;

    // 값이 성한지 먼저 본다. 이상한 값을 게임 표에 쓰느니 아무것도 안 한다.
    for (const auto& rule : rules) {
        if (rule.want > kSocketSlotMax) return r;
    }
    if (socket_cap_active()) socket_cap_restore(reader);

    std::vector<CapSaved> saved;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> caps;
    const auto& cat = item_catalog();
    saved.reserve(512);
    caps.reserve(512);
    for (const auto& e : cat) {
        if (e.record == 0) continue;
        const std::uint32_t want = want_for(rules, e.category, e.equip_type);
        if (!socket_cap_target(e.max_sockets, e.equip_type, want)) {
            ++r.skipped;
            continue;
        }
        // 목록이 낡았을 수 있다. 표에서 다시 읽어 대조한 뒤에만 쓴다.
        std::uint32_t live = 0;
        if (!reader.read_value(e.record + kRecSockets, &live)) continue;
        if (live != e.max_sockets) continue;   // 목록과 표가 어긋난다
        if (!cap_wr32(e.record + kRecSockets, want)) continue;
        std::uint32_t back = 0;
        if (!reader.read_value(e.record + kRecSockets, &back) || back != want) {
            continue;                           // 안 써졌다
        }
        saved.push_back(CapSaved{e.record, e.key, live, want});
        caps.emplace_back(e.key, want);
        ++r.changed;
    }

    if (r.changed == 0) return r;
    {
        std::lock_guard<std::mutex> lk(g_cap_mutex);
        g_cap_saved = saved;
        g_cap_rules = rules;
    }
    g_cap_on.store(true, std::memory_order_release);
    republish_caps(caps);
    r.ok = true;
    log_write("소켓 상한", 0, "-",
              std::format("부위 규칙 {}개, 아이템 {}개 올림 (대상 아님 {})",
                          rules.size(), r.changed, r.skipped));
    return r;
}

SocketCapResult socket_cap_restore(const mem::Reader& reader) {
    SocketCapResult r;
    std::vector<CapSaved> saved;
    {
        std::lock_guard<std::mutex> lk(g_cap_mutex);
        saved.swap(g_cap_saved);
        g_cap_rules.clear();
    }
    if (saved.empty()) return r;

    std::vector<std::pair<std::uint32_t, std::uint32_t>> caps;
    caps.reserve(saved.size());
    for (const auto& s : saved) {
        if (cap_wr32(s.record + kRecSockets, s.original)) {
            ++r.changed;
        } else {
            ++r.skipped;
        }
        caps.emplace_back(s.key, s.original);
    }
    g_cap_on.store(false, std::memory_order_release);
    republish_caps(caps);
    r.ok = true;
    log::infof("소켓 상한: {}개를 원래 값으로 되돌렸다 (실패 {})", r.changed,
               r.skipped);
    (void)reader;
    return r;
}

std::uint16_t full_endurance_for(std::uint32_t item_key) {
    if (!items_ready()) return 0;
    for (const auto& e : item_catalog()) {
        if (e.key != item_key) continue;
        return (e.max_endurance == 0xFFFF) ? 0 : e.max_endurance;
    }
    return 0;
}

bool is_socket_gem(const ItemCatalogEntry& e) {
    return e.category == kSocketGemCategory && !e.name.empty();
}

}  // namespace cdtb::game
