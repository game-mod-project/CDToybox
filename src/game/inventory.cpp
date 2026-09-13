#include "game/inventory.h"
#include "core/write_log.h"

#include <vector>

#include <string>

#include <mutex>

#include <windows.h>

#include <atomic>
#include <chrono>
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
// 클라를 못 찾은 채 몇 번 훑었나. 상시 루프가 10초마다 11GB 힙을 영원히 훑는 것을
// 막는다(리뷰 재검토 B-2).
std::atomic<int> g_client_tries{0};
constexpr int kClientGiveUp = 12;
// 캐시한 컴포넌트를 **언제부터** 못 읽고 있나(realm 별, 0 이면 멀쩡하다).
// 분석 스레드 한 곳에서만 읽고 쓴다.
std::chrono::steady_clock::time_point g_dead_since[2];
// 이만큼 계속 못 읽으면 버린다. 로딩 화면을 넉넉히 넘기면서도, 낡은 값을 보여 주는
// 시간이 너무 길어지지 않는 선이다.
constexpr auto kDeadFor = std::chrono::seconds(10);
// 인벤토리 세대. forget_inventory 마다 오른다. 자동 재적용이 "새 인벤토리인가" 를
// **주소로** 판단하면, 힙이 같은 자리를 돌려줬을 때 조용히 안 걸린다.
std::atomic<unsigned> g_inv_gen{0};

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
    // **아직 못 찾은 클래스만** 넣는다. find_objects_of 의 상한은 클래스별이 아니라
    // **전체**이고 낮은 주소부터 채우므로(rtti.h), 두 이름을 늘 함께 넣으면 먼저
    // 잡힌 쪽이 칸을 다 먹어 나머지가 잘린다 - 실측 2026-09-13 에 서버 109개·
    // 클라 95개(합 204)라, 128 로 자르면 클라 95개 중 67개가 잘려 나갔다.
    // 서버를 잡은 뒤에는 128칸이 전부 클라 몫이 된다(리뷰 재검토 B-2).
    std::vector<std::string> want;
    if (!have_server) want.emplace_back(kInventoryClass);
    if (!have_client) want.emplace_back(kInventoryClassClient);
    constexpr std::size_t kFindMax = 128;
    const auto found = rtti.find_objects_of(want, kFindMax);
    if (found.size() >= kFindMax) {
        // 닿으면 진짜 컴포넌트가 잘려 나갈 수 있고, 증상이 "아직 없다" 와 구분되지
        // 않는다(camera.cpp 의 같은 경고와 형태를 맞춘다).
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
    // 클라를 못 찾는 판이 있을 수 있다(내용이 빈 채로만 존재하는 경우 등).
    // 영원히 같은 비용으로 다시 훑지 않도록 몇 번 해 보고 포기한다 - 포기해도
    // 가방 확장은 서버 쪽에만 쓰고 패널이 "한쪽 realm 에만 썼습니다" 를 낸다.
    if (g_component.load(std::memory_order_acquire) != 0 &&
        g_component_client.load(std::memory_order_acquire) == 0) {
        const int n = g_client_tries.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (n == kClientGiveUp) {
            log::warnf("인벤토리 클라 컴포넌트를 {}번 만에 못 찾았다 - 그만 찾는다"
                       " (가방 확장은 서버 realm 에만 간다)", n);
        }
    }
    return g_component.load(std::memory_order_acquire) != 0;
}

namespace {

// 컴포넌트가 아직 유효한가. **"아이템이 들어 있는가" 로 보면 안 된다** - 인벤토리를
// 다 비운 플레이어(또는 다 창고에 넣은 플레이어)와 죽은 포인터가 구분되지 않는다
// (리뷰 중대 3). 살아 있는 컴포넌트는 아이템이 없어도 유효한 컨테이너 배열을 갖는다.
bool component_alive(const mem::Reader& reader, std::uintptr_t comp) {
    // read_inventory_containers 는 배열 포인터가 0 이거나 개수가 0·비정상이면 거짓을
    // 돌려준다 - 실측 2026-09-13 의 죽은 컴포넌트가 정확히 그 모양이었다(배열
    // 0xFFFF, 개수 0). 쓰레기가 우연히 그 검사를 통과해도 컨테이너 목록이 비므로
    // 한 번 더 거른다.
    std::vector<InventoryContainer> cs;
    return read_inventory_containers(reader, comp, &cs) && !cs.empty();
}

// **한 realm 만** 버린다. 클라 미러가 존 전환에서 잠깐 비었다고 멀쩡한 서버
// 컴포넌트까지 잃으면, 그 뒤 10초마다 11GB 힙을 훑는다(리뷰 중대 3).
void drop_realm(int realm) {
    if (realm == 1) {
        g_component_client.store(0, std::memory_order_release);
        g_client_tries.store(0, std::memory_order_release);
    } else {
        g_component.store(0, std::memory_order_release);
    }
    g_inv_gen.fetch_add(1, std::memory_order_acq_rel);
    request_inventory_rescan();
}

}  // namespace

