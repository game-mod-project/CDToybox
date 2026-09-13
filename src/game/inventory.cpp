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
    // 상한은 통과 캐시(kPassScanPerClass = 64)의 두 클래스분과 맞춘다.
    constexpr std::size_t kFindMax = 128;
    const auto found = rtti.find_objects_of(
        {kInventoryClass, kInventoryClassClient}, kFindMax);
    if (found.size() >= kFindMax) {
        // 닿으면 진짜 컴포넌트가 잘려 나갈 수 있고, 증상이 "아직 없다" 와 구분되지
        // 않는다(리뷰 지적 12, camera.cpp 의 같은 경고와 형태를 맞춘다).
        log::warnf("인벤토리 후보가 상한 {}에 닿았다 - 잘렸을 수 있다", kFindMax);
    }
    for (const auto& f : found) {
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

bool inventory_both_ready() {
    return g_component.load(std::memory_order_acquire) != 0 &&
           g_component_client.load(std::memory_order_acquire) != 0;
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
    // 가방 백업이 가리키던 세계가 사라졌다. **낡은 주소에 되돌려 쓰면 재활용된
    // 힙을 때린다**(clan-roster-volatile-writes 와 같은 함정, 리뷰 지적 1).
    forget_bag_backup();
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


// 두 이름을 다 넣어야 한다. find_objects_of 는 **이름이 전부 캐시에 있을 때만**
// 스냅숏을 쓰므로, 하나라도 빠지면 통과마다 11GB 힙을 다시 훑는다(리뷰 지적 5).
std::vector<std::string> inventory_scan_classes() {
    return {kInventoryClass, kInventoryClassClient};
}


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
    // 한쪽이 0 이어도 검사한다. 예전에는 `a != 0 && b != 0 &&` 가 붙어 있어,
    // 우리가 모르는 제3의 갈래가 +0x16 에 값을 넣은 모양이 그냥 통과했다 - 그러면
    // 엔진 재계산에서 732 를 넘길 수 있었다(리뷰 지적 2). 실측 18개 전부가
    // sum == a + b 라 무조건으로 바꿔도 쓰던 컨테이너를 하나도 잃지 않는다.
    if (a + b != sum) {
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

bool bag_kind_selected(std::uint16_t kind, bool storage) {
    if (kind == 1) return true;   // 가방
    if (!storage) return false;
    // 보관함류만. 용량 5·10·20·50 짜리 작은 칸은 어느 쪽이든 건드리지 않는다 -
    // 그것까지 부풀린 것이 2026-09-05 "리로드 후 지급 손상" 의 유력한 원인이다.
    // **종류 4 는 뺀다**: 혼자 +0x20 에 8칸짜리 보조 배열을 다는데(실측 2026-09-13)
    // 그 정체를 모른다. 용량만 올리고 그쪽을 두는 것은 모르는 모양을 건드리는
    // 것이라, 확인 전까지는 7·9·11 만으로 간다(리뷰 지적 11).
    return kind == 7 || kind == 9 || kind == 11;
}

namespace {

// 확장 전 원본. 되돌리기가 **진짜 복원**이 되게 한다 - 옛 restore 는 확장을 0 으로
// 써서 가방의 190·보관함의 200 을 날렸다(그건 복원이 아니다).
struct BagBackup {
    std::uintptr_t address = 0;
    std::uint16_t kind = 0;   // 재활용된 주소를 가려내는 데 쓴다
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

// 한 컴포넌트의 대상 컨테이너에 계획을 적용한다.
void apply_to(const mem::Reader& reader, std::uintptr_t comp, int target,
              bool storage, bool remember, BagResult* r) {
    if (comp == 0) return;
    std::vector<InventoryContainer> cs;
    if (!read_inventory_containers(reader, comp, &cs)) return;
    const int before = r->changed;
    for (const auto& c : cs) {
        if (!bag_kind_selected(c.kind, storage)) continue;
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
            r->last_skip = p.skip;   // 화면·로그에 이유를 낸다(리뷰 지적 10)
            log::infof("가방 건너뜀: 종류 {} 0x{:X} - {}", c.kind, c.address,
                       p.skip);
            continue;
        }
        if (remember) {
            // **주소당 첫 원본만** 남긴다. 예전에는 bag_expand 머리에서 무조건
            // 비웠는데, 그러면 같은 값으로 한 번 더 누르기만 해도 원본이 사라져
            // 되돌릴 수단이 영영 없어졌다(리뷰 지적 3).
            std::lock_guard<std::mutex> lk(g_bag_mtx);
            bool seen = false;
            for (const auto& x : g_bag_backup) {
                if (x.address == c.address) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                g_bag_backup.push_back(
                    BagBackup{c.address, c.kind, cap, sum, a, b});
            }
        }
        // 저장이 담는 칸 -> 합계 -> 캐시 순으로. +0x18 은 건드리지 않는다.
        // 쓰기 성공을 **버리지 않는다** - 이 기능은 세이브에 값을 박는 유일한
        // 기능이라, 무엇이 실제로 써졌는지 틀리게 적으면 사고 뒤 재구성이 안 된다.
        bool ok = bag_wr16(c.address + 0x1A,
                           static_cast<std::uint16_t>(p.expand_b));
        ok = bag_wr16(c.address + 0x16, static_cast<std::uint16_t>(p.sum)) && ok;
        ok = bag_wr16(c.address + 0x14,
                      static_cast<std::uint16_t>(p.capacity)) && ok;
        std::uint16_t back_cap = 0, back_b = 0;
        // **세이브가 담는 칸(+0x1A)도 되읽는다.** 화면 캐시만 보면 "화면은 700,
        // 저장 칸은 그대로" 인 상태를 성공으로 적게 된다(리뷰 지적 6).
        const bool read_ok = reader.read_value(c.address + 0x14, &back_cap) &&
                             reader.read_value(c.address + 0x1A, &back_b);
        if (ok && read_ok &&
            back_cap == static_cast<std::uint16_t>(p.capacity) &&
            back_b == static_cast<std::uint16_t>(p.expand_b)) {
            ++r->changed;
            log_write("가방 용량", c.address + 0x14, std::to_string(cap),
                      std::to_string(p.capacity));
            log_write("가방 확장칸", c.address + 0x1A, std::to_string(b),
                      std::to_string(p.expand_b));
        } else {
            // 반쯤 써진 채로 두지 않는다. 남는 값은 전부 목표 이하라 위험하지는
            // 않지만, 사용자가 "왜 이 값인가" 를 알 수 없는 상태가 된다.
            bag_wr16(c.address + 0x1A, b);
            bag_wr16(c.address + 0x16, sum);
            bag_wr16(c.address + 0x14, cap);
            ++r->fail;
            log::warnf("가방 확장 실패(되돌림): 0x{:X} 종류 {}", c.address,
                       c.kind);
        }
    }
    if (r->changed > before) ++r->realms;
}

}  // namespace

BagResult bag_expand(const mem::Reader& reader, int target, bool storage) {
    // **백업을 비우지 않는다.** 주소당 첫 원본만 남기므로 여러 번 눌러도 처음 값이
    // 지켜진다(리뷰 지적 3). 비우는 것은 되돌리기 성공과 forget_inventory 뿐이다.
    BagResult r;
    apply_to(reader, inventory_component(), target, storage, true, &r);
    apply_to(reader, inventory_component_client(), target, storage, true, &r);
    log::infof("가방 확장: 목표 {} -> 바꾼 것 {}개({} realm), 건너뜀 {}, 실패 {}",
               target, r.changed, r.realms, r.skip, r.fail);
    return r;
}

void forget_bag_backup() {
    std::lock_guard<std::mutex> lk(g_bag_mtx);
    g_bag_backup.clear();
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
    // **지금 살아 있는 컨테이너만 되돌린다.** 게임이 인벤토리를 새로 만들면(재접속·
    // 캐릭터 교체·월드 재진입) 백업의 주소는 남의 객체가 된다. bag_wr16 의 SEH 는
    // 매핑 안 된 페이지만 막지, 재할당된 유효 주소는 조용히 써 버린다
    // (clan-roster-volatile-writes 와 같은 함정, 리뷰 지적 1).
    std::vector<std::pair<std::uintptr_t, std::uint16_t>> alive;
    for (const auto comp : {inventory_component(), inventory_component_client()}) {
        if (comp == 0) continue;
        std::vector<InventoryContainer> cs;
        if (!read_inventory_containers(reader, comp, &cs)) continue;
        for (const auto& c : cs) alive.emplace_back(c.address, c.kind);
    }
    for (const auto& s : saved) {
        bool ok_alive = false;
        for (const auto& a : alive) {
            if (a.first == s.address && a.second == s.kind) {
                ok_alive = true;
                break;
            }
        }
        if (!ok_alive) {
            ++r.fail;
            log::warnf("가방 복원 건너뜀: 0x{:X} 는 지금 컨테이너가 아니다",
                       s.address);
            continue;
        }
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
