#pragma once

#include <atomic>

namespace cdtb {

// 못 찾는 탐색의 끝.
//
// **간격만 두는 것으로는 부족하다.** *간격 < 한 번 비용* 이면 쉬는 것이 아니라
// 쉬지 않고 도는 것이다 - 수배 컴포넌트는 15초 간격에 한 번이 45초짜리라
// 로그의 3분의 1이 그 경고였고 배경이 내내 무거웠다. 화면은 안 멎으니 아무도
// 눈치채지 못했다(`TROUBLESHOOTING.md` 2.10.1).
//
// **연속 실패만 센다. 성공하면 0 으로 돌아간다.** 명부 컴포넌트처럼 게임이
// 새로 만들 때마다 다시 찾아야 하는 대상이 있어서, 성공한 재탐색까지 세면
// **정당한 재탐색이 막혀** 종 바꾸기·획득 뒤처리가 조용히 죽는다. 끝은
// "못 찾는 상태가 이어진다" 에만 걸어야 한다.
//
// 그만둔 뒤에는 사람이 다시 켠다(`rearm`). 그리고 **그만뒀다는 것을 화면이
// 말해야 한다** - 안 그러면 "저절로 잡습니다" 를 영원히 띄운다.
//
// 여러 스레드가 만진다(분석 루프 · 배경 워커 · 그리는 스레드의 [다시 찾기]).
// 그래서 세는 칸이 원자다.
class FindLimit {
public:
    // 상한 0 이하는 **설정 실수**로 본다. 끝을 없애는 길을 두면 이 규칙이
    // 다시 새어 나가므로, 적어도 한 번은 해 보고 그만둔다.
    explicit FindLimit(int cap) : cap_(cap > 0 ? cap : 1) {}

    // 한 번 더 해 봐도 되는가.
    bool may_try() const {
        return fails_.load(std::memory_order_relaxed) < cap_;
    }
    // 그만뒀는가. 화면 문구·상태 조회용.
    bool gave_up() const {
        return fails_.load(std::memory_order_relaxed) >= cap_;
    }
    // 이번 실패를 센다. **이번이 마지막이면** true - 그때 로그에 남긴다.
    // 상한에 닿으면 더 올리지 않는다(긴 판에서 넘치지 않게).
    bool note_failure() {
        int n = fails_.load(std::memory_order_relaxed);
        while (n < cap_) {
            if (fails_.compare_exchange_weak(n, n + 1,
                                             std::memory_order_relaxed)) {
                return n + 1 >= cap_;
            }
        }
        return true;
    }
    void note_success() { fails_.store(0, std::memory_order_relaxed); }
    void rearm() { fails_.store(0, std::memory_order_relaxed); }

    int fails() const { return fails_.load(std::memory_order_relaxed); }
    int cap() const { return cap_; }

private:
    const int cap_;
    std::atomic<int> fails_{0};
};

}  // namespace cdtb