void inventory_check_alive(const mem::Reader& reader) {
    const std::uintptr_t comps[2] = {
        g_component.load(std::memory_order_acquire),
        g_component_client.load(std::memory_order_acquire)};
    const auto now = std::chrono::steady_clock::now();
    for (int realm = 0; realm < 2; ++realm) {
        // 아직 못 찾은 realm 은 탐색이 할 일이다.
        if (comps[realm] == 0 || component_alive(reader, comps[realm])) {
            g_dead_since[realm] = {};
            continue;
        }
        // 로딩 화면에서는 잠깐 안 읽힐 수 있다. **바퀴 수가 아니라 경과 시간**으로
        // 잰다 - 이 루프 한 바퀴는 Sleep(2초)에 더해 액터·명부·장비 탐색까지
        // 도는 시간이라, "3바퀴" 가 실제로 몇 초인지는 상황마다 다르다(리뷰 중대 3).
        if (g_dead_since[realm].time_since_epoch().count() == 0) {
            g_dead_since[realm] = now;
            continue;
        }
        if (now - g_dead_since[realm] < kDeadFor) continue;
        g_dead_since[realm] = {};
        log::warnf("인벤토리 컴포넌트{} 0x{:X} 가 죽었다 - 다시 찾는다",
                   realm == 1 ? "(클라)" : "", comps[realm]);
        drop_realm(realm);
    }
}

