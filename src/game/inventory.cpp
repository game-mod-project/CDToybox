#include "game/inventory.h"

#include <atomic>
#include <cstring>
#include <utility>

#include "core/log.h"

namespace cdtb::game {
namespace {

// --- 컴포넌트 ---
constexpr std::size_t kContainersPtr = 0x18;    // 컨테이너 포인터 배열
constexpr std::size_t kContainerCount = 0x20;   // u32 개수

// --- 컨테이너 ---
constexpr std::size_t kContRecordsPtr = 0x00;
constexpr std::size_t kContSlots = 0x08;        // u32 배열 칸 수
constexpr std::size_t kContKind = 0x10;         // u16
constexpr std::size_t kContUsed = 0x12;         // u16
constexpr std::size_t kContCapacity = 0x14;     // u16

// --- 레코드 ---
constexpr std::size_t kRecStride = 0xC8;
constexpr std::size_t kRecInstanceId = 0x00;    // u64
constexpr std::size_t kRecIndex = 0x08;         // u16 아이템 표 순번
constexpr std::size_t kRecTemper = 0x0A;        // u16 담금질
constexpr std::size_t kRecEndurance = 0x40;     // u16 현재 내구도
constexpr std::size_t kRecSharpness = 0x58;     // u16 장비 연마
constexpr std::size_t kRecCount = 0x10;         // i64
constexpr std::size_t kRecSockets = 0x60;       // ptr 소켓 배열
// 벡터의 크기·용량이다. **아이템의 소켓 칸 수가 아니다** - 게임이 이
// 배열을 언제나 5칸으로 잡아 실측이 예외 없이 5/5 다(0x234F930 이
// `mov edx,5` 로 확보하고 `mov [r14+0x68],5` 로 굳힌다).
constexpr std::size_t kRecSocketSize = 0x68;    // u32 (늘 5)
// **열린 소켓 칸 수**가 여기다. TrItemValue +0x5E 가 그대로 온다.
// 0 이면 다섯 칸이 전부 잠긴(byte[4]==0xFF) 상태다.
constexpr std::size_t kRecOpenSockets = 0x70;   // u8

constexpr std::uint16_t kEmptyIndex = 0xFFFF;
constexpr std::uint64_t kEmptyInstance = ~0ull;

}  // namespace

bool read_inventory_containers(const mem::Reader& reader,
                               std::uintptr_t component,
                               std::vector<InventoryContainer>* out) {
    if (out == nullptr || component == 0) return false;

    std::uint64_t array = 0;
    std::uint32_t count = 0;
    if (!reader.read_value(component + kContainersPtr, &array)) return false;
    if (!reader.read_value(component + kContainerCount, &count)) return false;
    // 잘못 집으면 개수가 쓰레기값이 된다. 그대로 믿고 할당하면
    // 메모리를 통째로 먹는다. 실측은 18개다.
    if (array == 0 || count == 0 || count > kMaxInventoryContainers) {
        return false;
    }

    std::vector<std::uint64_t> ptrs(count, 0);
    if (!reader.read(static_cast<std::uintptr_t>(array), ptrs.data(),
                     ptrs.size() * sizeof(std::uint64_t))) {
        return false;
    }

    std::vector<InventoryContainer> cs;
    cs.reserve(count);
    for (const auto raw : ptrs) {
        if (raw == 0) continue;   // 널 슬롯. 나머지는 계속 읽는다.

        InventoryContainer c;
        c.address = static_cast<std::uintptr_t>(raw);

        std::uint64_t records = 0;
        if (!reader.read_value(c.address + kContRecordsPtr, &records)) continue;
        if (!reader.read_value(c.address + kContSlots, &c.slots)) continue;
        if (!reader.read_value(c.address + kContKind, &c.kind)) continue;
        if (!reader.read_value(c.address + kContUsed, &c.used)) continue;
        if (!reader.read_value(c.address + kContCapacity, &c.capacity)) {
            continue;
        }
        if (records == 0 || c.slots == 0 || c.slots > kMaxInventorySlots) {
            continue;
        }

        c.records = static_cast<std::uintptr_t>(records);
        cs.push_back(c);
    }
    *out = std::move(cs);
    return true;
}

bool read_inventory_records(const mem::Reader& reader,
                            const InventoryContainer& container,
                            std::vector<InventoryRecord>* out) {
    if (out == nullptr || container.records == 0) return false;
    if (container.slots == 0 || container.slots > kMaxInventorySlots) {
        return false;
    }

    std::vector<std::uint8_t> raw(
        static_cast<std::size_t>(container.slots) * kRecStride);
    if (!reader.read(container.records, raw.data(), raw.size())) return false;

    std::vector<InventoryRecord> rs;
    rs.reserve(container.used);
    for (std::uint32_t i = 0; i < container.slots; ++i) {
        const std::uint8_t* p =
            raw.data() + static_cast<std::size_t>(i) * kRecStride;

        std::uint64_t instance = 0, sockets = 0;
        std::uint16_t index = 0, temper = 0, endurance = 0, sharp = 0;
        std::uint32_t socket_size = 0;
        std::uint8_t open_sockets = 0;
        std::int64_t count = 0;
        std::memcpy(&instance, p + kRecInstanceId, sizeof(instance));
        std::memcpy(&index, p + kRecIndex, sizeof(index));
        std::memcpy(&temper, p + kRecTemper, sizeof(temper));
        std::memcpy(&endurance, p + kRecEndurance, sizeof(endurance));
        std::memcpy(&sharp, p + kRecSharpness, sizeof(sharp));
        std::memcpy(&count, p + kRecCount, sizeof(count));
        std::memcpy(&sockets, p + kRecSockets, sizeof(sockets));
        std::memcpy(&socket_size, p + kRecSocketSize, sizeof(socket_size));
        std::memcpy(&open_sockets, p + kRecOpenSockets, sizeof(open_sockets));

        // 빈 칸은 인스턴스 ID 가 전부 0xFF 이고 순번도 0xFFFF 다.
        if (instance == kEmptyInstance || index == kEmptyIndex) continue;
        if (count <= 0) continue;

        InventoryRecord r;
        r.address = container.records + static_cast<std::size_t>(i) * kRecStride;
        r.slot = i;
        r.instance_id = instance;
        r.index = index;
        r.temper = temper;
        r.endurance = endurance;
        r.sharpness = sharp;
        r.count = count;
        r.sockets = static_cast<std::uintptr_t>(sockets);
        r.socket_count = socket_size;
        r.open_sockets = open_sockets;
        rs.push_back(r);
    }
    *out = std::move(rs);
    return true;
}

bool read_inventory_sockets(const mem::Reader& reader,
                            const InventoryRecord& record,
                            std::vector<InventorySocket>* out) {
    if (out == nullptr || record.sockets == 0) return false;
    if (record.socket_count == 0 || record.socket_count > kMaxSockets) {
        return false;
    }

    std::vector<std::uint8_t> raw(
        static_cast<std::size_t>(record.socket_count) * kSocketSize);
    if (!reader.read(record.sockets, raw.data(), raw.size())) return false;

    std::vector<InventorySocket> ss;
    ss.reserve(record.socket_count);
    for (std::uint32_t i = 0; i < record.socket_count; ++i) {
        const std::uint8_t* p =
            raw.data() + static_cast<std::size_t>(i) * kSocketSize;

        InventorySocket s;
        s.slot = i;
        std::memcpy(&s.index, p, sizeof(s.index));
        // 뜻을 다 모르므로 원본을 그대로 들고 있는다. export 는 모르는
        // 칸까지 되돌려야 한다.
        std::memcpy(s.raw, p, kSocketSize);
        ss.push_back(s);
    }
    *out = std::move(ss);
    return true;
}


// --------------------------------------------------- 모드용 배경 탐색

namespace {

constexpr const char* kInventoryClass =
    ".?AVServerInventoryActorComponent@pa@@";

std::atomic<std::uintptr_t> g_component{0};
std::atomic<bool> g_rescan{false};

}  // namespace

bool discover_inventory(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_component.load(std::memory_order_acquire) != 0) return true;

