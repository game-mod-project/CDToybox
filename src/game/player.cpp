#include "game/player.h"

#include <windows.h>

#include <atomic>
#include <vector>

#include "game/equip.h"

namespace cdtb::game {
namespace {

constexpr std::size_t kStride = 0x90;
constexpr std::size_t kType = 0x00;   // i32
constexpr std::size_t kCur = 0x08;    // i64 (하위 i32 만 쓴다)
constexpr std::size_t kMax = 0x18;    // i64 base(=최대)

// 평면 오프셋(게이지 배열 base 기준).
constexpr std::size_t kHpCur = 0x08, kHpMax = 0x18;
constexpr std::size_t kStaCur = 0x6C8, kStaMax = 0x6D8;
constexpr std::size_t kSpiCur = 0x758, kSpiMax = 0x768;

std::atomic<std::uintptr_t> g_arr{0};
std::atomic<std::uintptr_t> g_char{0};   // 고정된 플레이어 char(스티키)
std::atomic<std::uintptr_t> g_comp{0};   // 그 char 의 장비 컴포넌트
std::atomic<bool> g_god{false};
std::atomic<bool> g_sta{false};
std::atomic<bool> g_spi{false};

bool vp(std::uintptr_t p) {
    return p > 0x100000000ULL && p < 0x7FFFFFFFFFFFULL;
}
std::uintptr_t q(const mem::Reader& r, std::uintptr_t a, std::size_t o) {
    std::uint64_t v = 0;
    return r.read_value(a + o, &v) ? static_cast<std::uintptr_t>(v) : 0;
}
std::int32_t ri(const mem::Reader& r, std::uintptr_t a) {
    std::int32_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

// 인프로세스 i32 쓰기(주입 DLL 전용). SEH.
bool wr32(std::uintptr_t a, std::int32_t v) {
    __try {
        *reinterpret_cast<volatile std::int32_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::uintptr_t player_gauge_array(const mem::Reader& reader, std::uintptr_t ch) {
    if (!vp(ch)) return 0;
    const std::uintptr_t actor = q(reader, ch, 0x68);
    const std::uintptr_t mark = q(reader, actor, 0x20);
    const std::uintptr_t root = q(reader, mark, 0x18);
    const std::uintptr_t arr = q(reader, root, 0x58);
    if (!vp(arr)) return 0;
    if (ri(reader, arr + kType) != 0) return 0;  // Health 게이트
    return arr;
}

bool char_is_player(const mem::Reader& reader, std::uintptr_t ch) {
    const std::uintptr_t arr = player_gauge_array(reader, ch);
    if (arr == 0) return false;
    // 정신력 풀(type 21 또는 23, max>0)이 있어야 플레이어. 적/NPC 는 없다.
    for (int k = 0; k < 64; ++k) {
        const std::uintptr_t e = arr + static_cast<std::uintptr_t>(k) * kStride;
        const std::int32_t ty = ri(reader, e + kType);
        if (ty < 0 || ty > 4096) break;
        if (ty == 21 || ty == 23) {
            std::int64_t mx = 0;
            if (reader.read_value(e + kMax, &mx) && mx > 0) return true;
        }
    }
    return false;
}

void player_discover(const mem::Rtti& rtti, const mem::Reader& reader) {
    // 스티키: 고정된 char 의 게이지 배열이 아직 유효(Health 게이트)하면 유지.
    // Health 게이트만 보므로 정신력 스캔이 순간 실패해도 안 흔들린다.
    const std::uintptr_t cached = g_char.load(std::memory_order_acquire);
    if (cached != 0) {
        const std::uintptr_t arr = player_gauge_array(reader, cached);
        if (arr != 0) {
            g_arr.store(arr, std::memory_order_release);
            return;
        }
        // 게이트 실패(지역이동·캐릭전환) - 아래에서 재탐색.
    }

    // 재탐색(스티키 무효 시에만, 힙 스캔): 정신력 풀을 가진 char 중 **착용 조각이
    // 가장 많은 것** = 로컬 플레이어(동행 11 < 플레이어 18). 조각 수가 유일한
    // 로컬-플레이어 신호다(동행도 정신력 풀은 있다).
    const auto objs = rtti.find_objects("EquipSlotActorComponent", 512);
    std::uintptr_t best_char = 0, best_comp = 0;
    int best_pieces = 0;
    std::vector<WornPiece> tmp;
    for (const auto& o : objs) {
        const std::uintptr_t ch = q(reader, o.address, 0x08);
        if (!char_is_player(reader, ch)) continue;
        EquipTable t;
        if (!find_equip_table(reader, o.address, &t)) continue;
        if (!read_worn_gear(reader, t, &tmp)) continue;
        const int pieces = static_cast<int>(tmp.size());
        if (pieces > best_pieces) {
            best_pieces = pieces;
            best_char = ch;
            best_comp = o.address;
        }
    }
    if (best_char != 0) {
        g_char.store(best_char, std::memory_order_release);
        g_comp.store(best_comp, std::memory_order_release);
        g_arr.store(player_gauge_array(reader, best_char),
                    std::memory_order_release);
    }
    // 못 찾으면 이전 고정 유지(g_char 그대로).
}

std::uintptr_t player_char() { return g_char.load(std::memory_order_acquire); }
std::uintptr_t player_comp() { return g_comp.load(std::memory_order_acquire); }

bool player_ready() {
    return g_arr.load(std::memory_order_acquire) != 0;
}

PlayerVitals player_vitals(const mem::Reader& reader) {
    PlayerVitals v;
    const std::uintptr_t arr = g_arr.load(std::memory_order_acquire);
    if (arr == 0 || ri(reader, arr + kType) != 0) return v;  // 게이트 재검증
    v.hp_cur = ri(reader, arr + kHpCur);
    v.hp_max = ri(reader, arr + kHpMax);
    v.sta_cur = ri(reader, arr + kStaCur);
    v.sta_max = ri(reader, arr + kStaMax);
    v.spi_cur = ri(reader, arr + kSpiCur);
    v.spi_max = ri(reader, arr + kSpiMax);
    v.ok = true;
    return v;
}

void player_set_godmode(bool on) { g_god.store(on, std::memory_order_release); }
void player_set_inf_stamina(bool on) { g_sta.store(on, std::memory_order_release); }
void player_set_inf_spirit(bool on) { g_spi.store(on, std::memory_order_release); }
bool player_godmode() { return g_god.load(std::memory_order_acquire); }
bool player_inf_stamina() { return g_sta.load(std::memory_order_acquire); }
bool player_inf_spirit() { return g_spi.load(std::memory_order_acquire); }

void player_apply(const mem::Reader& reader) {
    const std::uintptr_t arr = g_arr.load(std::memory_order_acquire);
    if (arr == 0) return;
    if (ri(reader, arr + kType) != 0) {   // 게이트 깨짐(전환/이동) - 재발견 대기
        g_arr.store(0, std::memory_order_release);
        return;
    }
    // 현재=최대. CDR 과 동일. 최대가 0/음수면 건드리지 않는다.
    if (g_god.load(std::memory_order_acquire)) {
        const std::int32_t mx = ri(reader, arr + kHpMax);
        if (mx > 0) wr32(arr + kHpCur, mx);
    }
    if (g_sta.load(std::memory_order_acquire)) {
        const std::int32_t mx = ri(reader, arr + kStaMax);
        if (mx > 0) wr32(arr + kStaCur, mx);
    }
    if (g_spi.load(std::memory_order_acquire)) {
        const std::int32_t mx = ri(reader, arr + kSpiMax);
        if (mx > 0) wr32(arr + kSpiCur, mx);
    }
}

}  // namespace cdtb::game