bool inventory_both_ready() {
    if (g_component.load(std::memory_order_acquire) == 0) return false;
    if (g_component_client.load(std::memory_order_acquire) != 0) return true;
    // 포기했으면 "다 찾았다" 로 쳐서 상시 루프가 멈추게 한다.
    return g_client_tries.load(std::memory_order_acquire) >= kClientGiveUp;
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
    g_inv_gen.fetch_add(1, std::memory_order_acq_rel);
    // 가방 백업은 **버리지 않는다.** bag_restore 가 "지금 살아 있는 컨테이너인가"
    // 를 직접 검사하므로(낡은 주소에 쓰는 길은 그쪽에서 막힌다), 여기서 선제적으로
    // 버리면 같은 주소로 곧 다시 잡히는 흔한 경우에 되돌릴 수단만 잃는다
    // (리뷰 재검토 B-3).
    g_client_tries.store(0, std::memory_order_release);
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

BagPlan plan_bag_expand(int cap, int sum, int a, int b, int slots, int target,
                        int branch) {
    BagPlan p;
    p.branch = branch == kBagBranchB ? kBagBranchB : kBagBranchA;
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
    // **줄이지 않는다.** 이미 목표보다 큰 칸을 목표까지 깎으면 용량 밖으로
    // 밀려난 아이템이 어떻게 되는지 모른다 - 그건 확장 기능이 할 일이 아니다.
    // 칸을 고를 수 있게 되기 전에는 이 검사가 `want - base - a < 0` 에 우연히
    // 숨어 있었다. A 를 고르면 큰 값이 **우리가 덮어쓸 칸**에 있어 그 우연한
    // 방어가 사라진다(2026-09-13, 시험이 잡았다).
    if (cap > want) {
        p.skip = "이미 목표보다 크다";
        return p;
    }
    // **고른 칸만** 조정하고 반대 칸은 그대로 둔다. 엔진이 합계를 sum 으로
    // 재계산하든 max 로 재계산하든 결과가 want 이하가 된다(max 면 오히려 작아진다).
    // cap <= want 이므로 expand >= (고른 칸의 현재 값) >= 0 이다.
    p.other = p.branch == kBagBranchA ? b : a;
    const int cur = p.branch == kBagBranchA ? a : b;
    p.expand = want - p.base - p.other;
    p.sum = p.other + p.expand;
    p.capacity = p.base + p.sum;
    p.apply = p.capacity != cap || p.expand != cur;
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

std::mutex g_bag_mtx;                    // g_bag_backup 전용
std::vector<BagBackup> g_bag_backup;
// 되돌릴 기록을 버린 적이 있나. "확장한 적이 없습니다" 와 "기록이 사라졌습니다" 는
// 사용자에게 전혀 다른 말이다(리뷰 경미 4).
std::atomic<bool> g_backup_dropped{false};

// **쓰기 전체를 직렬화한다.** 이번 변경으로 bag_expand 가 분석 스레드(자동 재적용)와
// 렌더 스레드(화면 버튼) 양쪽에서 불린다. g_bag_mtx 는 벡터만 지키므로 이것이 없으면
// 되돌리기의 네 쓰기 사이에 자동 재적용이 끼어들어 cap < sum 인 - 우리 모델이 영원히
// "용량이 확장 합계보다 작다" 로 거부하는 - 상태를 만들 수 있다(리뷰 중대 2).
std::mutex g_bag_op_mtx;

// 로드 뒤 자동 다시 적용에 쓰는 설정. 사용자가 적용을 눌렀을 때만 무장된다.
std::atomic<bool> g_auto_on{false};
std::atomic<int> g_auto_target{0};
std::atomic<bool> g_auto_storage{false};
std::atomic<int> g_auto_branch{kBagBranchA};
std::atomic<unsigned> g_auto_gen{0};       // 이 세대에는 이미 했다
std::atomic<unsigned> g_auto_try_gen{0};   // 지금 어느 세대를 두고 재시도 중인가
std::atomic<int> g_auto_tries{0};
constexpr int kAutoGiveUp = 10;            // 이만큼 해 보고 그 세대는 포기한다

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다.
bool bag_wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void remember_seen(const BagSeen& seen) {
    std::lock_guard<std::mutex> lk(g_bag_mtx);
    bag_backup_upsert(g_bag_backup, seen);
}

// 한 컴포넌트의 대상 컨테이너에 계획을 적용한다.
void apply_to(const mem::Reader& reader, std::uintptr_t comp, int target,
              bool storage, bool remember, int branch, int realm, BagResult* r) {
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
                                          static_cast<int>(c.slots), target,
                                          branch);
        BagSeen seen;
        seen.realm = realm;
        seen.kind = c.kind;
        seen.address = c.address;
        seen.cap = cap;
        seen.sum = sum;
        seen.a = a;
        seen.b = b;

        if (!p.apply) {
            ++r->skip;
            r->last_skip = p.skip;   // 화면·로그에 이유를 낸다(리뷰 지적 10)
            log::infof("가방 건너뜀: 종류 {} 0x{:X} - {}", c.kind, c.address,
                       p.skip);
            // **주소만이라도 갱신한다.** "이미 그 값이다" 는 우리가 쓴 값이 세이브에
            // 남아 로드 뒤에도 그대로인 경우의 판정이다 - 그때 주소를 안 고치면
            // 이 기능이 성공했을 때만 되돌리기가 잠긴다(리뷰 치명 1).
            if (remember) {
                if (std::strcmp(p.skip, "이미 그 값이다") == 0) {
                    seen.known = true;
                    seen.want_cap = cap;
                    seen.want_sum = sum;
                }
                remember_seen(seen);
            }
            continue;
        }
        // **쓰기 전에** 기록한다. 쓰는 도중 죽어도 무엇이 원본이었는지는 남는다.
        // 이미 있는 항목이면 주소만 갈아 끼우고 원본 값은 지킨다.
        if (remember) {
            seen.changed = true;
            remember_seen(seen);
        }
        // 고른 칸 -> 합계 -> 캐시 순으로. 반대 칸은 건드리지 않는다.
        // 쓰기 성공을 **버리지 않는다** - 이 기능은 세이브에 값을 박는 유일한
        // 기능이라, 무엇이 실제로 써졌는지 틀리게 적으면 사고 뒤 재구성이 안 된다.
        const std::uintptr_t slot =
            c.address + (p.branch == kBagBranchA ? 0x18 : 0x1A);
        const std::uint16_t was = p.branch == kBagBranchA ? a : b;
        bool ok = bag_wr16(slot, static_cast<std::uint16_t>(p.expand));
        ok = bag_wr16(c.address + 0x16, static_cast<std::uint16_t>(p.sum)) && ok;
        ok = bag_wr16(c.address + 0x14,
                      static_cast<std::uint16_t>(p.capacity)) && ok;
        std::uint16_t back_cap = 0, back_b = 0;
        // **쓴 확장 칸도 되읽는다.** 화면 캐시(+0x14)만 보면 "화면은 300, 확장
        // 칸은 그대로" 인 상태를 성공으로 적게 된다(리뷰 지적 6).
        const bool read_ok = reader.read_value(c.address + 0x14, &back_cap) &&
                             reader.read_value(slot, &back_b);
        if (ok && read_ok &&
            back_cap == static_cast<std::uint16_t>(p.capacity) &&
            back_b == static_cast<std::uint16_t>(p.expand)) {
            ++r->changed;
            log_write("가방 용량", c.address + 0x14, std::to_string(cap),
                      std::to_string(p.capacity));
            log_write(p.branch == kBagBranchA ? "가방 확장칸A" : "가방 확장칸B",
                      slot, std::to_string(was), std::to_string(p.expand));
            if (remember) {
                // 지금 컨테이너가 **우리가 만든 값**임을 확정한다. 되돌리기가
                // 이것과 지금 값을 대조해, 그 사이 누가 바꿨으면 쓰지 않는다.
                seen.known = true;
                seen.want_cap = static_cast<std::uint16_t>(p.capacity);
                seen.want_sum = static_cast<std::uint16_t>(p.sum);
                remember_seen(seen);
            }
        } else {
            // 반쯤 써진 채로 두지 않는다. 남는 값은 전부 목표 이하라 위험하지는
            // 않지만, 사용자가 "왜 이 값인가" 를 알 수 없는 상태가 된다.
            bool undo = bag_wr16(slot, was);
            undo = bag_wr16(c.address + 0x16, sum) && undo;
            undo = bag_wr16(c.address + 0x14, cap) && undo;
            ++r->fail;
            log_write("가방 되돌림", c.address + 0x14,
                      std::to_string(p.capacity), std::to_string(cap));
            log::warnf("가방 확장 실패({}): 0x{:X} 종류 {}",
                       undo ? "되돌림 성공" : "되돌림도 실패", c.address,
                       c.kind);
            if (remember && undo) {
                // 원래 값으로 돌아갔다. 그 사실을 적어 두지 않으면 되돌리기가
                // "적용한 뒤 값이 바뀌었습니다" 로 영영 막힌다.
                seen.known = true;
                seen.want_cap = cap;
                seen.want_sum = sum;
                remember_seen(seen);
            }
        }
    }
    if (r->changed > before) ++r->realms;
}

}  // namespace

