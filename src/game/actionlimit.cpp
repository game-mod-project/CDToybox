// @build 1.0.0.2949  제한 컴포넌트 vt 0x55B53C8 (RTTI 로 그대로임을 확인, 2026-09-22)
// @build 1.0.0.2944  항목 0x38바이트 (2949 런타임 미확인)
//   근거는 `actionlimit.h` 와 specs/2026-09-21-boss-room-action-limit.md.
//   읽기만 한다 - 게임 메모리에 쓰지 않고 훅도 걸지 않는다.

#include "game/actionlimit.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/log.h"
#include "game/callcheck.h"
#include "mem/reader.h"
#include "mem/safe_read.h"

namespace cdtb::game {

// ------------------------------------------------------------ 순수 부분

ActionLimit action_limit_decode(const std::uint8_t* raw) {
    ActionLimit e;
    if (raw == nullptr) return e;
    e.source = raw[0x00];
    std::memcpy(&e.id, raw + 0x08, 8);
    e.move_lv = raw[0x10];
    e.weapon_out = raw[0x11] != 0;
    e.ride = raw[0x12] != 0;
    e.ride_indoor = raw[0x13] != 0;
    e.ride_off = raw[0x14] != 0;
    e.unset_on_seq = raw[0x15] != 0;
    std::memcpy(&e.limit_ptr, raw + 0x18, 8);
    std::memcpy(&e.limit_n, raw + 0x20, 4);
    std::memcpy(&e.allow_ptr, raw + kLimitAllowPtrOff, 8);
    std::memcpy(&e.allow_n, raw + kLimitAllowCountOff, 4);
    return e;
}

std::size_t action_limit_text(const ActionLimit& e, char* buf, std::size_t cap) {
    if (buf == nullptr || cap == 0) return 0;
    std::string s = "출처 " + std::to_string(e.source) + " 번호 0x";
    char id[24];
    std::snprintf(id, sizeof(id), "%llX", static_cast<unsigned long long>(e.id));
    s += id;
    s += " :";
    if (e.ride) s += " 탑승금지";
    if (e.ride_indoor) s += " 실내탑승금지";
    if (e.ride_off) s += " 하차강제";
    if (e.weapon_out) s += " 무기금지";
    if (e.move_lv != 0) s += " 이동" + std::to_string(e.move_lv);
    if (e.unset_on_seq) s += " 시퀀서해제";
    if (e.limit_n != 0) s += " 금지그룹 " + std::to_string(e.limit_n);
    if (e.allow_n != 0) s += " 허용 " + std::to_string(e.allow_n);
    const std::size_t n = s.size() < cap ? s.size() : cap - 1;
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
    return n;
}

// ------------------------------------------------------------ 게임 쪽 (읽기만)

namespace {

bool rd(std::uintptr_t a, void* out, std::size_t n) {
    return a != 0 && mem::safe_read_bytes(a, out, n);
}
template <class T>
bool rd_t(std::uintptr_t a, T* v) {
    return rd(a, v, sizeof(T));
}

// 액터 -> 제한 보관 객체. 가운데 컴포넌트의 vtable 이 맞아야만 준다.
std::uintptr_t limit_holder_of(std::uintptr_t actor, std::uintptr_t base) {
    std::uintptr_t holder = 0;
    std::uintptr_t ctl = 0;
    std::uintptr_t vt = 0;
    std::uintptr_t lim = 0;
    if (!rd_t(actor + kActorHolderOff, &holder) || holder == 0) return 0;
    if (!rd_t(holder + kHolderCtlOff, &ctl) || ctl == 0) return 0;
    if (!rd_t(ctl, &vt) || vt != base + kCtlVtRva) return 0;
    if (!rd_t(ctl + kCtlLimitOff, &lim)) return 0;
    return lim;
}

// 개수와 항목을 읽는다. 항목이 너무 많거나 못 읽으면 거짓.
bool read_limits(std::uintptr_t holder, std::uint32_t* count, ActionLimit* e) {
    std::uintptr_t arr = 0;
    std::uint32_t n = 0;
    if (!rd_t(holder + kLimitArrayOff, &arr) || !rd_t(holder + kLimitCountOff, &n)) {
        return false;
    }
    *count = n;
    if (n == 0) return true;
    if (n > static_cast<std::uint32_t>(kMaxLimitEntries) || arr == 0) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint8_t raw[kLimitEntrySize] = {};
        if (!rd(arr + i * kLimitEntrySize, raw, sizeof(raw))) return false;
        e[i] = action_limit_decode(raw);
    }
    return true;
}

std::uintptr_t main_player(std::uintptr_t base) {
    std::uintptr_t root = 0;
    std::uintptr_t mgr = 0;
    std::uintptr_t main = 0;
    if (!rd_t(base + kFocusMgrGlobalRva, &root) || root == 0) return 0;
    if (!rd_t(root + kFocusMgrOff, &mgr) || mgr == 0) return 0;
    if (!rd_t(mgr + kMainPlayerOff, &main)) return 0;
    return main;
}

// 제한 목록의 모양이 바뀔 때만 한 줄.
std::atomic<std::uint64_t> g_tick_sig{~0ull};
std::atomic<int> g_tick_lines{0};
constexpr int kMaxTickLines = 60;

std::uint64_t signature(std::uint32_t count, const ActionLimit* e, bool ok) {
    if (!ok) return 0xFFFFFFFF00000000ull;
    std::uint64_t h = 1469598103934665603ull ^ count;
    for (std::uint32_t i = 0; i < count && i < static_cast<std::uint32_t>(kMaxLimitEntries);
         ++i) {
        const ActionLimit& x = e[i];
        const std::uint64_t v = (static_cast<std::uint64_t>(x.source) << 56) ^ x.id ^
                                (static_cast<std::uint64_t>(x.allow_n) << 40) ^
                                (static_cast<std::uint64_t>(x.limit_n) << 32) ^
                                (x.ride ? 1u : 0u) ^ (x.ride_off ? 2u : 0u) ^
                                (x.ride_indoor ? 4u : 0u) ^ (x.weapon_out ? 8u : 0u) ^
                                (static_cast<std::uint64_t>(x.move_lv) << 8);
        h = (h ^ v) * 1099511628211ull;
    }
    return h;
}

}  // namespace

