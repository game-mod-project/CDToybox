#include "game/inventory.h"
#include "core/write_log.h"

#include <vector>

#include <string>

#include <mutex>

#include <windows.h>

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
// 같은 인벤토리가 클라·서버 두 벌로 존재한다. 가방 확장은 화면 숫자의 출처가
// 어느 쪽인지 확정되지 않아 **둘 다** 쓴다(장비·게이지의 both-realms 와 같은 이유).
constexpr const char* kInventoryClassClient =
    ".?AVClientInventoryActorComponent@pa@@";

std::atomic<std::uintptr_t> g_component{0};
std::atomic<std::uintptr_t> g_component_client{0};
std::atomic<bool> g_rescan{false};

// 내용이 든 컴포넌트인가. 빈 것이 여럿 살아 있어 그것으로 가른다.
bool component_has_items(const mem::Reader& reader, std::uintptr_t addr,
                         std::vector<InventoryContainer>* out) {
    if (!read_inventory_containers(reader, addr, out)) return false;
    for (const auto& c : *out) {
        if (c.used > 0) return true;
    }
    return false;
}

}  // namespace

bool discover_inventory(const mem::Rtti& rtti, const mem::Reader& reader) {
    const bool have_server = g_component.load(std::memory_order_acquire) != 0;
    const bool have_client =
        g_component_client.load(std::memory_order_acquire) != 0;
    if (have_server && have_client) return true;

    // 두 클래스를 **힙 한 번 훑기**로 같이 찾는다 - instances_of_class 는 vtable
    // 마다 힙 전체를 읽어, 따로 부르면 훑기가 두 배가 된다.
    for (const auto& f : rtti.find_objects_of(
             {kInventoryClass, kInventoryClassClient}, 128)) {
        const bool is_client = f.cls == kInventoryClassClient;
        auto& slot = is_client ? g_component_client : g_component;
        if (slot.load(std::memory_order_acquire) != 0) continue;
        std::vector<InventoryContainer> cs;
        if (!component_has_items(reader, f.address, &cs)) continue;
        slot.store(f.address, std::memory_order_release);
        log::infof("인벤토리 컴포넌트{} 0x{:X}", is_client ? "(클라)" : "",
                   f.address);
    }
    return g_component.load(std::memory_order_acquire) != 0;
}

bool inventory_ready() {
    return g_component.load(std::memory_order_acquire) != 0;
}

std::uintptr_t inventory_component() {
    return g_component.load(std::memory_order_acquire);
}

std::uintptr_t inventory_component_client() {
    return g_component_client.load(std::memory_order_acquire);
}