// ------------------------------------------------- 되돌리기 기록(순수 부분)

void bag_backup_upsert(std::vector<BagBackup>& v, const BagSeen& seen) {
    BagBackup* hit = nullptr;
    // 주소가 같은 항목이 먼저다. 같은 realm 에 같은 종류가 둘인 인벤토리가 나오더라도
    // 서로의 원본을 바꿔 달지 않는다 - 실측 18개는 종류가 전부 유일하지만 그 가정에
    // 기대지 않는다(리뷰 치명 3).
    for (auto& x : v) {
        if (x.address == seen.address) {
            hit = &x;
            break;
        }
    }
    if (hit == nullptr) {
        for (auto& x : v) {
            if (x.realm == seen.realm && x.kind == seen.kind) {
                hit = &x;
                break;
            }
        }
    }
    if (hit != nullptr) {
        hit->address = seen.address;   // **원본 값(cap/sum/a/b)은 덮지 않는다**
        if (seen.known) {
            hit->want_cap = seen.want_cap;
            hit->want_sum = seen.want_sum;
        }
        return;
    }
    // 우리가 바꾼 적 없는 컨테이너의 되돌리기 기록은 있을 이유가 없다.
    if (!seen.changed) return;
    BagBackup n;
    n.realm = seen.realm;
    n.kind = seen.kind;
    n.address = seen.address;
    n.cap = seen.cap;
    n.sum = seen.sum;
    n.a = seen.a;
    n.b = seen.b;
    if (seen.known) {
        n.want_cap = seen.want_cap;
        n.want_sum = seen.want_sum;
    }
    v.push_back(n);
}

