#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mem/scanner.h"

namespace cdtb::mem {

// 값 기반 메모리 스캔. Cheat Engine의 "첫 스캔 → 재스캔" 흐름이다.
//
// 값이 얼마인지 몰라도 변함/안 변함/증가/감소로 좁힐 수 있어,
// 게임 내부 지식 없이 자료구조를 찾을 수 있다.
//
// 영역을 인자로 받으므로 합성 버퍼로 테스트할 수 있다. 실제 게임
// 메모리는 writable_regions()가 준다.
class FloatScan {
public:
    // 후보 상한. 넘으면 capped()가 true가 되고 거기서 멈춘다.
    // 주소 8바이트 * 100만 = 8MB. 이보다 많이 나오면 더 특징적인
    // 값을 골라야 한다.
    static constexpr std::size_t kMaxCandidates = 1000000;

    std::size_t first(const std::vector<Range>& regions, float target,
                      float eps);

    std::size_t narrow_equals(float target, float eps);
    std::size_t narrow_changed();
    std::size_t narrow_unchanged();
    std::size_t narrow_increased();
    std::size_t narrow_decreased();

    void reset();

    std::size_t count() const { return addrs_.size(); }
    bool capped() const { return capped_; }
    const std::vector<std::uintptr_t>& results() const { return addrs_; }

    // 최초 스캔의 진행 바이트. 워커 스레드에서 돌 때 UI가 읽는다.
    std::uint64_t scanned_bytes() const { return scanned_.load(); }

private:
    // 조건 함수로 후보를 거른다. cur는 현재값, prev는 직전 스냅샷.
    template <class Pred>
    std::size_t narrow_by(Pred pred);

    std::vector<std::uintptr_t> addrs_;
    std::vector<float> prev_;
    bool capped_ = false;
    std::atomic<std::uint64_t> scanned_{0};
};

}  // namespace cdtb::mem