ActionLimitState action_limit_state(const mem::Reader& reader) {
    ActionLimitState st;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return st;
    const std::uintptr_t actor = main_player(base);
    const std::uintptr_t holder = actor != 0 ? limit_holder_of(actor, base) : 0;
    if (holder == 0) return st;
    std::uint32_t n = 0;
    if (!read_limits(holder, &n, st.entry)) return st;
    st.player = true;
    st.limits = static_cast<int>(n);
    st.shown = static_cast<int>(n);
    for (int i = 0; i < st.shown; ++i) {
        if (st.entry[i].allow_n > 0) ++st.with_allow;
        if (st.entry[i].ride) ++st.ride;
        if (st.entry[i].ride_off) ++st.ride_off;
    }
    return st;
}

void action_limit_tick(const mem::Reader& reader, bool log_limits) {
    if (!log_limits) return;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return;
    const std::uintptr_t actor = main_player(base);
    const std::uintptr_t holder = actor != 0 ? limit_holder_of(actor, base) : 0;
    std::uint32_t n = 0;
    ActionLimit e[kMaxLimitEntries];
    const bool ok = holder != 0 && read_limits(holder, &n, e);
    const std::uint64_t sig = signature(n, e, ok);
    if (g_tick_sig.exchange(sig, std::memory_order_relaxed) == sig) return;
    if (g_tick_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxTickLines) return;
    if (!ok) {
        log::infof("행동 제한: 주 플레이어의 제한 목록을 못 읽었다 (액터 0x{:X})",
                   actor);
        return;
    }
    if (n == 0) {
        log::infof("행동 제한: 없음 (액터 0x{:X})", actor);
        return;
    }
    std::string line = "행동 제한 " + std::to_string(n) + "개:";
    for (std::uint32_t i = 0; i < n; ++i) {
        char buf[160] = {};
        action_limit_text(e[i], buf, sizeof(buf));
        line += " [";
        line += buf;
        line += "]";
    }
    log::infof("{} (액터 0x{:X})", line, actor);
}

}  // namespace cdtb::game
