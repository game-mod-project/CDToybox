#include "game/grant.h"

#include <atomic>
#include <cstddef>

#include "core/log.h"
#include "mem/hook.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 함수 앞머리 그대로다. 주소를 박아 두면 패치마다 밀리므로 바이트로
// 찾는다. 둘 다 349MB 이미지 안에서 유일한 것을 확인했다.
constexpr const char* kActorGetterPattern =
    "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41";

constexpr const char* kSpawnGroundPattern =
    "4C 8B DC 49 89 5B 08 49 89 6B 10 56 57 41 54 41 56 41 57 48 81 EC 50 01";

using ActorGetterFn = std::uintptr_t(__fastcall*)(void*);

ActorGetterFn g_orig_actor_getter = nullptr;
void* g_actor_getter_target = nullptr;
std::atomic<std::uintptr_t> g_last_actor{0};
bool g_installed = false;

// 서로 다른 값을 모은다. 훅 안에서 RTTI 를 푸는 것은 너무 비싸므로
// 주소만 적어 두고 나중에 분석 스레드가 클래스를 붙인다.
constexpr int kSeenCap = 16;
std::uintptr_t g_seen[kSeenCap]{};
std::uint32_t g_seen_hits[kSeenCap]{};
std::atomic<int> g_seen_count{0};

// 바닥 스폰. 인자는 전부 포인터다 - 디스어셈블에서 확인했다.
//   rcx 액터  rdx 결과  r8 아이템키  r9 개수  [+0x20] 필드3  [+0x28] 위치
using SpawnFn = void*(__fastcall*)(void*, std::uint32_t*, const std::uint32_t*,
                                   const std::int64_t*, const std::uint16_t*,
                                   const float*);
SpawnFn g_spawn = nullptr;

// 게임의 여러 스레드에서 불린다. 하는 일은 값을 적어 두는 것뿐이다.
std::uintptr_t __fastcall det_actor_getter(void* session) {
    const std::uintptr_t actor = g_orig_actor_getter(session);
    if (actor != 0) {
        g_last_actor.store(actor, std::memory_order_relaxed);
        const int n = g_seen_count.load(std::memory_order_relaxed);
        const int now = note_actor(g_seen, g_seen_hits, n, kSeenCap, actor);
        if (now != n) g_seen_count.store(now, std::memory_order_release);
    }
    return actor;
}

bool find_one(const std::vector<std::uint8_t>& image, const char* pattern,
              std::uint64_t* rva_out) {
    if (rva_out == nullptr || image.empty()) return false;
    const auto parsed = mem::parse_pattern(pattern);
    if (!parsed) return false;

    // 두 개까지만 모은다. 하나를 넘으면 어차피 고를 수 없다.
    const mem::Range range{image.data(), image.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) return false;

    *rva_out = static_cast<std::uint64_t>(hits[0] - image.data());
    return true;
}

}  // namespace

bool find_actor_getter_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kActorGetterPattern, rva_out);
}

bool find_spawn_ground_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kSpawnGroundPattern, rva_out);
}

bool actor_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed) return true;

    std::uint64_t rva = 0;
    if (!find_actor_getter_rva(rtti.image(), &rva)) {
        log::warnf("액터 조회 함수를 찾지 못했다 - 패치로 밀렸을 수 있다");
        return false;
    }
    if (!mem::hook_init()) return false;

    g_actor_getter_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_actor_getter_target, &det_actor_getter,
                           reinterpret_cast<void**>(&g_orig_actor_getter))) {
        log::errorf("액터 조회 후킹 실패 (RVA 0x{:X})", rva);
        g_actor_getter_target = nullptr;
        return false;
    }
    g_installed = true;
    log::infof("액터 조회 후킹 설치 (RVA 0x{:X})", rva);
    return true;
}

void actor_hook_remove() {
    if (!g_installed) return;
    mem::hook_remove(g_actor_getter_target);
    g_actor_getter_target = nullptr;
    g_orig_actor_getter = nullptr;
    g_installed = false;
    log::infof("액터 조회 후킹 원복");
}

bool actor_hook_installed() { return g_installed; }

int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value) {
    if (slots == nullptr || hits == nullptr) return count;
    for (int i = 0; i < count; ++i) {
        if (slots[i] == value) {
            ++hits[i];
            return count;
        }
    }
    if (count >= cap) return count;
    slots[count] = value;
    hits[count] = 1;
    return count + 1;
}

int seen_actors(std::uintptr_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_seen_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_seen[i];
        if (hits_out != nullptr) hits_out[i] = g_seen_hits[i];
    }
    return take;
}

std::uintptr_t last_actor() {
    return g_last_actor.load(std::memory_order_relaxed);
}

bool spawn_args_ok(std::uint32_t item_key, std::int64_t count) {
    return item_key != 0 && count > 0;
}

bool spawn_resolve(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_spawn != nullptr) return true;
    std::uint64_t rva = 0;
    if (!find_spawn_ground_rva(rtti.image(), &rva)) {
        log::warnf("바닥 스폰 함수를 찾지 못했다 - 패치로 밀렸을 수 있다");
        return false;
    }
    g_spawn = reinterpret_cast<SpawnFn>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    log::infof("바닥 스폰 함수 확보 (RVA 0x{:X})", rva);
    return true;
}

bool spawn_item_to_ground(std::uintptr_t actor, std::uint32_t item_key,
                          std::int64_t count, const float pos[3],
                          std::uint32_t* result_out) {
    if (g_spawn == nullptr || pos == nullptr) return false;
    if (!spawn_args_ok(item_key, count)) return false;
    if (actor == 0) actor = last_actor();
    if (actor == 0) return false;

    // 게임은 값이 아니라 포인터를 받는다. 수명이 호출 동안 유지되게
    // 지역에 두고 넘긴다.
    std::uint32_t result = 0;
    std::uint32_t key = item_key;
    std::int64_t n = count;
    std::uint16_t field3 = 0;      // 뜻을 아직 모른다. 0 으로 둔다
    float where[3] = {pos[0], pos[1], pos[2]};

    g_spawn(reinterpret_cast<void*>(actor), &result, &key, &n, &field3, where);
    if (result_out != nullptr) *result_out = result;
    return true;
}

}  // namespace cdtb::game
