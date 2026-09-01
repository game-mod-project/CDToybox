#include "findquat.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
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

void cmd_hold(const cdtb::probe::Remote& r, std::uintptr_t addr, float x,
              float y, float z, unsigned ms) {
    float before[3]{};
    const bool had = r.read(addr, before, sizeof(before));
    const float v[3] = {x, y, z};

    std::printf("0x%llX 에 (%.2f, %.2f, %.2f) 를 %u ms 동안 눌러 씁니다.\n",
                static_cast<unsigned long long>(addr), x, y, z, ms);
    if (had) {
        std::printf("  쓰기 전 (%.2f, %.2f, %.2f)\n", before[0], before[1],
                    before[2]);
    }

    const DWORD end = ::GetTickCount() + ms;
    std::size_t writes = 0;
    while (::GetTickCount() < end) {
        if (r.write(addr, v, sizeof(v))) ++writes;
    }

    float after[3]{};
    r.read(addr, after, sizeof(after));
    std::printf("  %zu회 씀. 끝난 뒤 (%.2f, %.2f, %.2f)\n", writes, after[0],
                after[1], after[2]);
    if (had) {
        std::printf("  원래 값으로 되돌립니다.\n");
        r.write(addr, before, sizeof(before));
    }
}

void cmd_findvec3d(const cdtb::probe::Remote& r, double x, double y, double z,
                   double eps, std::size_t max_hits) {
    std::vector<std::uint8_t> buf;
    std::size_t scanned = 0, hits = 0;

    for (const auto& reg : r.regions()) {
        if (reg.size == 0 || reg.size > (1u << 30)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), reg.size)) continue;
        scanned += reg.size;

        // 8바이트 정렬만 본다. double 좌표는 정렬돼 있다.
        for (std::size_t i = 0; i + 24 <= reg.size; i += 8) {
            double v[3];
            std::memcpy(v, buf.data() + i, 24);
            bool ok = true;
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(v[k])) { ok = false; break; }
            }
            if (!ok) continue;
            if (std::fabs(v[0] - x) > eps) continue;
            if (std::fabs(v[1] - y) > eps) continue;
            if (std::fabs(v[2] - z) > eps) continue;

            std::printf("0x%llX  (%.3f, %.3f, %.3f)  %s\n",
                        static_cast<unsigned long long>(reg.base + i), v[0],
                        v[1], v[2], reg.writable ? "쓰기가능" : "읽기전용");
            if (++hits >= max_hits) {
                std::printf("(최대 %zu개에서 멈춤)\n", max_hits);
                return;
            }
        }
    }
    std::printf("double 좌표 %zu곳 / %.1f MB 훑음 (오차 %.2f)\n", hits,
                static_cast<double>(scanned) / 1048576.0, eps);
}

void cmd_holdmany(const cdtb::probe::Remote& r, float x, float y, float z,
                  float eps, float dy, unsigned ms, std::size_t start,
                  std::size_t count) {
    struct Hit { std::uintptr_t addr; float v[3]; };
    std::vector<Hit> all;
    std::vector<std::uint8_t> buf;

    for (const auto& reg : r.regions()) {
        if (!reg.writable || reg.size == 0 || reg.size > (1u << 30)) continue;
        buf.resize(reg.size);
        if (!r.read(reg.base, buf.data(), reg.size)) continue;

        for (std::size_t i = 0; i + 12 <= reg.size; i += 4) {
            float v[3];
            std::memcpy(v, buf.data() + i, 12);
            if (!std::isfinite(v[0]) || !std::isfinite(v[1]) ||
                !std::isfinite(v[2])) {
                continue;
            }
            if (std::fabs(v[0] - x) > eps) continue;
            if (std::fabs(v[1] - y) > eps) continue;
            if (std::fabs(v[2] - z) > eps) continue;
            all.push_back({reg.base + i, {v[0], v[1], v[2]}});
            if (all.size() >= 20000) break;
        }
        if (all.size() >= 20000) break;
    }

    std::printf("후보 %zu곳\n", all.size());
    if (all.empty()) return;
    if (start >= all.size()) {
        std::printf("start 가 범위를 벗어났습니다\n");
        return;
    }
    const std::size_t end = (count == 0) ? all.size()
                                         : (std::min)(all.size(), start + count);
    std::printf("[%zu, %zu) 구간 %zu곳을 Y%+.0f 로 %u ms 동안 눌러 씁니다.\n",
                start, end, end - start, dy, ms);

    const DWORD stop = ::GetTickCount() + ms;
    std::size_t writes = 0;
    while (::GetTickCount() < stop) {
        for (std::size_t i = start; i < end; ++i) {
            const float v[3] = {all[i].v[0], all[i].v[1] + dy, all[i].v[2]};
            if (r.write(all[i].addr, v, sizeof(v))) ++writes;
        }
    }

    // 원래 값으로 되돌린다. 게임이 이미 다시 썼을 수도 있지만,
    // 우리가 남긴 값을 그대로 두지는 않는다.
    for (std::size_t i = start; i < end; ++i) {
        r.write(all[i].addr, all[i].v, sizeof(all[i].v));
    }
    std::printf("%zu회 씀. 원래 값으로 복구했습니다.\n", writes);
}
