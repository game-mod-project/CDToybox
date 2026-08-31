#include "mem/value_scanner.h"

#include <cmath>

#include "mem/safe_read.h"

namespace cdtb::mem {

std::size_t FloatScan::first(const std::vector<Range>& regions, float target,
                             float eps) {
    reset();
    addrs_.resize(kMaxCandidates);
    std::size_t count = 0;

    for (const auto& r : regions) {
        if (count >= kMaxCandidates) {
            capped_ = true;
            break;
        }
        // 영역이 도중에 해제되면 false가 오지만, 그때까지 채워진
        // 결과는 유효하므로 다음 영역으로 넘어간다.
        scan_region_floats(r.begin, r.size, target, eps, addrs_.data(),
                           kMaxCandidates, &count);
        scanned_.fetch_add(r.size);
    }

    addrs_.resize(count);
    addrs_.shrink_to_fit();

    // 재스캔 비교용 스냅샷.
    prev_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!safe_read_float(addrs_[i], &prev_[i])) prev_[i] = 0.0f;
    }
    return count;
}

template <class Pred>
std::size_t FloatScan::narrow_by(Pred pred) {
    std::vector<std::uintptr_t> keep;
    std::vector<float> keep_prev;
    keep.reserve(addrs_.size());
    keep_prev.reserve(addrs_.size());

    for (std::size_t i = 0; i < addrs_.size(); ++i) {
        float cur = 0.0f;
        // 읽을 수 없게 된 주소는 버린다. 게임이 메모리를 해제하는
        // 것은 정상이고, 그걸로 죽으면 안 된다.
        if (!safe_read_float(addrs_[i], &cur)) continue;
        if (pred(cur, prev_[i])) {
            keep.push_back(addrs_[i]);
            keep_prev.push_back(cur);
        }
    }

    addrs_.swap(keep);
    prev_.swap(keep_prev);
    return addrs_.size();
}

std::size_t FloatScan::narrow_equals(float target, float eps) {
    return narrow_by([target, eps](float cur, float) {
        return std::fabs(cur - target) <= eps;
    });
}

std::size_t FloatScan::narrow_changed() {
    return narrow_by([](float cur, float prev) { return cur != prev; });
}

std::size_t FloatScan::narrow_unchanged() {
    return narrow_by([](float cur, float prev) { return cur == prev; });
}

std::size_t FloatScan::narrow_increased() {
    return narrow_by([](float cur, float prev) { return cur > prev; });
}

std::size_t FloatScan::narrow_decreased() {
    return narrow_by([](float cur, float prev) { return cur < prev; });
}

void FloatScan::reset() {
    addrs_.clear();
    addrs_.shrink_to_fit();
    prev_.clear();
    prev_.shrink_to_fit();
    capped_ = false;
    scanned_.store(0);
}

}  // namespace cdtb::mem