const char* bag_restore_blocked(const BagBackup& s, int cap, int sum, int used) {
    if (cap < 0 || sum < 0 || used < 0) return "값을 읽을 수 없습니다";
    if (s.want_cap == 0) return "무엇을 써 놓았는지 모릅니다";
    // **우리가 써 놓은 값 그대로인가.** 다르면 그 사이 우리가 아닌 누군가가 바꾼
    // 것이다 - 캐릭터 교체, 정당한 확장 구매, 다른 세이브. 그 위에 남의 원본을 쓰면
    // 돈 주고 산 칸을 지우는 일이 된다(리뷰 치명 3).
    if (cap != static_cast<int>(s.want_cap) ||
        sum != static_cast<int>(s.want_sum)) {
        return "적용한 뒤 값이 바뀌었습니다 - 되돌리지 않습니다";
    }
    // 기본 슬롯이 다르면 아예 다른 인벤토리다(값싼 이중 방어).
    if (cap - sum != static_cast<int>(s.cap) - static_cast<int>(s.sum)) {
        return "기본 슬롯이 달라 다른 인벤토리로 보입니다";
    }
    // 확장한 칸을 채운 뒤 되돌리면 용량 밖으로 아이템이 밀려난다. plan_bag_expand 는
    // 같은 연산을 cap > want 로 거부하는데, 복원만 그 원칙 밖에 있었다(리뷰 치명 4).
    if (used > static_cast<int>(s.cap)) {
        return "아이템이 원래 용량보다 많습니다 - 먼저 정리하십시오";
    }
    return nullptr;
}

bool should_auto_reapply(bool on, unsigned gen, unsigned auto_gen,
                         bool both_ready) {
    if (!on) return false;
    if (gen == auto_gen) return false;   // 이 세대에는 이미 했다
    // 클라까지 잡힐 때까지 기다린다 - 서버만 보고 쓰면 한쪽 realm 에만 가고,
    // 그 뒤로는 "이미 했다" 로 표시돼 클라가 영영 안 걸린다.
    return both_ready;
}

// ------------------------------------------------------------- 확장과 복원

BagResult bag_expand(const mem::Reader& reader, int target, bool storage,
                     int branch) {
    std::lock_guard<std::mutex> op(g_bag_op_mtx);
    // **백업을 비우지 않는다.** (realm, 종류)당 첫 원본만 남기므로 여러 번 눌러도
    // 처음 값이 지켜진다(리뷰 지적 3). 비우는 것은 되돌리기 성공뿐이다.
    BagResult r;
    apply_to(reader, inventory_component(), target, storage, true, branch, 0,
             &r);
    apply_to(reader, inventory_component_client(), target, storage, true, branch,
             1, &r);
    // 새 기록이 생겼으면 "기록이 사라졌습니다" 안내는 더 이상 참이 아니다.
    if (r.changed > 0) g_backup_dropped.store(false, std::memory_order_release);
    log::infof("가방 확장: 목표 {} 칸 {} -> 바꾼 것 {}개({} realm), 건너뜀 {},"
               " 실패 {}", target,
               branch == kBagBranchB ? "B(+0x1A)" : "A(+0x18)", r.changed,
               r.realms, r.skip, r.fail);
    return r;
}

void bag_auto_set(int target, bool storage, int branch) {
    g_auto_target.store(target, std::memory_order_release);
    g_auto_storage.store(storage, std::memory_order_release);
    g_auto_branch.store(branch, std::memory_order_release);
    // 지금 세대에는 방금 손으로 걸었다. 이 다음 **새로 생긴** 것부터가 대상이다.
    g_auto_gen.store(g_inv_gen.load(std::memory_order_acquire), std::memory_order_release);
    g_auto_tries.store(0, std::memory_order_release);
    g_auto_on.store(true, std::memory_order_release);
}

void bag_auto_clear() { g_auto_on.store(false, std::memory_order_release); }

bool bag_auto_on() { return g_auto_on.load(std::memory_order_acquire); }

