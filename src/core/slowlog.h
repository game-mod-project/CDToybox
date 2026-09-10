#pragma once

#include <windows.h>

#include "core/log.h"

namespace cdtb::log {

// 걸린 시간이 문턱을 넘을 때만 로그에 남긴다. 넘지 않으면 아무것도
// 하지 않으므로 매 프레임 자리에 놓아도 된다.
//
// 왜 필요한가: "느리다" 는 증상만으로 원인을 짚다가 이 세션에서 이미
// 두 번 헛다리를 짚었다. 코드를 읽어 '아마 여기' 라고 정하지 말고
// 숫자를 보고 정한다.
class Slow {
public:
    Slow(const char* what, double limit_ms)
        : what_(what), limit_(limit_ms) {
        ::QueryPerformanceCounter(&t0_);
    }
    ~Slow() {
        LARGE_INTEGER t1{}, f{};
        ::QueryPerformanceCounter(&t1);
        ::QueryPerformanceFrequency(&f);
        if (f.QuadPart == 0) return;
        const double ms =
            static_cast<double>(t1.QuadPart - t0_.QuadPart) * 1000.0 /
            static_cast<double>(f.QuadPart);
        if (ms >= limit_) warnf("느림: {} {:.1f}ms", what_, ms);
    }
    Slow(const Slow&) = delete;
    Slow& operator=(const Slow&) = delete;

private:
    const char* what_;
    double limit_;
    LARGE_INTEGER t0_{};
};

}  // namespace cdtb::log
