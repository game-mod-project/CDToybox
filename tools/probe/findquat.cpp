#include "findquat.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "remote.h"

namespace {

// 단위 쿼터니언처럼 생겼는가.
//
// 길이가 1이라는 조건만으로도 후보가 크게 준다. 항등(0,0,0,±1)과
// 축 정렬은 뺀다 - 씬에 그런 것이 수만 개 있고 카메라가 그 상태로
// 있을 일은 없다.
bool looks_like_quat(const float q[4]) {
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(q[i])) return false;
        if (std::fabs(q[i]) > 1.001f) return false;
    }
    const float len = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
    if (std::fabs(len - 1.0f) > 0.01f) return false;

    // 성분 중 하나라도 0 도 1 도 아닌 값이 있어야 한다.
    int interesting = 0;
    for (int i = 0; i < 4; ++i) {
        const float a = std::fabs(q[i]);
        if (a > 0.02f && a < 0.98f) ++interesting;
    }
    return interesting >= 2;
}

}  // namespace

// 시점을 돌리는 동안 변하는 단위 쿼터니언을 찾는다.
//
// 두 번에 나눠 한다. 먼저 후보 주소를 모으고(메모리 전체를 두 번
// 통째로 뜨면 10GB 를 두 벌 들고 있어야 한다), 잠시 뒤 그 주소만
// 다시 읽어 변한 것을 고른다.
void cmd_findquat(const cdtb::probe::Remote& r, unsigned wait_ms,
                  std::size_t max_report) {
    struct Cand { std::uintptr_t addr; float q[4]; };
    std::vector<Cand> cand;
    std::vector<std::uint8_t> buf;
    std::size_t scanned = 0;

    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.size == 0 || reg.size > (1u << 30)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), reg.size)) continue;
        scanned += reg.size;

        for (std::size_t i = 0; i + 16 <= reg.size; i += 4) {
            float q[4];
            std::memcpy(q, buf.data() + i, 16);
            if (!looks_like_quat(q)) continue;
            cand.push_back({reg.base + i, {q[0], q[1], q[2], q[3]}});
            if (cand.size() >= 2000000) break;
        }
        if (cand.size() >= 2000000) break;
    }

    std::printf("후보 %zu개 / %.1f MB 훑음. %u ms 뒤 다시 본다.\n",
                cand.size(), static_cast<double>(scanned) / 1048576.0, wait_ms);
    if (cand.empty()) return;

    ::Sleep(wait_ms);

    std::size_t changed = 0, shown = 0;
    float after[4];
    for (std::size_t i = 0; i < cand.size(); ++i) {
        if (!r.read(cand[i].addr, after, 16)) continue;
        if (!looks_like_quat(after)) continue;

        // 비트가 아니라 크기로 본다. 회전은 연속적으로 변한다.
        float d = 0.0f;
        for (int k = 0; k < 4; ++k) d += std::fabs(after[k] - cand[i].q[k]);
        if (d < 0.002f) continue;

        ++changed;
        if (shown < max_report) {
            ++shown;
            std::printf("0x%llX  (%+.4f %+.4f %+.4f %+.4f) -> "
                        "(%+.4f %+.4f %+.4f %+.4f)  변화 %.4f\n",
                        static_cast<unsigned long long>(cand[i].addr),
                        cand[i].q[0], cand[i].q[1], cand[i].q[2],
                        cand[i].q[3], after[0], after[1], after[2],
                        after[3], d);
        }
    }
    std::printf("변한 쿼터니언 %zu개 (표시 %zu개)\n", changed, shown);
}