void bag_auto_tick(const mem::Reader& reader) {
    const unsigned gen = g_inv_gen.load(std::memory_order_acquire);
    if (!should_auto_reapply(g_auto_on.load(std::memory_order_acquire), gen,
                             g_auto_gen.load(std::memory_order_acquire),
                             inventory_both_ready())) {
        return;
    }
    if (g_auto_try_gen.exchange(gen, std::memory_order_acq_rel) != gen) {
        g_auto_tries.store(0, std::memory_order_release);
    }
    const int target = g_auto_target.load(std::memory_order_acquire);
    const BagResult r =
        bag_expand(reader, target, g_auto_storage.load(std::memory_order_acquire),
                   g_auto_branch.load(std::memory_order_acquire));
    if (r.changed + r.skip == 0) {
        // 대상 컨테이너를 **하나도 못 봤다**. 로드 도중에는 가방 컨테이너의 레코드
        // 배열이 아직 0 이라 목록에 아예 안 잡힌다(read_inventory_containers 의
        // records == 0 거르개). 여기서 세대를 소모하면 그 로드에서는 영영 안 걸린다
        // (리뷰 중대 1).
        const int n = g_auto_tries.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (n >= kAutoGiveUp) {
            g_auto_gen.store(gen, std::memory_order_release);
            log::warnf("가방 자동 다시 적용: {}바퀴 동안 가방 컨테이너를 못 봤다"
                       " - 이 세대는 포기한다", n);
        }
        return;
    }
    g_auto_gen.store(gen, std::memory_order_release);
    log::infof("인벤토리가 새로 생겼다 - 가방 확장을 다시 걸었다 (목표 {}, 바꾼 것"
               " {}개, 건너뜀 {})", target, r.changed, r.skip);
}

bool bag_has_backup() {
    std::lock_guard<std::mutex> lk(g_bag_mtx);
    return !g_bag_backup.empty();
}

bool bag_backup_dropped() {
    return g_backup_dropped.load(std::memory_order_acquire);
}

