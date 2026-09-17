#include "game/wheelfill.h"

#include <atomic>

namespace cdtb {
namespace game {

namespace {

constexpr int kRowsMax = 16;
constexpr std::uint16_t kSlotNone = 0xFFFF;

std::atomic<bool> g_on{false};
std::uint16_t g_rows[kRowsMax]{};
std::atomic<int> g_n{0};

}  // namespace

bool wheel_fill_wanted(std::uint16_t cur, std::uint16_t species,
                       const std::uint16_t* rows, int n) {
    // 이미 어느 칸에 올라가 있으면 손대지 않는다. 사용자가 올려 둔 것을
    // 우리가 옮기면 그쪽이 사라진다.
    if (cur != kSlotNone) return false;
    if (rows == nullptr || n <= 0) return false;
    for (int i = 0; i < n; ++i) {
        if (rows[i] == species) return true;
    }
    return false;
}

void wheel_fill_set_enabled(bool on) {
    g_on.store(on, std::memory_order_relaxed);
}

bool wheel_fill_enabled() { return g_on.load(std::memory_order_relaxed); }

void wheel_fill_set_rows(const std::uint16_t* rows, int n) {
    if (rows == nullptr || n <= 0) {
        g_n.store(0, std::memory_order_relaxed);
        return;
    }
    if (n > kRowsMax) n = kRowsMax;
    for (int i = 0; i < n; ++i) g_rows[i] = rows[i];
    g_n.store(n, std::memory_order_relaxed);
}

int wheel_fill_rows(const std::uint16_t** out) {
    if (out != nullptr) *out = g_rows;
    return g_n.load(std::memory_order_relaxed);
}

// 같은 종에 레코드가 여럿일 때 **어느 것을 휠에 올릴지** 가른다.
// 드래곤이 1000483(생명 1) · 1000724(생명 2500) 둘이었고, 빈 껍데기가 올라가
// 있어서 소환이 완결되지 않았다(2026-09-16 실측).
bool wheel_fill_better(std::int32_t hp_a, std::int32_t grow_a,
                       std::int32_t hp_b, std::int32_t grow_b) {
    // **-1 은 "게임이 정한다" 센티널이고 온전한 쪽이다.** 와이번·A.T.A.G. 가
    // 그 값인데 스탯이 정상으로 뜬다(실측). 부호 그대로 비교하면 -1 이 껍데기의
    // 1 보다 낮게 깔려 **껍데기를 고르게 된다** - 시험에서 걸렸다.
    const auto rank = [](std::int32_t hp) {
        return hp == kHpDefault ? kHpRankMax : hp;
    };
    const std::int32_t ra = rank(hp_a);
    const std::int32_t rb = rank(hp_b);
    if (ra != rb) return ra > rb;
    return grow_a > grow_b;
}

}  // namespace game
}  // namespace cdtb
