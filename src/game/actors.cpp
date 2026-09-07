#include "game/actors.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "core/log.h"
#include "game/roster.h"

namespace cdtb::game {
namespace {

constexpr const char* kActorManagerClass = ".?AVClientActorManager@pa@@";

// 액터 -> 캐릭터 행 번호 사슬 (RVA 0x17544D0 그대로)
constexpr std::size_t kActorHolder = 0x68;
constexpr std::size_t kHolderInfo = 0x20;
constexpr std::size_t kInfoRow = 0x30;

bool read_bucket(const mem::Reader& r, std::uintptr_t at, std::uintptr_t* arr,
                 std::uint32_t* count, std::uint32_t* cap) {
    std::uint64_t p = 0;
    if (!r.read_value(at, &p)) return false;
    if (!r.read_value(at + 8, count)) return false;
    if (!r.read_value(at + 12, cap)) return false;
    *arr = static_cast<std::uintptr_t>(p);
    return true;
}

bool bucket_plausible(std::uintptr_t arr, std::uint32_t count, std::uint32_t cap) {
    if (arr == 0) return false;
    if (cap == 0 || cap > kActorBucketMaxCap) return false;
    return count <= cap;
}

// 힙 포인터로 보이는가. 배열의 끝은 0 이나 쓰레기 값이다.
bool plausible_actor_ptr(std::uint64_t p) {
    return p > 0x10000 && p < 0x7FFFFFFFFFFFull && (p & 0x7) == 0;
}

}  // namespace

bool looks_like_actor_manager(const mem::Reader& reader, std::uintptr_t manager) {
    if (manager == 0) return false;
    for (std::size_t off = kActorBucketFirst; off <= kActorBucketLast;
         off += kActorBucketStride) {
        std::uintptr_t arr = 0;
        std::uint32_t count = 0, cap = 0;
        if (!read_bucket(reader, manager + off, &arr, &count, &cap)) return false;
        if (!bucket_plausible(arr, count, cap)) continue;
        std::uint64_t first = 0;
        if (reader.read_value(arr, &first) && plausible_actor_ptr(first)) return true;
    }
    return false;
}

// 아래에 정의돼 있다. 매니저 고르기에서 먼저 쓴다.
bool walk_actor_pointers(const mem::Reader& reader, std::uintptr_t manager,
                         std::vector<std::uintptr_t>* out);

bool find_actor_manager(const mem::Reader& reader, const mem::Rtti& rtti,
                        std::uintptr_t* out) {
    if (out == nullptr) return false;
    // 첫 번째로 그럴듯한 것을 집으면 안 된다. 매니저는 여러 개 살아
    // 있고(메인 화면 것이 남아 있기도 한다) 그중 빈 것을 집으면 근처
    // 목록이 계속 비어 보인다 - 실측 2026-09-07: 월드 진입 전에 잡은
    // 매니저를 그대로 물고 있어 액터가 1개로 나왔다(직전 세션 1703).
    // 액터를 가장 많이 들고 있는 것을 고른다.
    std::uintptr_t best = 0;
    std::size_t best_n = 0;
    for (const auto addr : rtti.instances_of_class(kActorManagerClass, 8)) {
        if (!looks_like_actor_manager(reader, addr)) continue;
        std::vector<std::uintptr_t> ptrs;
        if (!walk_actor_pointers(reader, addr, &ptrs)) continue;
        if (best == 0 || ptrs.size() > best_n) {
            best = addr;
            best_n = ptrs.size();
        }
    }
    if (best == 0) return false;
    *out = best;
    return true;
}

bool walk_actor_pointers(const mem::Reader& reader, std::uintptr_t manager,
                         std::vector<std::uintptr_t>* out) {
    if (out == nullptr || manager == 0) return false;
    std::vector<std::uintptr_t> found;
    for (std::size_t off = kActorBucketFirst; off <= kActorBucketLast;
         off += kActorBucketStride) {
        std::uintptr_t arr = 0;
        std::uint32_t count = 0, cap = 0;
        if (!read_bucket(reader, manager + off, &arr, &count, &cap)) continue;
        if (!bucket_plausible(arr, count, cap)) continue;
        // 개수 필드(+8)는 살아 있는 수가 아니다(실측: 130 이었다가 0 이 되는
        // 동안 배열은 그대로 212개). 배열은 빽빽한 포인터 목록이고 끝은
        // 0 이나 쓰레기 값이므로 용량까지 그렇게 걷는다.
        for (std::uint32_t i = 0; i < cap; ++i) {
            std::uint64_t p = 0;
            if (!reader.read_value(arr + static_cast<std::uintptr_t>(i) * 8, &p)) {
                break;
            }
            if (!plausible_actor_ptr(p)) break;
            found.push_back(static_cast<std::uintptr_t>(p));
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    *out = std::move(found);
    return true;
}

bool actor_character_row(const mem::Reader& reader, std::uintptr_t actor,
                         std::uint16_t* row_out) {
    if (actor == 0 || row_out == nullptr) return false;
    std::uint64_t holder = 0, info = 0;
    if (!reader.read_value(actor + kActorHolder, &holder) || holder == 0) return false;
    if (!reader.read_value(static_cast<std::uintptr_t>(holder) + kHolderInfo, &info) ||
        info == 0) {
        return false;
    }
    std::uint16_t row = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(info) + kInfoRow, &row)) {
        return false;
    }
    *row_out = row;
    return true;
}


bool read_actor_handles(const mem::Reader& reader, std::uintptr_t manager,
                        std::vector<std::pair<std::uintptr_t, std::uint32_t>>* out) {
    if (out == nullptr || manager == 0) return false;
    std::uint64_t container = 0;
    if (!reader.read_value(manager + kActorContainerOff, &container) ||
        container == 0) {
        return false;
    }
    const auto c = static_cast<std::uintptr_t>(container);
    std::uint32_t nbuckets = 0;
    std::uint64_t buckets = 0, nodes = 0;
    if (!reader.read_value(c + kContainerBucketCount, &nbuckets)) return false;
    if (!reader.read_value(c + kContainerBuckets, &buckets)) return false;
    if (!reader.read_value(c + kContainerNodes, &nodes)) return false;
    if (nbuckets == 0 || nbuckets > kMaxBuckets || buckets == 0 || nodes == 0) {
        return false;
    }
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> found;
    for (std::uint32_t b = 0; b < nbuckets; ++b) {
        const std::uintptr_t at =
            static_cast<std::uintptr_t>(buckets) + b * kBucketStride;
        std::uint32_t count = 0;
        if (!reader.read_value(at, &count)) continue;
        // 버킷 하나에 담기는 수는 작다. 이상하면 건너뛴다.
        if (count > (kBucketStride - 8) / 8) continue;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint32_t key = 0, idx = 0;
            if (!reader.read_value(at + 8 + i * 8, &key)) break;
            if (!reader.read_value(at + 8 + i * 8 + 4, &idx)) break;
            std::uint64_t node = 0;
            if (!reader.read_value(static_cast<std::uintptr_t>(nodes) +
                                       static_cast<std::uintptr_t>(idx) * 8,
                                   &node) ||
                node == 0) {
                continue;
            }
            std::uint32_t node_key = 0;
            std::uint64_t actor = 0;
            if (!reader.read_value(static_cast<std::uintptr_t>(node) + 4,
                                   &node_key) ||
                node_key != key) {
                continue;
            }
            if (!reader.read_value(static_cast<std::uintptr_t>(node) + 8,
                                   &actor) ||
                actor == 0) {
                continue;
            }
            found.emplace_back(static_cast<std::uintptr_t>(actor), key);
        }
    }
    *out = std::move(found);
    return true;
}