BagResult bag_restore(const mem::Reader& reader) {
    std::lock_guard<std::mutex> op(g_bag_op_mtx);
    BagResult r;
    // **누른 사실 자체로 무장을 푼다.** 사용자가 "그만" 이라고 말한 것이다. 예전에는
    // 하나라도 되돌려야 풀려서, 전부 건너뛴 경우 백업만 사라지고 자동 쓰기는
    // 계속됐다 - 화면 툴팁의 약속과 정반대였다(리뷰 치명 2).
    bag_auto_clear();

    std::vector<BagBackup> saved;
    {
        std::lock_guard<std::mutex> lk(g_bag_mtx);
        saved = g_bag_backup;
    }
    if (saved.empty()) {
        r.last_skip = "되돌릴 기록이 없습니다";
        return r;
    }

    // realm 별로 지금 살아 있는 컨테이너를 읽는다. **"아직 못 읽는다" 와 "새로
    // 생겼다" 를 가른다** - 예전에는 둘을 구분하지 않고 백업을 통째로 버려, 로드
    // 직후(생존 검사가 알아채기 전에) 되돌리기를 누르면 되돌릴 수단이 영영
    // 사라졌다. 그것이 가장 자연스러운 조작이다(리뷰 치명 2).
    bool readable[2] = {false, false};
    std::vector<InventoryContainer> live[2];
    const std::uintptr_t comps[2] = {inventory_component(),
                                     inventory_component_client()};
    for (int i = 0; i < 2; ++i) {
        if (comps[i] == 0) continue;
        readable[i] = read_inventory_containers(reader, comps[i], &live[i]);
    }

    std::vector<BagBackup> keep;   // 되돌리지 못했고 아직 가망이 있는 것
    for (const auto& s : saved) {
        const int realm = s.realm == 1 ? 1 : 0;
        if (!readable[realm]) {
            ++r.skip;
            r.last_skip = "지금은 인벤토리를 읽을 수 없습니다 - 잠시 뒤 다시"
                          " 누르십시오";
            keep.push_back(s);   // **버리지 않는다**
            continue;
        }
        // **지금 살아 있는 컨테이너만 되돌린다.** 게임이 인벤토리를 새로 만들면
        // 백업의 주소는 남의 객체가 된다. bag_wr16 의 SEH 는 매핑 안 된 페이지만
        // 막지, 재할당된 유효 주소는 조용히 써 버린다
        // (clan-roster-volatile-writes 와 같은 함정, 리뷰 지적 1).
        const InventoryContainer* now = nullptr;
        for (const auto& c : live[realm]) {
            if (c.address == s.address && c.kind == s.kind) {
                now = &c;
                break;
            }
        }
        if (now == nullptr) {
            // 죽은 컨테이너는 다시 살아나지 않는다. 남겨 두면 되돌리기가 영구히
            // 잠기므로 실패가 아니라 **건너뜀**으로 세고 버린다(리뷰 재검토 B-1).
            ++r.skip;
            r.last_skip = "인벤토리가 새로 생겨 옛 컨테이너가 없습니다";
            g_backup_dropped.store(true, std::memory_order_release);
            log::warnf("가방 복원 건너뜀: 0x{:X} 는 지금 컨테이너가 아니다",
                       s.address);
            continue;
        }
        std::uint16_t cap = 0, sum = 0, cur_a = 0, cur_b = 0;
        if (!reader.read_value(s.address + 0x14, &cap) ||
            !reader.read_value(s.address + 0x16, &sum) ||
            !reader.read_value(s.address + 0x18, &cur_a) ||
            !reader.read_value(s.address + 0x1A, &cur_b)) {
            ++r.fail;
            keep.push_back(s);
            continue;
        }
        if (const char* why =
                bag_restore_blocked(s, cap, sum, static_cast<int>(now->used))) {
            ++r.skip;
            r.last_skip = why;
            keep.push_back(s);
            log::warnf("가방 복원 건너뜀: 0x{:X} - {}", s.address, why);
            continue;
        }
        // 쓰기 **전에** 남긴다 - 쓰는 도중 죽어도 무엇을 썼는지 재구성할 수 있게.
        // 확장 칸을 고를 수 있게 된 뒤로는 +0x18 도 우리가 쓰므로 **두 갈래를 다**
        // 되돌린다(리뷰 경미 5: 쓰기 네 번에 로그 한 줄이었다).
        log_write("가방 복원 확장A", s.address + 0x18, std::to_string(cur_a),
                  std::to_string(s.a));
        log_write("가방 복원 확장B", s.address + 0x1A, std::to_string(cur_b),
                  std::to_string(s.b));
        log_write("가방 복원 용량", s.address + 0x14, std::to_string(cap),
                  std::to_string(s.cap));
        // 되돌릴 때도 쓴 순서의 역순으로 - 캐시(+0x14)를 마지막에 맞춘다.
        bool ok = bag_wr16(s.address + 0x18, s.a);
        ok = bag_wr16(s.address + 0x1A, s.b) && ok;
        ok = bag_wr16(s.address + 0x16, s.sum) && ok;
        ok = bag_wr16(s.address + 0x14, s.cap) && ok;
        std::uint16_t back_cap = 0, back_a = 0, back_b = 0;
        const bool read_ok = reader.read_value(s.address + 0x14, &back_cap) &&
                             reader.read_value(s.address + 0x18, &back_a) &&
                             reader.read_value(s.address + 0x1A, &back_b);
        if (ok && read_ok && back_cap == s.cap && back_a == s.a &&
            back_b == s.b) {
            ++r.changed;
        } else {
            ++r.fail;
            keep.push_back(s);   // 이것만 다시 해 볼 여지가 있다
            log::warnf("가방 복원 실패: 0x{:X} 종류 {} (쓰기 {}, 되읽기 {})",
                       s.address, s.kind, ok ? "성공" : "실패",
                       read_ok ? "성공" : "실패");
        }
    }
    {
        // 되돌린 것과 죽은 것은 지우고, 실패한 것과 아직 못 읽은 것만 남긴다.
        // 예전에는 하나라도 실패하면 통째로 남겨 되돌리기가 영구히 잠겼다
        // (리뷰 재검토 B-1). 쓰기 뮤텍스가 이 구간 전체를 감싸므로 그 사이
        // apply_to 가 넣은 항목을 날리는 일(lost update)은 없다(리뷰 중대 2).
        std::lock_guard<std::mutex> lk(g_bag_mtx);
        g_bag_backup = keep;
    }
    log::infof("가방 복원: 되돌린 것 {}개, 건너뜀 {}, 실패 {}", r.changed, r.skip,
               r.fail);
    return r;
}

}  // namespace cdtb::game
