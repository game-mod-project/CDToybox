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
                        int branch, int limit) {
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
    // 여기까지 왔으면 우리가 아는 모양이다. 아래의 건너뜀은 전부 "알아보고 나서
    // 안 건드리기로 한 결정" 이라, 다시 해 본다고 달라지지 않는다.
    p.understood = true;
    int want = target;
    // 종류별 상한이 먼저다. 표에 없거나 이상한 값이면 가장 보수적인 쪽(전체 상한)을
    // 쓴다 - 상한 없는 길을 만들지 않는다.
    if (limit <= 0 || limit > kBagTargetMax) limit = kBagTargetMax;
    if (want > limit) want = limit;
    if (want > kBagTargetMax) want = kBagTargetMax;   // 이중 방어
    if (want > kBagEngineMax) want = kBagEngineMax;   // 삼중 방어
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
    if (!p.apply) {
        p.skip = "이미 그 값이다";
        p.same = true;   // 판정을 문구에서 뗀다(재검토 경미 3)
    }
    return p;
}

namespace {

// 실측 천장(2026-09-13): 가방 240 / 보관함 440 / 9·11 은 300.
// 상한은 아직 **전부 700** 이다 - 보관함류의 엔진 한계를 말한 근거를 못 찾았고,
// 근거 없이 낮추면 기능을 죽이고 근거 없이 올리면 세이브를 죽인다. 조사 결과가
// 나오면 이 표의 숫자만 고치면 된다(구조는 이미 종류별이다).
constexpr BagKindRule kKindRules[] = {
    {1, kBagTargetMax, false, "가방"},
    {7, kBagTargetMax, true, "보관함"},
    {9, kBagTargetMax, true, "종류 9"},
    {11, kBagTargetMax, true, "종류 11"},
};

}  // namespace

std::span<const BagKindRule> bag_kind_rules() { return kKindRules; }

int bag_kind_cap(std::uint16_t kind) {
    for (const auto& r : kKindRules) {
        if (r.kind == kind) return r.cap;
    }
    return 0;
}

bool bag_kind_selected(std::uint16_t kind, bool storage) {
    for (const auto& r : kKindRules) {
        if (r.kind != kind) continue;
        return !r.storage_only || storage;
    }
    return false;
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
                                          branch, bag_kind_cap(c.kind));
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
            // "이미 그 값이다" 는 끝난 것이고, 나머지는 우리가 모양을 못 알아본
            // 것이다. 둘을 한 칸에 섞어 세면 자동 재적용이 전이 상태를 보고도
            // "다 했다" 로 끝낸다.
            if (!p.understood) ++r->unknown;
            r->last_skip = p.skip;   // 화면·로그에 이유를 낸다(리뷰 지적 10)
            log::infof("가방 건너뜀: 종류 {} 0x{:X} - {}", c.kind, c.address,
                       p.skip);
            // **주소만이라도 갱신한다.** "이미 그 값이다" 는 우리가 쓴 값이 세이브에
            // 남아 로드 뒤에도 그대로인 경우의 판정이다 - 그때 주소를 안 고치면
            // 이 기능이 성공했을 때만 되돌리기가 잠긴다(리뷰 치명 1).
            if (remember) {
                seen.understood = p.understood;
                if (p.same) {
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
            seen.understood = p.understood;   // 쓰기로 갔으면 언제나 참이다
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
        std::uint16_t back_cap = 0, back_sum = 0, back_b = 0;
        // **쓴 칸을 전부 되읽는다.** 화면 캐시(+0x14)만 보면 "화면은 300, 확장 칸은
        // 그대로" 인 상태를 성공으로 적게 된다(리뷰 지적 6). +0x16 이 조용히 실패하면
        // cap < sum 인 - plan_bag_expand 가 영원히 거부하는 - 상태가 남는다
        // (재검토 경미 2).
        const bool read_ok = reader.read_value(c.address + 0x14, &back_cap) &&
                             reader.read_value(c.address + 0x16, &back_sum) &&
                             reader.read_value(slot, &back_b);
        if (ok && read_ok &&
            back_cap == static_cast<std::uint16_t>(p.capacity) &&
            back_sum == static_cast<std::uint16_t>(p.sum) &&
            back_b == static_cast<std::uint16_t>(p.expand)) {
            ++r->changed;
            log_write("가방 용량", c.address + 0x14, std::to_string(cap),
                      std::to_string(p.capacity));
            log_write("가방 확장합계", c.address + 0x16, std::to_string(sum),
                      std::to_string(p.sum));
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
            if (remember) {
                // **지금 실제로 어떤 상태인지 되읽어 적는다.** 되돌림이 실패한
                // 경우가 바로 되돌리기가 필요한 상태인데, 예전에는 그때만
                // want 가 0 으로 남아 "무엇을 써 놓았는지 모릅니다" 로 영구히
                // 막혔다 - 버튼은 켜진 채로(재검토 중대 4).
                std::uint16_t now_cap = 0, now_sum = 0;
                if (reader.read_value(c.address + 0x14, &now_cap) &&
                    reader.read_value(c.address + 0x16, &now_sum)) {
                    seen.known = true;
                    seen.want_cap = now_cap;
                    seen.want_sum = now_sum;
                } else if (undo) {
                    // 되읽기는 실패했지만 되돌림은 성공했다 - 지금 값은 손대기 전
                    // 값과 같다. 이 한 줄이 없으면 want 가 0 으로 굳어 되돌리기가
                    // 영구히 막히고, **재기준 가드까지 영영 닫혀** 나중에 사용자가
                    // 확장권을 사면 되돌리기가 옛 원본을 쓴다(게이트 경미 2).
                    seen.known = true;
                    seen.want_cap = cap;
                    seen.want_sum = sum;
                }
                remember_seen(seen);
            }
        }
    }
    if (r->changed > before) ++r->realms;
}

}  // namespace