bool snapshot_live_actors(const mem::Reader& reader, std::uintptr_t manager,
                          std::vector<LiveActor>* out) {
    if (out == nullptr) return false;
    std::vector<std::uintptr_t> ptrs;
    if (!walk_actor_pointers(reader, manager, &ptrs)) return false;
    std::vector<std::pair<std::uintptr_t, std::uint32_t>> handles;
    read_actor_handles(reader, manager, &handles);
    std::vector<LiveActor> list;
    list.reserve(ptrs.size());
    for (const auto a : ptrs) {
        LiveActor la;
        la.actor = a;
        for (const auto& hp : handles) {
            if (hp.first == a) { la.handle = hp.second; break; }
        }
        std::uint16_t row = 0;
        if (actor_character_row(reader, a, &row)) {
            la.row = row;
            if (const RosterEntry* e = character_by_row(row)) {
                la.key = e->key;
                la.name = e->name;
                la.label = e->label;
                la.merc_row = e->merc_row;
                la.hirable = e->hirable;
            }
        }
        list.push_back(std::move(la));
    }
    *out = std::move(list);
    return true;
}

// --------------------------------------------------- 모드용 캐시

namespace {

std::atomic<std::uintptr_t> g_manager{0};
// 다시 찾기에 쓴다. 처음 발견에 쓴 것을 그대로 들고 있는다.
const mem::Rtti* g_rtti = nullptr;
// 살아 있는 월드에서 이보다 적으면 매니저를 잘못 잡은 것으로 본다.
constexpr std::size_t kMinPlausibleActors = 8;
// 목록은 그리는 스레드만 만들고 읽는다(패널이 버튼/주기로 refresh 를
// 부르고 같은 프레임에서 그린다). 다른 스레드가 읽지 않으므로 판을
// 겹쳐 둘 필요가 없다.
std::vector<LiveActor> g_live;

}  // namespace

bool discover_actor_manager(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_manager.load(std::memory_order_acquire) != 0) return true;
    std::uintptr_t m = 0;
    if (!find_actor_manager(reader, rtti, &m)) return false;
    g_manager.store(m, std::memory_order_release);
    g_rtti = &rtti;
    log::infof("액터 매니저 0x{:X} - 근처 목록 준비됨", m);
    return true;
}

bool actor_manager_ready() { return g_manager.load(std::memory_order_acquire) != 0; }

bool refresh_live_actors(const mem::Reader& reader) {
    std::uintptr_t m = g_manager.load(std::memory_order_acquire);
    if (m == 0) return false;
    std::vector<LiveActor> list;
    if (!snapshot_live_actors(reader, m, &list)) return false;

    // 잡아 둔 매니저가 말라붙었으면 다시 찾는다. 월드 진입 전에 잡으면
    // 그 뒤로 영영 비어 보인다. 살아 있는 월드에서 액터가 한 자릿수인
    // 경우는 없다.
    if (list.size() < kMinPlausibleActors && g_rtti != nullptr) {
        std::uintptr_t again = 0;
        if (find_actor_manager(reader, *g_rtti, &again) && again != 0 &&
            again != m) {
            std::vector<LiveActor> better;
            if (snapshot_live_actors(reader, again, &better) &&
                better.size() > list.size()) {
                g_manager.store(again, std::memory_order_release);
                log::infof("액터 매니저를 0x{:X} 로 바꿨다 - 잡아 둔 것이 "
                           "비어 있었다({}개 -> {}개)",
                           again, list.size(), better.size());
                list.swap(better);
            }
        }
    }

    g_live.swap(list);
    return true;
}

const std::vector<LiveActor>& live_actors() { return g_live; }

}  // namespace cdtb::game
