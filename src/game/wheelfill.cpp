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

}  // namespace game
}  // namespace cdtb