// ------------------------------------------------- 되돌리기 기록(순수 부분)

void bag_backup_upsert(std::vector<BagBackup>& v, const BagSeen& seen) {
    // **열쇠는 (realm, 종류)뿐이다.** 주소로 먼저 맞춰 보던 것을 뺐다 - 게임이 힙에서
    // 같은 자리를 돌려주면 그 주소에 다른 종류·다른 realm 의 컨테이너가 올 수 있고,
    // 그러면 남의 항목에 want 를 덮어써서 진짜 기록을 잃는다(재검토 중대 2).
    BagBackup* hit = nullptr;
    for (auto& x : v) {
        if (x.realm == seen.realm && x.kind == seen.kind) {
            hit = &x;
            break;
        }
    }
    if (hit != nullptr) {
        hit->address = seen.address;
        // **손대기 전 값이 우리가 써 놓은 값이 아니면 그것이 새 원본이다.**
        // 그 사이 누가 바꿨다는 뜻이고(확장권 구매, 캐릭터 교체, 다른 세이브,
        // 리로드로 우리 값이 사라짐), 되돌아갈 자리는 옛 원본이 아니라 지금 이 값이다.
        // 이게 없으면 자동 재적용이 want 만 새로 덮어써서 혈통 검사가 리로드 한 번으로
        // 무력해지고, 되돌리기가 사용자가 돈 주고 산 칸을 지운다(재검토 치명 1).
        // 우리 값이 그대로 살아 돌아온 경우에는 값이 정확히 같아 다시 잡지 않는다.
        if (seen.understood && hit->want_cap != 0 &&
            (seen.cap != hit->want_cap || seen.sum != hit->want_sum)) {
            hit->cap = seen.cap;
            hit->sum = seen.sum;
            hit->a = seen.a;
            hit->b = seen.b;
        }
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

bool bag_restore_already_original(const BagBackup& s, int cap, int sum, int a,
                                 int b) {
    return cap == static_cast<int>(s.cap) && sum == static_cast<int>(s.sum) &&
           a == static_cast<int>(s.a) && b == static_cast<int>(s.b);
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

namespace {

// 잠금을 **이미 쥔 채** 부른다. 자동 재적용이 "무장됐나" 를 잠금 안에서 다시 보고
// 이어서 쓰려면, 같은 비재귀 뮤텍스를 두 번 잡지 않도록 알맹이가 따로 있어야 한다
// (재검토 경미 1).
BagResult bag_expand_locked(const mem::Reader& reader, int target, bool storage,
                            int branch) {
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

}  // namespace

BagResult bag_expand(const mem::Reader& reader, int target, bool storage,
                     int branch) {
    std::lock_guard<std::mutex> op(g_bag_op_mtx);
    return bag_expand_locked(reader, target, storage, branch);
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
    BagResult r;
    {
        // **잠금을 쥔 채 다시 확인한다.** 검사와 쓰기 사이에 렌더 스레드의
        // 되돌리기가 먼저 끝나면, 방금 되돌린 것을 2초 안에 다시 걸게 된다 -
        // 하필 "로드 직후 곧바로 되돌리기" 라는 가장 흔한 조작과 겹친다
        // (재검토 경미 1).
        std::lock_guard<std::mutex> op(g_bag_op_mtx);
        if (!g_auto_on.load(std::memory_order_acquire)) return;
        r = bag_expand_locked(
            reader, target, g_auto_storage.load(std::memory_order_acquire),
            g_auto_branch.load(std::memory_order_acquire));
    }
    // 아직 끝이 아닌 두 가지. 둘 다 "다음 바퀴에 다시" 가 맞다.
    //   * 대상 컨테이너를 **하나도 못 봤다** - 로드 도중에는 레코드 배열이 아직
    //     0 이라 목록에 아예 안 잡힌다(read_inventory_containers 의 거르개).
    //   * 봤지만 **하나도 못 바꾼 채 모양만 못 알아봤다** - 아직 채워지는 중이다.
    //     실측 2026-09-13: 재탐색 0.001초 뒤에 들어가 서버 realm 4개가 전부 이
    //     이유로 밀렸고(그 바퀴의 changed 는 0), 세대는 소모돼 그 로드에서는 한쪽
    //     realm 에만 걸린 채 끝났다.
    //
    // **changed 를 조건에 섞는 것이 중요하다.** unknown 만 보면, 영영 모양을 모르는
    // 컨테이너가 하나라도 있는 판에서 첫 바퀴에 다 됐는데도 10바퀴를 더 돌고
    // 실패처럼 읽히는 WARN 을 남긴다(리뷰 B-2).
    const bool nothing_seen = r.changed + r.skip == 0;
    const bool still_becoming = r.unknown > 0 && r.changed == 0;
    if (nothing_seen || still_becoming) {
        const int n = g_auto_tries.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (n >= kAutoGiveUp) {
            g_auto_gen.store(gen, std::memory_order_release);
            // **그 바퀴의 수를 그대로 싣는다.** "N바퀴 동안 <마지막 이유>" 는
            // 앞선 바퀴가 다른 이유였을 때 진단을 틀린 데로 끈다 - 이 변경 자체가
            // 로그 한 줄에 속아서 생긴 것이다(리뷰 B-4).
            log::warnf("가방 자동 다시 적용: {}바퀴째 - 바꾼 것 {}, 건너뜀 {},"
                       " 모르는 모양 {} - 이 세대는 포기한다",
                       n, r.changed, r.skip, r.unknown);
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
    bool dropped_now = false;      // 이번에 기록을 버렸나(화면 문구가 갈린다)
    for (const auto& s : saved) {
        const int realm = s.realm == 1 ? 1 : 0;
        if (!readable[realm]) {
            // **버리지 않는다.** 클라 기록은 클라 컴포넌트를 찾은 적이 있을 때만
            // 생기므로, 지금 못 읽는 것은 "영영 없다" 가 아니라 "아직/다시 못
            // 찾았다" 다. 여기서 버리면 곧 다시 잡히는 흔한 경우에 되돌릴 수단만
            // 잃는다(게이트 경미 1 - 앞서 넣었던 포기 판정은 정확히 "찾았다가
            // 잃은" 경우에만 열려, 버리면 안 되는 자리에서만 버렸다).
            ++r.skip;
            r.last_skip = realm == 1
                              ? "클라 인벤토리를 아직 못 찾았습니다 - 잠시 뒤 다시"
                                " 누르십시오"
                              : "지금은 인벤토리를 읽을 수 없습니다 - 잠시 뒤 다시"
                                " 누르십시오";
            keep.push_back(s);
            continue;
        }
        // **지금 살아 있는 컨테이너를 (realm, 종류)로 다시 찾는다.** 주소로만 찾으면,
        // 자동 재적용이 꺼져 있거나 포기한 세대에서는 주소가 낡은 채 남아 되돌리기
        // 한 번에 기록이 통째로 버려진다 - A 칸이 저장에 남은 바로 그 경우에
        // (재검토 중대 3). 낡은 주소에 쓰는 길은 이 조회가 막고(bag_wr16 의 SEH 는
        // 매핑 안 된 페이지만 막지, 재할당된 유효 주소는 조용히 써 버린다 -
        // clan-roster-volatile-writes 와 같은 함정), 엉뚱한 컨테이너에 쓰는 길은
        // 아래 bag_restore_blocked 의 혈통 검사가 막는다.
        const InventoryContainer* now = nullptr;
        for (const auto& c : live[realm]) {
            if (c.kind != s.kind) continue;
            if (now == nullptr) now = &c;
            if (c.address == s.address) {   // 주소까지 같으면 그것이 확실하다
                now = &c;
                break;
            }
        }
        if (now == nullptr) {
            // 죽은 컨테이너는 다시 살아나지 않는다. 남겨 두면 되돌리기가 영구히
            // 잠기므로 실패가 아니라 **건너뜀**으로 세고 버린다(리뷰 재검토 B-1).
            ++r.skip;
            r.last_skip = "인벤토리가 새로 생겨 옛 컨테이너가 없습니다";
            dropped_now = true;
            g_backup_dropped.store(true, std::memory_order_release);
            log::warnf("가방 복원 건너뜀: 0x{:X} 는 지금 컨테이너가 아니다",
                       s.address);
            continue;
        }
        const std::uintptr_t addr = now->address;
        if (addr != s.address) {
            log::infof("가방 복원: 종류 {} 가 0x{:X} 에서 0x{:X} 로 옮겨 갔다",
                       s.kind, s.address, addr);
        }
        std::uint16_t cap = 0, sum = 0, cur_a = 0, cur_b = 0;
        if (!reader.read_value(addr + 0x14, &cap) ||
            !reader.read_value(addr + 0x16, &sum) ||
            !reader.read_value(addr + 0x18, &cur_a) ||
            !reader.read_value(addr + 0x1A, &cur_b)) {
            ++r.fail;
            keep.push_back(s);
            continue;
        }
        if (bag_restore_already_original(s, cap, sum, cur_a, cur_b)) {
            // 되돌릴 것이 없다. 기록을 지워 버튼이 꺼지게 한다 - 남겨 두면 자동
            // 재적용을 끈 사용자가 리로드할 때마다 켜진 버튼과 "적용한 뒤 값이
            // 바뀌었습니다" 를 보게 된다(게이트 경미 5).
            ++r.skip;
            r.last_skip = "이미 원래 값입니다";
            continue;
        }
        if (const char* why =
                bag_restore_blocked(s, cap, sum, static_cast<int>(now->used))) {
            ++r.skip;
            r.last_skip = why;
            keep.push_back(s);
            log::warnf("가방 복원 건너뜀: 0x{:X} - {}", addr, why);
            continue;
        }
        // 쓰기 **전에** 남긴다 - 쓰는 도중 죽어도 무엇을 썼는지 재구성할 수 있게.
        // 확장 칸을 고를 수 있게 된 뒤로는 +0x18 도 우리가 쓰므로 **두 갈래를 다**
        // 되돌린다(리뷰 경미 5: 쓰기 네 번에 로그 한 줄이었다).
        log_write("가방 복원 확장A", addr + 0x18, std::to_string(cur_a),
                  std::to_string(s.a));
        log_write("가방 복원 확장B", addr + 0x1A, std::to_string(cur_b),
                  std::to_string(s.b));
        log_write("가방 복원 확장합계", addr + 0x16, std::to_string(sum),
                  std::to_string(s.sum));
        log_write("가방 복원 용량", addr + 0x14, std::to_string(cap),
                  std::to_string(s.cap));
        // 되돌릴 때도 쓴 순서의 역순으로 - 캐시(+0x14)를 마지막에 맞춘다.
        bool ok = bag_wr16(addr + 0x18, s.a);
        ok = bag_wr16(addr + 0x1A, s.b) && ok;
        ok = bag_wr16(addr + 0x16, s.sum) && ok;
        ok = bag_wr16(addr + 0x14, s.cap) && ok;
        std::uint16_t back_cap = 0, back_sum = 0, back_a = 0, back_b = 0;
        const bool read_ok = reader.read_value(addr + 0x14, &back_cap) &&
                             reader.read_value(addr + 0x16, &back_sum) &&
                             reader.read_value(addr + 0x18, &back_a) &&
                             reader.read_value(addr + 0x1A, &back_b);
        if (ok && read_ok && back_cap == s.cap && back_sum == s.sum &&
            back_a == s.a && back_b == s.b) {
            ++r.changed;
        } else {
            ++r.fail;
            BagBackup again = s;
            again.address = addr;   // 다음 시도는 지금 자리에서
            keep.push_back(again);
            log::warnf("가방 복원 실패: 0x{:X} 종류 {} (쓰기 {}, 되읽기 {})", addr,
                       s.kind, ok ? "성공" : "실패", read_ok ? "성공" : "실패");
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
    // 이번에 버린 것이 없으면 "기록이 사라졌습니다" 는 더 이상 지금 상태가 아니다.
    // 안 내리면 리로드를 한 번 겪은 세션에서 되돌리기에 성공해도 툴팁이 계속 그렇게
    // 말한다(게이트 경미 4).
    if (r.changed > 0 && !dropped_now) {
        g_backup_dropped.store(false, std::memory_order_release);
    }
    log::infof("가방 복원: 되돌린 것 {}개, 건너뜀 {}, 실패 {}", r.changed, r.skip,
               r.fail);
    return r;
}

}  // namespace cdtb::game
