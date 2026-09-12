#include "game/player.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <vector>

#include "core/write_log.h"
#include "game/equip.h"

namespace cdtb::game {
namespace {

constexpr std::size_t kStride = 0x90;
constexpr std::size_t kType = 0x00;   // i32
constexpr std::size_t kMax = 0x18;    // i64 base(=최대)

// 평면 오프셋(게이지 배열 base 기준).
constexpr std::size_t kHpCur = 0x08, kHpMax = 0x18;
constexpr std::size_t kStaCur = 0x6C8, kStaMax = 0x6D8;
constexpr std::size_t kSpiCur = 0x758, kSpiMax = 0x768;

std::atomic<std::uintptr_t> g_arr{0};     // 표시용(주 realm)
std::atomic<std::uintptr_t> g_char{0};    // 고정된 플레이어 char(스티키)
std::mutex g_arrs_mtx;
std::vector<std::uintptr_t> g_arrs;       // freeze 대상: 클라+서버 게이지 배열
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

// 클라+서버 등 플레이어의 모든 realm 게이지 배열을 모은다. 사망 판정은 서버
// 게이지가 권위라, 체력 무적은 양쪽 다 freeze 해야 한다(장비 both-realms 와 같음).
// equip 캐시 테이블(힙 스캔 없음)에서 char_is_player 인 것의 게이지를 수집.
void rebuild_player_arrs(const mem::Reader& reader, std::uintptr_t primary) {
    std::vector<std::uintptr_t> arrs;
    if (primary != 0) arrs.push_back(primary);
    std::vector<EquipTable> tabs;
    equip_tables_copy(&tabs);
    for (const auto& t : tabs) {
        if (t.comp == 0) continue;
        const std::uintptr_t ch = q(reader, t.comp, 0x08);
        if (!char_is_player(reader, ch)) continue;
        const std::uintptr_t a = player_gauge_array(reader, ch);
        if (a == 0) continue;
        bool dup = false;
        for (const auto x : arrs) {
            if (x == a) { dup = true; break; }
        }
        if (!dup) arrs.push_back(a);
    }
    std::lock_guard<std::mutex> lk(g_arrs_mtx);
    g_arrs = std::move(arrs);
}

void player_discover(const mem::Reader& reader) {
    // 스티키: 고정된 char 의 게이지 배열이 아직 유효(Health 게이트)하면 유지.
    const std::uintptr_t cached = g_char.load(std::memory_order_acquire);
    if (cached != 0) {
        const std::uintptr_t arr = player_gauge_array(reader, cached);
        if (arr != 0) {
            g_arr.store(arr, std::memory_order_release);
            rebuild_player_arrs(reader, arr);   // realm 목록 갱신(주소 이동 대비)
            return;
        }
        // 게이트 실패(지역이동·캐릭전환) - 아래에서 다시 잡는다.
    }
    // 힙 스캔 없음: equip 이 이미 고른 플레이어 comp 를 쓴다. NPC 방지로
    // char_is_player 를 한 번 더 확인한 뒤에만 고정한다.
    const std::uintptr_t comp = equip_player_comp();
    if (comp == 0) return;
    const std::uintptr_t ch = q(reader, comp, 0x08);
    if (!char_is_player(reader, ch)) return;   // 아니면 이전 고정 유지
    const std::uintptr_t arr = player_gauge_array(reader, ch);
    g_char.store(ch, std::memory_order_release);
    g_arr.store(arr, std::memory_order_release);
    rebuild_player_arrs(reader, arr);
}

std::uintptr_t player_char() { return g_char.load(std::memory_order_acquire); }

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

void player_set_godmode(bool on) {
    log_write("플레이어 무적", 0,
              g_god.load(std::memory_order_acquire) ? "on" : "off",
              on ? "on" : "off");
    g_god.store(on, std::memory_order_release);
}
void player_set_inf_stamina(bool on) {
    log_write("플레이어 무한 스태미나", 0,
              g_sta.load(std::memory_order_acquire) ? "on" : "off",
              on ? "on" : "off");
    g_sta.store(on, std::memory_order_release);
}
void player_set_inf_spirit(bool on) {
    log_write("플레이어 무한 정신력", 0,
              g_spi.load(std::memory_order_acquire) ? "on" : "off",
              on ? "on" : "off");
    g_spi.store(on, std::memory_order_release);
}
bool player_godmode() { return g_god.load(std::memory_order_acquire); }
bool player_inf_stamina() { return g_sta.load(std::memory_order_acquire); }
bool player_inf_spirit() { return g_spi.load(std::memory_order_acquire); }

void player_apply(const mem::Reader& reader) {
    const bool god = g_god.load(std::memory_order_acquire);
    const bool sta = g_sta.load(std::memory_order_acquire);
    const bool spi = g_spi.load(std::memory_order_acquire);
    if (!god && !sta && !spi) return;

    // 모든 realm(클라+서버)에 freeze. 체력 사망 판정은 서버 게이지가 권위라
    // 한쪽만 쓰면 죽는다. 각 배열은 Health 게이트로 유효성 재확인.
    std::vector<std::uintptr_t> arrs;
    {
        std::lock_guard<std::mutex> lk(g_arrs_mtx);
        arrs = g_arrs;
    }
    for (const auto arr : arrs) {
        if (arr == 0 || ri(reader, arr + kType) != 0) continue;  // 게이트
        if (god) {
            const std::int32_t mx = ri(reader, arr + kHpMax);
            if (mx > 0) wr32(arr + kHpCur, mx);
        }
        if (sta) {
            const std::int32_t mx = ri(reader, arr + kStaMax);
            if (mx > 0) wr32(arr + kStaCur, mx);
        }
        if (spi) {
            const std::int32_t mx = ri(reader, arr + kSpiMax);
            if (mx > 0) wr32(arr + kSpiCur, mx);
        }
    }
}

}  // namespace cdtb::game
