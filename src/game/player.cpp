#include "game/player.h"
#include "game/actors.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <chrono>
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
// 게이지 배열을 **언제부터** 못 읽고 있나(0 이면 멀쩡하다). 분석 스레드 한
// 곳에서만 읽고 쓴다.
std::chrono::steady_clock::time_point g_dead_since;
constexpr auto kPlayerDeadFor = std::chrono::seconds(10);
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

// char -> [+0x68] actor -> [+0x20] marker -> [+0x18] root. 게이지 배열은 root+0x58.
std::uintptr_t char_root(const mem::Reader& reader, std::uintptr_t ch) {
    if (!vp(ch)) return 0;
    const std::uintptr_t actor = q(reader, ch, 0x68);
    const std::uintptr_t mark = q(reader, actor, 0x20);
    return q(reader, mark, 0x18);
}

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
    // 힙 스캔 없음: equip 이 고른 플레이어 comp(장비 창의 캐릭터 선택을 따른다)를 쓴다.
    // NPC 방지로 char_is_player 를 한 번 더 확인한다.
    const std::uintptr_t comp = equip_player_comp();
    const std::uintptr_t want = comp != 0 ? q(reader, comp, 0x08) : 0;
    const bool want_ok = want != 0 && char_is_player(reader, want);
    // 스티키: 고정된 char 의 게이지 배열이 아직 유효(Health 게이트)하면 유지 - 단 장비 쪽이
    // **다른** 캐릭터를 골랐으면(웅카로 바꿈) 그쪽으로 옮긴다. freeze 대상은 어차피 플레이어형
    // 전원의 realm 배열이라(rebuild_player_arrs) 옮겨가는 것은 표시용 주 배열(g_arr)과
    // 낙사 root 계산의 게이트(nofall_refresh 가 player_char() 를 본다)다 - 고른 캐릭터가 곧 조종
    // 중인 캐릭터일 때 오히려 맞는 방향이다(리뷰 E-3).
    const std::uintptr_t cached = g_char.load(std::memory_order_acquire);
    if (cached != 0 && (!want_ok || want == cached)) {
        const std::uintptr_t arr = player_gauge_array(reader, cached);
        if (arr != 0) {
            // **유예 시계를 여기서도 비운다.** 안 비우면 지역 이동 ①에서 켜진 시계가
            // 그대로 남아, 한참 뒤 지역 이동 ②의 0.1초짜리 끊김에 **즉시** 10초를
            // 넘긴 것으로 판정돼 멀쩡한 캐릭터를 버린다 - 유예를 넣은 목적 자체가
            // 그 한 번의 깜빡임을 막는 것이었다.
            g_dead_since = {};
            g_arr.store(arr, std::memory_order_release);
            rebuild_player_arrs(reader, arr);   // realm 목록 갱신(주소 이동 대비)
            return;
        }
        // 게이트 실패(지역이동·캐릭전환) - 아래에서 다시 잡는다.
    }
    if (!want_ok) {
        // **죽은 채로 붙들지 않는다.** 예전에는 여기서 그냥 돌아가, 리로드로 장비와
        // 게이지가 함께 죽으면 g_arr 가 죽은 주소를 든 채 player_ready() 가 참으로
        // 남았다. 그래서 화면이 "아직 안 잡혔습니다" 대신 **-1 / -1** 을 그렸다
        // (사용자 화면 확인 2026-09-13).
        //
        // 지역 이동 중에는 잠깐 못 읽을 수 있으니 경과 시간으로 잰다. 비우면
        // player_ready() 가 거짓이 되어 화면이 정직해지고, 장비가 다시 잡히는
        // 순간 이 함수가 다시 고정한다.
        if (cached != 0 && player_gauge_array(reader, cached) == 0) {
            const auto now = std::chrono::steady_clock::now();
            if (g_dead_since.time_since_epoch().count() == 0) {
                g_dead_since = now;
            } else if (now - g_dead_since >= kPlayerDeadFor) {
                g_dead_since = {};
                log::warnf("플레이어 게이지 0x{:X} 를 계속 못 읽는다 - 놓는다",
                           cached);
                g_char.store(0, std::memory_order_release);
                g_arr.store(0, std::memory_order_release);
                std::lock_guard<std::mutex> lk(g_arrs_mtx);
                g_arrs.clear();
            }
        }
        return;   // 잡을 것이 없다
    }
    g_dead_since = {};
    const std::uintptr_t arr = player_gauge_array(reader, want);
    g_char.store(want, std::memory_order_release);
    g_arr.store(arr, std::memory_order_release);
    rebuild_player_arrs(reader, arr);
}

// player_roots 와 같은 걸음이되, root 대신 char 자체를 모은다. 둘을 한 함수로
// 합치지 않는 이유는 부르는 쪽(낙사)이 둘 다 필요하고 의미가 다르기 때문이다 -
// root 는 "누가 맞았나"(rcx), char 는 "누가 때렸나"(sourceCtx) 에 쓴다.
int player_chars(const mem::Reader& reader, std::uintptr_t* out, int max) {
    if (out == nullptr || max <= 0) return 0;
    int n = 0;
    auto push = [&](std::uintptr_t c) {
        if (!vp(c) || n >= max) return;
        for (int i = 0; i < n; ++i) {
            if (out[i] == c) return;
        }
        out[n++] = c;
    };
    const std::uintptr_t ch = player_char();
    push(ch);
    const std::uint16_t want = equip_current_character();
    if (want == kEquipAutoCharacter) return n;
    std::vector<EquipTable> tabs;
    equip_tables_copy(&tabs);
    for (const auto& t : tabs) {
        if (n >= max) break;
        if (t.comp == 0) continue;
        const std::uintptr_t c = q(reader, t.comp, 0x08);
        if (c == 0 || c == ch) continue;
        if (!char_is_player(reader, c)) continue;
        std::uint16_t row = 0;
        if (!actor_character_row(reader, c, &row)) continue;
        if (row != want) continue;
        push(c);
    }
    return n;
}

int player_roots(const mem::Reader& reader, std::uintptr_t* out, int max) {
    if (out == nullptr || max <= 0) return 0;
    int n = 0;
    auto push = [&](std::uintptr_t r) {
        if (!vp(r) || n >= max) return;
        for (int i = 0; i < n; ++i) {
            if (out[i] == r) return;
        }
        out[n++] = r;
    };
    // 지금 고른 캐릭터의 root 를 먼저 넣는다.
    const std::uintptr_t ch = player_char();
    push(char_root(reader, ch));
    // 같은 캐릭터의 **다른 realm** 을 찾아 함께 넣는다. player_char() 가 클라·서버
    // 중 어느 쪽을 집을지 보장되지 않는데, 데미지 디스패처는 서버 root 를 rcx 로
    // 넘긴다(2026-09-12 실측) - 클라 쪽이 잡힌 실행에서는 낙사 훅이 한 번도 안
    // 물렸다. 캐릭터 행으로 짝지어 둘 다 먹인다.
    const std::uint16_t want = equip_current_character();
    if (want == kEquipAutoCharacter) return n;   // 행을 못 풀면 지금 것만
    std::vector<EquipTable> tabs;
    equip_tables_copy(&tabs);
    for (const auto& t : tabs) {
        if (n >= max) break;
        if (t.comp == 0) continue;
        const std::uintptr_t c = q(reader, t.comp, 0x08);
        if (c == 0 || c == ch) continue;
        if (!char_is_player(reader, c)) continue;
        std::uint16_t row = 0;
        if (!actor_character_row(reader, c, &row)) continue;
        if (row != want) continue;
        push(char_root(reader, c));
    }
    return n;
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