    for (const auto addr : rtti.instances_of_class(kInventoryClass, 64)) {
        std::vector<InventoryContainer> cs;
        if (!read_inventory_containers(reader, addr, &cs)) continue;
        bool any = false;
        for (const auto& c : cs) {
            if (c.used > 0) {
                any = true;
                break;
            }
        }
        if (!any) continue;   // 빈 컴포넌트가 여럿 살아 있다
        g_component.store(addr, std::memory_order_release);
        log::infof("인벤토리 컴포넌트 0x{:X}", addr);
        return true;
    }
    return false;
}

bool inventory_ready() {
    return g_component.load(std::memory_order_acquire) != 0;
}

std::uintptr_t inventory_component() {
    return g_component.load(std::memory_order_acquire);
}

void forget_inventory() {
    g_component.store(0, std::memory_order_release);
    request_inventory_rescan();
}

void request_inventory_rescan() {
    g_rescan.store(true, std::memory_order_release);
}

bool take_inventory_rescan() {
    return g_rescan.exchange(false, std::memory_order_acq_rel);
}

// --------------------------------------------------------- 표시용 변환

InventoryRowText format_inventory_row(std::uint32_t endurance,
                                      std::uint32_t sharpness,
                                      const std::vector<std::string>& gems) {
    InventoryRowText t;
    if (endurance != kNoEndurance) t.endurance = std::to_string(endurance);
    if (sharpness != 0) t.sharpness = std::to_string(sharpness);
    for (const auto& g : gems) {
        if (g.empty()) continue;
        if (!t.sockets.empty()) t.sockets += ", ";
        t.sockets += g;
    }
    return t;
}


std::vector<std::string> inventory_scan_classes() { return {kInventoryClass}; }

}  // namespace cdtb::game