void forget_inventory() {
    g_component.store(0, std::memory_order_release);
    g_component_client.store(0, std::memory_order_release);
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


// ------------------------------------------------------ 가방·보관함 확장

BagPlan plan_bag_expand(int cap, int sum, int a, int b, int slots, int target) {
    BagPlan p;
    // 모르는 모양은 건드리지 않는다. 옛 사고는 한 칸만 보고 기본 슬롯을 잘못
    // 유도한 데서 시작했으므로, 모델이 안 맞으면 쓰지 않는 쪽이 맞다.
    if (cap < 0 || sum < 0 || a < 0 || b < 0 || slots <= 0) {
        p.skip = "값이 음수다";
        return p;
    }
    if (cap < sum) {
        p.skip = "용량이 확장 합계보다 작다";
        return p;
    }
    if (a != 0 && b != 0 && a + b != sum) {
        p.skip = "확장 두 갈래의 합이 합계와 다르다";
        return p;
    }
    p.base = cap - sum;   // **여기가 핵심** - +0x1A 가 아니라 합계로 유도한다
    if (p.base <= 0) {
        p.skip = "기본 슬롯이 0 이하다";
        return p;
    }
    int want = target;
    if (want > kBagTargetMax) want = kBagTargetMax;
    if (want > kBagEngineMax) want = kBagEngineMax;   // 이중 방어
    if (want > slots) want = slots;                   // 물리 배열을 넘지 않는다
    if (want < p.base) {
        p.skip = "목표가 기본 슬롯보다 작다";
        return p;
    }
    // +0x18(a)은 그대로 두고 +0x1A 만 조정한다. 엔진이 합계를 sum 으로 재계산하든
    // max 로 재계산하든 결과가 want 이하가 된다(max 면 오히려 작아진다).
    p.expand_b = want - p.base - a;
    if (p.expand_b < 0) {
        p.skip = "이미 목표보다 크다";
        return p;
    }
    p.sum = a + p.expand_b;
    p.capacity = p.base + p.sum;
    p.apply = p.capacity != cap || p.expand_b != b;
    if (!p.apply) p.skip = "이미 그 값이다";
    return p;
}

namespace {

// 확장 전 원본. 되돌리기가 **진짜 복원**이 되게 한다 - 옛 restore 는 확장을 0 으로
// 써서 가방의 190·보관함의 200 을 날렸다(그건 복원이 아니다).
struct BagBackup {
    std::uintptr_t address = 0;
    std::uint16_t cap = 0, sum = 0, a = 0, b = 0;
};
std::mutex g_bag_mtx;
std::vector<BagBackup> g_bag_backup;

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool bag_wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool want_kind(std::uint16_t kind, bool storage) {
    if (kind == 1) return true;                 // 가방
    if (!storage) return false;
    // 보관함류만. 용량 5·20 짜리 작은 것까지 부풀린 것이 2026-09-05 의
    // "리로드 후 지급 손상" 이었다.
    return kind == 4 || kind == 7 || kind == 9 || kind == 11;
}

// 한 컴포넌트의 대상 컨테이너에 계획을 적용한다.
void apply_to(const mem::Reader& reader, std::uintptr_t comp, int target,
              bool storage, bool remember, BagResult* r) {
    if (comp == 0) return;
    std::vector<InventoryContainer> cs;
    if (!read_inventory_containers(reader, comp, &cs)) return;
    for (const auto& c : cs) {
        if (!want_kind(c.kind, storage)) continue;
        std::uint16_t cap = 0, sum = 0, a = 0, b = 0;
        if (!reader.read_value(c.address + 0x14, &cap) ||
            !reader.read_value(c.address + 0x16, &sum) ||
            !reader.read_value(c.address + 0x18, &a) ||
            !reader.read_value(c.address + 0x1A, &b)) {
            ++r->fail;
            continue;
        }
        const BagPlan p = plan_bag_expand(cap, sum, a, b,
                                          static_cast<int>(c.slots), target);
        if (!p.apply) {
            ++r->skip;
            continue;
        }
        if (remember) {
            std::lock_guard<std::mutex> lk(g_bag_mtx);
            g_bag_backup.push_back(BagBackup{c.address, cap, sum, a, b});
        }
        // 저장이 담는 칸 -> 합계 -> 캐시 순으로. +0x18 은 건드리지 않는다.
        bag_wr16(c.address + 0x1A, static_cast<std::uint16_t>(p.expand_b));
        bag_wr16(c.address + 0x16, static_cast<std::uint16_t>(p.sum));
        bag_wr16(c.address + 0x14, static_cast<std::uint16_t>(p.capacity));
        std::uint16_t back = 0;
        if (reader.read_value(c.address + 0x14, &back) &&
            back == static_cast<std::uint16_t>(p.capacity)) {
            ++r->changed;
            log_write("가방 용량", c.address + 0x14, std::to_string(cap),
                      std::to_string(p.capacity));
        } else {
            ++r->fail;
        }
    }
}

}  // namespace

BagResult bag_expand(const mem::Reader& reader, int target, bool storage) {
    BagResult r;
    {
        std::lock_guard<std::mutex> lk(g_bag_mtx);
        g_bag_backup.clear();   // 이번 확장의 원본만 기억한다
    }
    apply_to(reader, inventory_component(), target, storage, true, &r);
    apply_to(reader, inventory_component_client(), target, storage, true, &r);
    log::infof("가방 확장: 목표 {} -> 바꾼 것 {}개, 건너뜀 {}, 실패 {}", target,
               r.changed, r.skip, r.fail);
    return r;
}

bool bag_has_backup() {
    std::lock_guard<std::mutex> lk(g_bag_mtx);
    return !g_bag_backup.empty();
}

BagResult bag_restore(const mem::Reader& reader) {
    BagResult r;
    std::vector<BagBackup> saved;
    {
        std::lock_guard<std::mutex> lk(g_bag_mtx);
        saved = g_bag_backup;
    }
    for (const auto& s : saved) {
        // 되돌릴 때도 쓴 순서의 역순으로 - 캐시(+0x14)를 마지막에 맞춘다.
        bag_wr16(s.address + 0x1A, s.b);
        bag_wr16(s.address + 0x16, s.sum);
        bag_wr16(s.address + 0x14, s.cap);
        std::uint16_t back = 0;
        if (reader.read_value(s.address + 0x14, &back) && back == s.cap) {
            ++r.changed;
        } else {
            ++r.fail;
        }
    }
    if (r.fail == 0) {
        std::lock_guard<std::mutex> lk(g_bag_mtx);
        g_bag_backup.clear();
    }
    log::infof("가방 복원: 되돌린 것 {}개, 실패 {}", r.changed, r.fail);
    return r;
}

}  // namespace cdtb::game
