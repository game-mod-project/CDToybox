// @build 1.0.0.2944  스킬 판정 0x362DF0(조건 0xED) · 제한 컴포넌트 vt 0x55B53C8
//   근거는 `bosscall.h` 와 specs/2026-09-21-boss-room-action-limit.md.

#include "game/bosscall.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>

#include "core/log.h"
#include "game/callcheck.h"
#include "game/callgate.h"
#include "mem/hook.h"
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

bool bosscall_targets(std::uint32_t cond_type, std::uint32_t skill_key) {
    return cond_type == kSkillCheckCondType &&
           (skill_key == kCallVehicleSkillKey || skill_key == kCallDragonSkillKey);
}

int bosscall_hide_plan(const ActionLimit* e, int n, int* out, int cap) {
    if (e == nullptr || out == nullptr || n <= 0 || n > kMaxLimitEntries) return 0;
    int m = 0;
    for (int i = 0; i < n && m < cap; ++i) {
        if (e[i].allow_n > 0) out[m++] = i;
    }
    return m;
}

bool bosscall_can_restore(const LimitShape& before, const LimitShape& after) {
    if (before.array != after.array || before.count != after.count) return false;
    if (before.count > static_cast<std::uint32_t>(kMaxLimitEntries)) return false;
    for (std::uint32_t i = 0; i < before.count; ++i) {
        if (before.allow_ptr[i] != after.allow_ptr[i]) return false;
    }
    return true;
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

// ------------------------------------------------------------ 게임 쪽

namespace {

// 조건 분배기 0x360BE0 이 `표[종류-0x83]` 을 부르는 꼴 그대로(0x360CEB..0x360D1D):
// rcx · rdx · r8 · r9 · [rsp+20](바이트) · [rsp+28] · [rsp+30]. 원본은 일곱째
// (`[rsp+0xA0]` = 진입 rsp+0x38)까지 읽으므로 **일곱을 다 넘긴다**(TROUBLESHOOTING
// 1.14 — 모자라면 원본이 우리 스택의 쓰레기를 읽는다). 다섯째는 바이트만 쓰이지만
// 슬롯 8바이트를 그대로 옮긴다.
using SkillCheckFn = std::uint8_t (*)(void*, void*, void*, void*, std::uint64_t,
                                      void*, void*);

SkillCheckFn g_orig = nullptr;
std::atomic<bool> g_installed{false};  // 트램폴린이 있다(한 번이라도 걸었다)
std::atomic<bool> g_active{false};     // 지금 넘기는 중
std::atomic<bool> g_unsupported{false};
std::atomic<bool> g_gate_on{false};    // 탑승 제한 관문을 우리가 켰다
std::uintptr_t g_base = 0;
std::mutex g_set_mtx;

std::atomic<std::uint32_t> g_hidden{0};
std::atomic<std::uint32_t> g_passed{0};
std::atomic<std::uint32_t> g_blocked{0};
std::atomic<std::uint32_t> g_kept{0};   // 모양이 바뀌어 되돌리지 않은 횟수
std::atomic<int> g_hook_lines{0};
std::atomic<std::uint64_t> g_hook_last{~0ull};
constexpr int kMaxHookLines = 24;

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

// 배열·개수·항목을 읽는다. 항목이 너무 많거나 못 읽으면 거짓.
bool read_limits(std::uintptr_t holder, LimitShape* s, ActionLimit* e) {
    std::uintptr_t arr = 0;
    std::uint32_t n = 0;
    if (!rd_t(holder + kLimitArrayOff, &arr) || !rd_t(holder + kLimitCountOff, &n)) {
        return false;
    }
    s->array = arr;
    s->count = n;
    if (n == 0) return true;
    if (n > static_cast<std::uint32_t>(kMaxLimitEntries) || arr == 0) return false;
    for (std::uint32_t i = 0; i < n; ++i) {
        std::uint8_t raw[kLimitEntrySize] = {};
        if (!rd(arr + i * kLimitEntrySize, raw, sizeof(raw))) return false;
        e[i] = action_limit_decode(raw);
        s->allow_ptr[i] = e[i].allow_ptr;
    }
    return true;
}

void report_hook(std::uint32_t skill, int hid, std::uint8_t r, bool kept) {
    // 같은 (스킬, 가린 수, 결과) 가 이어지면 넘긴다. 달라지면 다시 찍는다.
    const std::uint64_t key = (static_cast<std::uint64_t>(skill) << 32) |
                              (static_cast<std::uint64_t>(hid & 0xFF) << 8) | r;
    if (g_hook_last.exchange(key, std::memory_order_relaxed) == key && !kept) return;
    if (g_hook_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxHookLines) return;
    if (r == 0) {
        log::infof("보스룸 호출: 스킬 {} 판정 - 허용 목록 {}개를 가리니 **통과**"
                   "{}", skill, hid, kept ? " (목록 모양이 바뀌어 되돌리지 않음)" : "");
    } else {
        log::infof("보스룸 호출: 스킬 {} 판정 - 허용 목록 {}개를 가려도 막힘(결과 {})"
                   " - 허용 목록 말고 다른 것이 막는다{}",
                   skill, hid, r, kept ? " (목록 모양이 바뀌어 되돌리지 않음)" : "");
    }
}

std::uint8_t detour(void* ctx, void* param, void* a3, void* a4, std::uint64_t a5,
                    void* a6, void* a7) {
    if (!g_active.load(std::memory_order_acquire) || param == nullptr) {
        return g_orig(ctx, param, a3, a4, a5, a6, a7);
    }
    // 분배기가 이미 `[[rdx]]` 를 읽었다(0x360C06) - 레코드는 살아 있다.
    const std::uint32_t* rec = *static_cast<const std::uint32_t* const*>(param);
    if (rec == nullptr || !bosscall_targets(rec[0], rec[1])) {
        return g_orig(ctx, param, a3, a4, a5, a6, a7);
    }
    const std::uint32_t skill = rec[1];

    std::uintptr_t actor = 0;
    const std::uintptr_t holder =
        rd_t(reinterpret_cast<std::uintptr_t>(ctx) + 8, &actor) && actor != 0
            ? limit_holder_of(actor, g_base)
            : 0;
    LimitShape before;
    ActionLimit e[kMaxLimitEntries];
    int idx[kMaxLimitEntries] = {};
    int hid = 0;
    if (holder != 0 && read_limits(holder, &before, e)) {
        hid = bosscall_hide_plan(e, static_cast<int>(before.count), idx,
                                 kMaxLimitEntries);
    }
    if (hid == 0) {
        // 가릴 것이 없다 - 원본 그대로. (평소 벌판이 여기다.)
        return g_orig(ctx, param, a3, a4, a5, a6, a7);
    }

    const std::uint32_t zero = 0;
    int written = 0;
    for (int k = 0; k < hid; ++k) {
        const std::uintptr_t at =
            before.array + idx[k] * kLimitEntrySize + kLimitAllowCountOff;
        if (!mem::safe_write_bytes(at, &zero, sizeof(zero))) break;
        ++written;
    }
    const std::uint8_t r = g_orig(ctx, param, a3, a4, a5, a6, a7);

    // **되돌리기 전에 모양을 다시 본다.** 그 사이 항목이 빠지거나 옮겨졌으면
    // 첨자가 가리키는 항목이 달라졌을 수 있다 - 거기 개수를 써 넣으면 게임이
    // 그 목록의 실제 길이 밖을 읽는다. 그때는 안 쓴다(허용 목록이 풀린 채로
    // 남는 쪽이 목록 밖 읽기보다 낫다).
    LimitShape after;
    ActionLimit e2[kMaxLimitEntries];
    const bool same = read_limits(holder, &after, e2) &&
                      bosscall_can_restore(before, after);
    if (same) {
        for (int k = 0; k < written; ++k) {
            const std::uintptr_t at =
                before.array + idx[k] * kLimitEntrySize + kLimitAllowCountOff;
            const std::uint32_t n = e[idx[k]].allow_n;
            mem::safe_write_bytes(at, &n, sizeof(n));
        }
    } else {
        g_kept.fetch_add(1, std::memory_order_relaxed);
    }

    g_hidden.fetch_add(1, std::memory_order_relaxed);
    if (r == 0) {
        g_passed.fetch_add(1, std::memory_order_relaxed);
    } else {
        g_blocked.fetch_add(1, std::memory_order_relaxed);
    }
    report_hook(skill, written, r, !same);
    return r;
}

// 설치 전 **이 함수만의 명령**으로 확인한다(프롤로그는 흔하다 - §1.5).
bool meaning_ok(const mem::Reader& reader, std::uintptr_t fn) {
    auto same = [&](std::size_t off, std::initializer_list<std::uint8_t> want) {
        std::uint8_t got[8] = {};
        if (want.size() > sizeof(got) || !reader.read(fn + off, got, want.size())) {
            return false;
        }
        return std::memcmp(got, want.begin(), want.size()) == 0;
    };
    return same(kCheckCtlLoadOff, {0x48, 0x8B, 0xB8, 0x20, 0x01, 0x00, 0x00}) &&
           same(kCheckLearnCmpOff, {0x66, 0x39, 0xB0, 0xA0, 0x00, 0x00, 0x00}) &&
           same(kCheckAllowCmpOff, {0x83, 0x7B, 0x30, 0x00}) &&
           same(kCheckAllowPtrOff, {0x4C, 0x8B, 0x4B, 0x28});
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

// 제한 목록의 모양이 바뀔 때만 한 줄. 보스룸 출입이 저절로 찍힌다.
std::atomic<std::uint64_t> g_tick_sig{~0ull};
std::atomic<int> g_tick_lines{0};
constexpr int kMaxTickLines = 60;

std::uint64_t signature(const LimitShape& s, const ActionLimit* e, bool ok) {
    if (!ok) return 0xFFFFFFFF00000000ull;
    std::uint64_t h = 1469598103934665603ull ^ s.count;
    for (std::uint32_t i = 0; i < s.count && i < static_cast<std::uint32_t>(kMaxLimitEntries);
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

bool bosscall_set(const mem::Reader& reader, bool on, const char** why) {
    std::lock_guard<std::mutex> lk(g_set_mtx);
    auto fail = [&](const char* w) {
        if (why != nullptr) *why = w;
        return false;
    };
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return fail("게임 모듈을 못 찾았다");
    const std::uintptr_t fn = base + kSkillCheckFnRva;

    if (on) {
        if (g_active.load(std::memory_order_acquire)) {
            // 이미 켜져 있다 - 다시 걸지 않는다(켜진 훅에 hook_install 을 또 부르면
            // 실패가 돌아올 수 있고, 그걸 "못 켰다" 로 보이면 안 된다).
            if (why != nullptr) *why = "";
            return true;
        }
        if (g_unsupported.load()) return fail("이 게임 빌드에서는 못 쓴다");
        if (!g_installed.load()) {
            if (!meaning_ok(reader, fn)) {
                g_unsupported.store(true);
                log::warnf("보스룸 호출: 0x{:X} 가 2944 의 스킬 판정과 다르다 - 안 건다",
                           kSkillCheckFnRva);
                return fail("스킬 판정 함수의 바이트가 다르다(게임 갱신)");
            }
            g_base = base;
        }
        if (!mem::hook_install(reinterpret_cast<void*>(fn),
                               reinterpret_cast<void*>(&detour),
                               reinterpret_cast<void**>(&g_orig))) {
            return fail("스킬 판정 훅을 못 걸었다");
        }
        g_installed.store(true);
        g_active.store(true, std::memory_order_release);
        const char* gw = "";
        const bool gate = callgate_set(kCallGateRideLimit, true, &gw);
        g_gate_on.store(gate);
        log::infof("보스룸 호출 켬 - 스킬 판정 0x{:X} 감쌈 · 검증기 탑승 제한 관문 {}{}",
                   kSkillCheckFnRva, gate ? "켬" : "실패: ", gate ? "" : gw);
        if (why != nullptr) *why = "";
        return true;
    }

    // 끄기: 켠 적이 없으면 아무것도 안 한다(모드를 내릴 때도 불린다).
    if (!g_active.load(std::memory_order_acquire) && !g_gate_on.load()) {
        if (why != nullptr) *why = "";
        return true;
    }
    // 먼저 넘기기를 멈추고 훅을 끈다(트램폴린은 남긴다 - hook.h).
    g_active.store(false, std::memory_order_release);
    if (g_installed.load()) mem::hook_disable(reinterpret_cast<void*>(fn));
    if (g_gate_on.exchange(false)) callgate_set(kCallGateRideLimit, false, nullptr);
    log::infof("보스룸 호출 끔 (가린 {}회 · 통과 {} · 막힘 {} · 못 되돌림 {})",
               g_hidden.load(), g_passed.load(), g_blocked.load(), g_kept.load());
    if (why != nullptr) *why = "";
    return true;
}

BossCallState bosscall_state(const mem::Reader& reader) {
    BossCallState st;
    st.on = g_active.load(std::memory_order_acquire);
    st.unsupported = g_unsupported.load();
    st.hidden = g_hidden.load(std::memory_order_relaxed);
    st.passed = g_passed.load(std::memory_order_relaxed);
    st.blocked = g_blocked.load(std::memory_order_relaxed);
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return st;
    const std::uintptr_t actor = main_player(base);
    const std::uintptr_t holder = actor != 0 ? limit_holder_of(actor, base) : 0;
    if (holder == 0) return st;
    LimitShape s;
    if (!read_limits(holder, &s, st.entry)) return st;
    st.player = true;
    st.limits = static_cast<int>(s.count);
    st.shown = static_cast<int>(s.count);
    for (int i = 0; i < st.shown; ++i) {
        if (st.entry[i].allow_n > 0) ++st.with_allow;
        if (st.entry[i].ride) ++st.ride;
        if (st.entry[i].ride_off) ++st.ride_off;
    }
    return st;
}

void bosscall_tick(const mem::Reader& reader, bool log_limits) {
    if (!log_limits && !g_active.load(std::memory_order_acquire)) return;
    const std::uintptr_t base = reader.module_base();
    if (base == 0) return;
    const std::uintptr_t actor = main_player(base);
    const std::uintptr_t holder = actor != 0 ? limit_holder_of(actor, base) : 0;
    LimitShape s;
    ActionLimit e[kMaxLimitEntries];
    const bool ok = holder != 0 && read_limits(holder, &s, e);
    const std::uint64_t sig = signature(s, e, ok);
    if (g_tick_sig.exchange(sig, std::memory_order_relaxed) == sig) return;
    if (g_tick_lines.fetch_add(1, std::memory_order_relaxed) >= kMaxTickLines) return;
    if (!ok) {
        log::infof("행동 제한: 주 플레이어의 제한 목록을 못 읽었다 (액터 0x{:X})",
                   actor);
        return;
    }
    if (s.count == 0) {
        log::infof("행동 제한: 없음 (액터 0x{:X})", actor);
        return;
    }
    std::string line = "행동 제한 " + std::to_string(s.count) + "개:";
    for (std::uint32_t i = 0; i < s.count; ++i) {
        char buf[160] = {};
        action_limit_text(e[i], buf, sizeof(buf));
        line += " [";
        line += buf;
        line += "]";
    }
    log::infof("{} (액터 0x{:X})", line, actor);
}

}  // namespace cdtb::game
