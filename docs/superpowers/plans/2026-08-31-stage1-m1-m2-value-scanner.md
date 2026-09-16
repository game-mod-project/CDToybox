> **[2026-09-16] 값 스캐너는 구현 완료 · 카메라는 보류.** 값 스캐너
> (`mem/regions` · `mem/value_scanner` · `render/scan_panel`)는 들어왔다.
> **카메라 쪽 목표는 답을 못 찾아 보류**이고 `freecam` 은 훅을 설치하지
> 않는다(`kDeferred`) — `docs/STATUS.md` §2.4 · §3. **아래 체크박스는 실행 당시 안 찍었다** — 미완으로 읽지 말 것.

# 1단계 M1·M2 구현 계획 — 값 스캐너와 카메라 구조체 확보

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 게임 메모리에서 값을 찾아 좁혀 나가는 도구를 만들고, 그것으로 카메라 구조체를 찾아 FOV와 위치·회전 오프셋을 확정한다.

**Architecture:** `mem/regions`가 `VirtualQuery`로 스캔 대상 영역을 열거하고, `mem/value_scanner`가 그 위에서 후보를 좁힌다. 스캐너 로직은 영역을 인자로 받아 합성 버퍼로 테스트 가능하다. `render/scan_panel`이 오버레이 UI를 제공하고 최초 스캔은 워커 스레드에서 돌린다. `game/camera`는 베이스 주소와 오프셋을 **런타임 설정**으로 받아, 역공학 반복에 빌드·재시작이 필요 없게 한다.

**Tech Stack:** C++20 / MSVC 14.44 x64 / Win32 `VirtualQuery` / SEH / Dear ImGui

**Spec:** `docs/superpowers/specs/2026-08-31-stage1-freecam-design.md`

## Global Constraints

- 언어 표준 **C++20**, 컴파일러 **MSVC 14.44.35207 x64**, 생성기 **Ninja**.
- 빌드: `powershell -ExecutionPolicy Bypass -File E:\CDToybox\scripts\build.ps1`
- 배포: `powershell -ExecutionPolicy Bypass -File E:\CDToybox\scripts\deploy.ps1`
- 네임스페이스 루트는 `cdtb`. 게임 지식은 `src/game/`에만 둔다.
- **어떤 실패도 게임을 죽이지 않는다.** 메모리 접근은 전부 SEH로 감싼다.
- MSVC는 소멸자를 가진 C++ 객체가 있는 함수에서 `__try`를 허용하지 않는다(C2712). **SEH 껍데기와 본문을 분리한다.**
- 렌더 스레드(`on_frame`)를 막지 않는다. 오래 걸리는 작업은 워커 스레드로 보낸다.
- 렌더 핫패스에 로그와 뮤텍스를 두지 않는다.
- **인게임 검증이 필요한 변경은 한 번에 하나만 배포한다.** 배포마다 로그를 지운다.
- **게임 실행은 사용자가 직접 한다. 자동 실행하지 않는다.**
- 브랜치: `develop`에서 `feat/stage1-value-scanner`를 분기한다. `main`에 직접 커밋하지 않는다.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `src/mem/safe_read.{h,cpp}` | SEH로 보호된 메모리 읽기. C++ 객체를 두지 않는 최소 함수만 |
| `src/mem/regions.{h,cpp}` | `VirtualQuery`로 커밋·쓰기가능 영역 열거 |
| `src/mem/value_scanner.{h,cpp}` | `FloatScan` — 후보 수집과 좁히기. 영역을 인자로 받아 테스트 가능 |
| `src/game/camera.{h,cpp}` | 카메라 접근. 베이스·오프셋은 런타임 설정 |
| `src/render/scan_panel.{h,cpp}` | 오버레이 스캔 UI, 워커 스레드, 메모리 관찰 |
| `tests/safe_read_tests.cpp` | 유효/무효 주소 읽기 |
| `tests/regions_tests.cpp` | 영역 열거 불변식 |
| `tests/value_scanner_tests.cpp` | 합성 버퍼로 좁히기 로직 전수 |

`safe_read`를 별도 파일로 떼는 이유는 SEH 제약 때문이다. `__try`가 있는 함수에는 소멸자를 가진 객체를 둘 수 없으므로, 그런 함수만 모아 격리하면 나머지 코드가 자유로워진다.

---

### Task 1: SEH 보호 읽기와 영역 열거

**Files:**
- Create: `src/mem/safe_read.h`, `src/mem/safe_read.cpp`
- Create: `src/mem/regions.h`, `src/mem/regions.cpp`
- Create: `tests/safe_read_tests.cpp`, `tests/regions_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `cdtb::mem::Range` (기존 `mem/scanner.h`)
- Produces:
  - `bool cdtb::mem::safe_read_float(std::uintptr_t addr, float* out)`
  - `bool cdtb::mem::safe_read_bytes(std::uintptr_t addr, void* out, std::size_t n)`
  - `bool cdtb::mem::safe_write_float(std::uintptr_t addr, float value)`
  - `bool cdtb::mem::scan_region_floats(const std::uint8_t* base, std::size_t size, float target, float eps, std::uintptr_t* out, std::size_t cap, std::size_t* count)`
  - `std::vector<Range> cdtb::mem::writable_regions()`

- [ ] **Step 1: 작업 브랜치 생성**

```bash
cd /e/CDToybox
git checkout develop
git checkout -b feat/stage1-value-scanner
```

- [ ] **Step 2: 실패하는 테스트 작성**

`tests/safe_read_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/safe_read.h"

#include <cstring>

using namespace cdtb::mem;

TEST(safe_read_float_reads_valid_address) {
    float v = 3.5f;
    float out = 0.0f;
    CHECK(safe_read_float(reinterpret_cast<std::uintptr_t>(&v), &out));
    CHECK(out == 3.5f);
}

TEST(safe_read_float_rejects_null) {
    float out = 0.0f;
    CHECK(!safe_read_float(0, &out));
}

TEST(safe_read_float_survives_bad_address) {
    // 매핑되지 않은 주소. 크래시하지 않고 false를 반환해야 한다.
    float out = 0.0f;
    CHECK(!safe_read_float(0x10, &out));
}

TEST(safe_write_float_writes_valid_address) {
    float v = 1.0f;
    CHECK(safe_write_float(reinterpret_cast<std::uintptr_t>(&v), 9.25f));
    CHECK(v == 9.25f);
}

TEST(safe_write_float_survives_bad_address) {
    CHECK(!safe_write_float(0x10, 1.0f));
}

TEST(safe_read_bytes_copies_exact_length) {
    const std::uint8_t src[] = {1, 2, 3, 4, 5};
    std::uint8_t dst[5]{};
    CHECK(safe_read_bytes(reinterpret_cast<std::uintptr_t>(src), dst, 5));
    CHECK(std::memcmp(src, dst, 5) == 0);
}

TEST(scan_region_floats_finds_matches_at_aligned_offsets) {
    alignas(4) float buf[8] = {0.f, 1.5f, 0.f, 1.5f, 0.f, 0.f, 1.5f, 0.f};
    std::uintptr_t out[8]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    CHECK(scan_region_floats(base, sizeof(buf), 1.5f, 0.0001f, out, 8, &count));
    CHECK_EQ(count, std::size_t{3});
    CHECK_EQ(out[0], reinterpret_cast<std::uintptr_t>(&buf[1]));
    CHECK_EQ(out[1], reinterpret_cast<std::uintptr_t>(&buf[3]));
    CHECK_EQ(out[2], reinterpret_cast<std::uintptr_t>(&buf[6]));
}

TEST(scan_region_floats_honours_epsilon) {
    alignas(4) float buf[4] = {1.0f, 1.0005f, 1.01f, 2.0f};
    std::uintptr_t out[4]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    CHECK(scan_region_floats(base, sizeof(buf), 1.0f, 0.001f, out, 4, &count));
    CHECK_EQ(count, std::size_t{2});   // 1.0 과 1.0005 만
}

TEST(scan_region_floats_stops_at_capacity) {
    alignas(4) float buf[8] = {5.f, 5.f, 5.f, 5.f, 5.f, 5.f, 5.f, 5.f};
    std::uintptr_t out[3]{};
    std::size_t count = 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(buf);
    scan_region_floats(base, sizeof(buf), 5.0f, 0.0001f, out, 3, &count);
    CHECK_EQ(count, std::size_t{3});
}
```

`tests/regions_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/regions.h"
#include "mem/safe_read.h"

using namespace cdtb::mem;

TEST(writable_regions_is_not_empty) {
    const auto r = writable_regions();
    CHECK(!r.empty());
}

TEST(writable_regions_are_sane_and_readable) {
    const auto regions = writable_regions();
    for (const auto& r : regions) {
        CHECK(r.begin != nullptr);
        CHECK(r.size > 0);
    }
    // 첫 몇 개 영역의 선두 4바이트를 실제로 읽어 본다.
    int probed = 0;
    for (const auto& r : regions) {
        float v = 0.0f;
        CHECK(safe_read_float(reinterpret_cast<std::uintptr_t>(r.begin), &v));
        if (++probed >= 10) break;
    }
    CHECK(probed > 0);
}

TEST(writable_regions_contain_a_heap_allocation) {
    // 방금 할당한 메모리가 열거 결과 안에 있어야 한다.
    auto* p = new float(42.0f);
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    const auto regions = writable_regions();
    bool found = false;
    for (const auto& r : regions) {
        const auto b = reinterpret_cast<std::uintptr_t>(r.begin);
        if (addr >= b && addr < b + r.size) { found = true; break; }
    }
    CHECK(found);
    delete p;
}
```

- [ ] **Step 3: CMake 등록 후 컴파일 실패 확인**

`CMakeLists.txt`의 `cdtb_core` 소스에 `src/mem/safe_read.cpp`, `src/mem/regions.cpp`를 추가하고, `cdtb_tests` 소스에 `tests/safe_read_tests.cpp`, `tests/regions_tests.cpp`를 추가한다.

Run: `powershell -ExecutionPolicy Bypass -File E:\CDToybox\scripts\build.ps1`
Expected: FAIL — `mem/safe_read.h` 없음.

- [ ] **Step 4: safe_read 헤더 작성**

`src/mem/safe_read.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::mem {

// 아래 함수들은 전부 SEH로 감싸여 있어, 잘못된 주소를 받아도
// 크래시하지 않고 false를 반환한다.
//
// MSVC는 소멸자를 가진 C++ 객체가 있는 함수에서 __try를 허용하지
// 않으므로(C2712), 이 파일의 구현에는 그런 객체를 두지 않는다.

bool safe_read_float(std::uintptr_t addr, float* out);
bool safe_read_bytes(std::uintptr_t addr, void* out, std::size_t n);
bool safe_write_float(std::uintptr_t addr, float value);

// [base, base+size) 를 4바이트 정렬로 훑어 |v - target| <= eps 인
// 주소를 out에 채운다. cap에 도달하면 멈춘다.
// 영역이 도중에 해제되면 false를 반환한다(그때까지의 결과는 유효).
bool scan_region_floats(const std::uint8_t* base, std::size_t size,
                        float target, float eps, std::uintptr_t* out,
                        std::size_t cap, std::size_t* count);

}  // namespace cdtb::mem
```

- [ ] **Step 5: safe_read 구현 작성**

`src/mem/safe_read.cpp`:

```cpp
#include "mem/safe_read.h"

#include <windows.h>

#include <cmath>
#include <cstring>

namespace cdtb::mem {

bool safe_read_float(std::uintptr_t addr, float* out) {
    if (addr == 0 || out == nullptr) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read_bytes(std::uintptr_t addr, void* out, std::size_t n) {
    if (addr == 0 || out == nullptr || n == 0) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_float(std::uintptr_t addr, float value) {
    if (addr == 0) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), &value, sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool scan_region_floats(const std::uint8_t* base, std::size_t size,
                        float target, float eps, std::uintptr_t* out,
                        std::size_t cap, std::size_t* count) {
    if (base == nullptr || out == nullptr || count == nullptr) return false;
    __try {
        // 4바이트 정렬만 본다. 카메라 좌표와 FOV는 정렬된 float이고,
        // 정렬을 가정하면 후보 수와 스캔 시간이 모두 1/4로 준다.
        for (std::size_t i = 0; i + sizeof(float) <= size; i += 4) {
            float v;
            std::memcpy(&v, base + i, sizeof(float));
            if (std::fabs(v - target) <= eps) {
                if (*count >= cap) return true;
                out[(*count)++] = reinterpret_cast<std::uintptr_t>(base + i);
            }
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace cdtb::mem
```

- [ ] **Step 6: regions 헤더와 구현 작성**

`src/mem/regions.h`:

```cpp
#pragma once

#include "mem/scanner.h"

namespace cdtb::mem {

// VirtualQuery로 커밋되고 읽기·쓰기가 가능한 영역을 모은다.
// PAGE_GUARD / PAGE_NOACCESS / 실행 전용 페이지는 제외한다.
//
// 모듈 이미지의 데이터 섹션은 제외하지 않는다. 카메라 구조체를
// 가리키는 전역 포인터는 실행 파일의 .data 계열에 있을 가능성이
// 높고, 그것을 빼면 정작 필요한 것을 놓친다.
std::vector<Range> writable_regions();

// 열거된 영역들의 총 바이트. 진행률 표시에 쓴다.
std::size_t total_bytes(const std::vector<Range>& regions);

}  // namespace cdtb::mem
```

`src/mem/regions.cpp`:

```cpp
#include "mem/regions.h"

#include <windows.h>

namespace cdtb::mem {
namespace {

bool is_scannable(const MEMORY_BASIC_INFORMATION& mbi) {
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & PAGE_GUARD) return false;
    if (mbi.Protect & PAGE_NOACCESS) return false;

    const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (mbi.Protect & writable) != 0;
}

}  // namespace

std::vector<Range> writable_regions() {
    std::vector<Range> out;

    SYSTEM_INFO si{};
    ::GetSystemInfo(&si);

    auto addr = reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress);
    const auto limit =
        reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress);

    MEMORY_BASIC_INFORMATION mbi{};
    while (addr < limit) {
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi,
                           sizeof(mbi)) != sizeof(mbi)) {
            break;
        }
        if (is_scannable(mbi)) {
            out.push_back(
                Range{static_cast<const std::uint8_t*>(mbi.BaseAddress),
                      mbi.RegionSize});
        }
        const auto next = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) +
                          mbi.RegionSize;
        if (next <= addr) break;   // 전진하지 못하면 무한 루프다
        addr = next;
    }
    return out;
}

std::size_t total_bytes(const std::vector<Range>& regions) {
    std::size_t n = 0;
    for (const auto& r : regions) n += r.size;
    return n;
}

}  // namespace cdtb::mem
```

- [ ] **Step 7: 테스트 통과 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `45 tests, 0 failures` (기존 34 + 신규 11).

- [ ] **Step 8: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(mem): SEH 보호 읽기·쓰기와 메모리 영역 열거 추가

잘못된 주소를 받아도 크래시하지 않고 false를 반환한다. 게임
프로세스 안에서 도는 코드이므로 어떤 실패도 게임을 죽이면 안 된다.

SEH를 쓰는 함수만 safe_read.cpp에 격리했다. MSVC는 소멸자를 가진
C++ 객체가 있는 함수에서 __try를 허용하지 않으므로(C2712), 그런
함수를 한곳에 모으면 나머지 코드가 자유로워진다.

스캔은 4바이트 정렬만 본다. 카메라 좌표와 FOV는 정렬된 float이고,
정렬을 가정하면 후보 수와 시간이 모두 1/4로 준다."
```

---

### Task 2: FloatScan — 후보 좁히기

**Files:**
- Create: `src/mem/value_scanner.h`, `src/mem/value_scanner.cpp`
- Create: `tests/value_scanner_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `safe_read_float`, `scan_region_floats` (Task 1), `Range` (기존)
- Produces:
  - `class cdtb::mem::FloatScan`
    - `std::size_t first(const std::vector<Range>& regions, float target, float eps)`
    - `std::size_t narrow_equals(float target, float eps)`
    - `std::size_t narrow_changed()` / `narrow_unchanged()` / `narrow_increased()` / `narrow_decreased()`
    - `void reset()`
    - `std::size_t count() const`
    - `const std::vector<std::uintptr_t>& results() const`
    - `bool capped() const` — 최초 스캔이 상한에 걸렸는가

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/value_scanner_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/value_scanner.h"

#include <vector>

using namespace cdtb::mem;

namespace {

// 합성 버퍼를 하나의 영역으로 넘긴다. 실제 프로세스 메모리를
// 건드리지 않으므로 결정적이고 빠르다.
struct Fixture {
    alignas(4) float buf[8] = {1.0f, 2.0f, 1.0f, 3.0f,
                               1.0f, 4.0f, 5.0f, 1.0f};
    std::vector<Range> regions() const {
        return {Range{reinterpret_cast<const std::uint8_t*>(buf),
                      sizeof(buf)}};
    }
    std::uintptr_t at(int i) const {
        return reinterpret_cast<std::uintptr_t>(&buf[i]);
    }
};

}  // namespace

TEST(first_finds_all_matching_floats) {
    Fixture f;
    FloatScan s;
    CHECK_EQ(s.first(f.regions(), 1.0f, 0.0001f), std::size_t{4});
    CHECK_EQ(s.results()[0], f.at(0));
    CHECK_EQ(s.results()[3], f.at(7));
}

TEST(first_returns_zero_when_nothing_matches) {
    Fixture f;
    FloatScan s;
    CHECK_EQ(s.first(f.regions(), 99.0f, 0.0001f), std::size_t{0});
    CHECK(s.results().empty());
}

TEST(narrow_equals_keeps_only_still_matching) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 인덱스 0,2,4,7
    f.buf[2] = 7.0f;                          // 하나를 바꾼다
    f.buf[7] = 7.0f;
    CHECK_EQ(s.narrow_equals(1.0f, 0.0001f), std::size_t{2});
    CHECK_EQ(s.results()[0], f.at(0));
    CHECK_EQ(s.results()[1], f.at(4));
}

TEST(narrow_changed_keeps_only_changed) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[4] = 8.0f;
    CHECK_EQ(s.narrow_changed(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(4));
}

TEST(narrow_unchanged_keeps_only_unchanged) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[4] = 8.0f;
    CHECK_EQ(s.narrow_unchanged(), std::size_t{3});
}

TEST(narrow_increased_and_decreased) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 0,2,4,7
    f.buf[0] = 2.0f;    // 증가
    f.buf[2] = 0.5f;    // 감소
    CHECK_EQ(s.narrow_increased(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(0));
}

TEST(narrow_chain_converges_to_single_address) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);      // 4개
    f.buf[0] = 2.0f;
    s.narrow_changed();                        // 1개
    CHECK_EQ(s.count(), std::size_t{1});
    CHECK_EQ(s.results()[0], f.at(0));
}

TEST(reset_clears_everything) {
    Fixture f;
    FloatScan s;
    s.first(f.regions(), 1.0f, 0.0001f);
    s.reset();
    CHECK_EQ(s.count(), std::size_t{0});
    CHECK(!s.capped());
}

TEST(narrow_on_empty_scan_is_safe) {
    FloatScan s;
    CHECK_EQ(s.narrow_changed(), std::size_t{0});
    CHECK_EQ(s.narrow_equals(1.0f, 0.001f), std::size_t{0});
}
```

- [ ] **Step 2: CMake 등록 후 컴파일 실패 확인**

`cdtb_core`에 `src/mem/value_scanner.cpp`, `cdtb_tests`에 `tests/value_scanner_tests.cpp`를 추가한다.

Run: `powershell -ExecutionPolicy Bypass -File E:\CDToybox\scripts\build.ps1`
Expected: FAIL — `mem/value_scanner.h` 없음.

- [ ] **Step 3: 헤더 작성**

`src/mem/value_scanner.h`:

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mem/scanner.h"

namespace cdtb::mem {

// 값 기반 메모리 스캔. Cheat Engine의 "첫 스캔 → 재스캔" 흐름이다.
//
// 영역을 인자로 받으므로 합성 버퍼로 테스트할 수 있다. 실제 게임
// 메모리는 writable_regions()가 준다.
class FloatScan {
public:
    // 후보 상한. 넘으면 capped()가 true가 되고 거기서 멈춘다.
    // 8MB어치다. 이보다 많이 나오면 더 특징적인 값을 골라야 한다.
    static constexpr std::size_t kMaxCandidates = 1'000'000;

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
```

- [ ] **Step 4: 구현 작성**

`src/mem/value_scanner.cpp`:

```cpp
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
        // 읽을 수 없게 된 주소는 버린다.
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
```

- [ ] **Step 5: 테스트 통과 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `54 tests, 0 failures` (45 + 신규 9).

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(mem): 값 기반 메모리 스캐너 추가

Cheat Engine의 첫 스캔 -> 재스캔 흐름이다. 값이 얼마인지 몰라도
변함/안 변함/증가/감소로 좁힐 수 있어, 게임 내부 지식 없이
자료구조를 찾을 수 있다.

영역을 인자로 받게 해 합성 버퍼로 테스트한다. 실제 프로세스
메모리에 의존하지 않으므로 결정적이고 빠르다.

읽을 수 없게 된 주소는 재스캔에서 조용히 버린다. 게임이 메모리를
해제하는 것은 정상이고, 그걸로 죽으면 안 된다."
```

---

### Task 3: 오버레이 스캔 패널

**Files:**
- Create: `src/render/scan_panel.h`, `src/render/scan_panel.cpp`
- Modify: `src/render/overlay.cpp` (`draw_ui`에서 패널 호출)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `cdtb::mem::FloatScan`, `writable_regions()`, `total_bytes()`, `safe_read_float`, `safe_write_float`
- Produces:
  - `void cdtb::render::draw_scan_panel()` — ImGui 창 하나를 그린다
  - `void cdtb::render::shutdown_scan_panel()` — 워커 스레드 정리

- [ ] **Step 1: 헤더 작성**

`src/render/scan_panel.h`:

```cpp
#pragma once

namespace cdtb::render {

// 메모리 스캔 패널을 그린다. ImGui 프레임 안에서 부른다.
void draw_scan_panel();

// 워커 스레드를 정리한다. 오버레이 해체 시 부른다.
void shutdown_scan_panel();

}  // namespace cdtb::render
```

- [ ] **Step 2: 구현 작성**

`src/render/scan_panel.cpp`:

```cpp
#include "render/scan_panel.h"

#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#include "core/log.h"
#include "mem/regions.h"
#include "mem/safe_read.h"
#include "mem/value_scanner.h"

namespace cdtb::render {
namespace {

mem::FloatScan g_scan;

// 최초 스캔은 수 GB를 훑을 수 있다. 렌더 스레드에서 돌리면 게임이
// 멈추므로 워커로 보내고 진행률만 읽는다.
std::thread g_worker;
std::atomic<bool> g_busy{false};
std::atomic<std::uint64_t> g_total{0};

float g_target = 0.0f;
float g_eps = 0.001f;

// 관찰 고정. 후보 하나를 골라 실시간으로 값을 본다.
std::uintptr_t g_pinned = 0;
float g_write_value = 0.0f;

void start_first_scan() {
    if (g_busy.load()) return;
    if (g_worker.joinable()) g_worker.join();

    g_busy.store(true);
    const float target = g_target;
    const float eps = g_eps;
    g_worker = std::thread([target, eps]() {
        const auto regions = mem::writable_regions();
        g_total.store(mem::total_bytes(regions));
        const std::size_t n = g_scan.first(regions, target, eps);
        log::infof("첫 스캔: {} 영역, {} MB, 후보 {}개{}", regions.size(),
                   g_total.load() / (1024 * 1024), n,
                   g_scan.capped() ? " (상한 도달)" : "");
        g_busy.store(false);
    });
}

void draw_results() {
    ImGui::Text("후보 %zu개%s", g_scan.count(),
                g_scan.capped() ? "  [상한 도달 - 더 특징적인 값을 쓰세요]"
                                : "");
    if (g_scan.count() == 0) return;

    ImGui::BeginChild("results", ImVec2(0, 180), true);
    const std::size_t shown = g_scan.count() < 50 ? g_scan.count() : 50;
    for (std::size_t i = 0; i < shown; ++i) {
        const std::uintptr_t a = g_scan.results()[i];
        float v = 0.0f;
        const bool ok = mem::safe_read_float(a, &v);

        char label[64];
        std::snprintf(label, sizeof(label), "0x%llX",
                      static_cast<unsigned long long>(a));
        if (ImGui::Selectable(label, g_pinned == a)) g_pinned = a;
        ImGui::SameLine(200);
        if (ok) {
            ImGui::Text("%.6f", v);
        } else {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "읽기 실패");
        }
    }
    if (g_scan.count() > shown) {
        ImGui::TextDisabled("... %zu개 더", g_scan.count() - shown);
    }
    ImGui::EndChild();
}

void draw_pinned() {
    if (g_pinned == 0) return;
    ImGui::Separator();
    ImGui::Text("고정: 0x%llX", static_cast<unsigned long long>(g_pinned));

    float v = 0.0f;
    if (mem::safe_read_float(g_pinned, &v)) {
        ImGui::Text("현재값: %.6f", v);
    } else {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "읽을 수 없음");
    }

    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("쓸 값", &g_write_value);
    ImGui::SameLine();
    if (ImGui::Button("쓰기")) {
        const bool ok = mem::safe_write_float(g_pinned, g_write_value);
        log::infof("쓰기 0x{:X} <- {} : {}",
                   static_cast<unsigned long long>(g_pinned), g_write_value,
                   ok ? "성공" : "실패");
    }

    // 고정 주소 주변을 float 격자로 본다. 구조체 필드를 눈으로
    // 찾을 때 쓴다. -16 .. +31 슬롯.
    if (ImGui::CollapsingHeader("주변 덤프 (float)")) {
        for (int row = -4; row < 8; ++row) {
            const std::uintptr_t rowaddr =
                g_pinned + static_cast<std::intptr_t>(row) * 16;
            ImGui::Text("%+5d", row * 16);
            for (int col = 0; col < 4; ++col) {
                float f = 0.0f;
                const std::uintptr_t a = rowaddr + col * 4;
                ImGui::SameLine(60.0f + col * 110.0f);
                if (mem::safe_read_float(a, &f)) {
                    if (a == g_pinned) {
                        ImGui::TextColored(ImVec4(1, 1, 0.4f, 1), "%.4f", f);
                    } else {
                        ImGui::Text("%.4f", f);
                    }
                } else {
                    ImGui::TextDisabled("----");
                }
            }
        }
    }
}

}  // namespace

void draw_scan_panel() {
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    ImGui::Begin("메모리 스캔");

    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("찾을 값", &g_target);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputFloat("오차", &g_eps, 0.0f, 0.0f, "%.5f");

    if (g_busy.load()) {
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1), "스캔 중...  %llu / %llu MB",
                           g_scan.scanned_bytes() / (1024 * 1024),
                           g_total.load() / (1024 * 1024));
    } else {
        if (ImGui::Button("첫 스캔")) start_first_scan();
        ImGui::SameLine();
        if (ImGui::Button("초기화")) { g_scan.reset(); g_pinned = 0; }

        if (g_scan.count() > 0) {
            if (ImGui::Button("같음")) g_scan.narrow_equals(g_target, g_eps);
            ImGui::SameLine();
            if (ImGui::Button("변함")) g_scan.narrow_changed();
            ImGui::SameLine();
            if (ImGui::Button("안 변함")) g_scan.narrow_unchanged();
            ImGui::SameLine();
            if (ImGui::Button("증가")) g_scan.narrow_increased();
            ImGui::SameLine();
            if (ImGui::Button("감소")) g_scan.narrow_decreased();
        }
    }

    ImGui::Separator();
    draw_results();
    draw_pinned();
    ImGui::End();
}

void shutdown_scan_panel() {
    if (g_worker.joinable()) g_worker.join();
    g_scan.reset();
    g_pinned = 0;
}

}  // namespace cdtb::render
```

- [ ] **Step 3: overlay에 연결**

`src/render/overlay.cpp` 상단에 `#include "render/scan_panel.h"`를 추가하고, `draw_ui()`의 마지막 `ImGui::End();` **뒤**에 다음 한 줄을 넣는다.

```cpp
    cdtb::render::draw_scan_panel();
```

그리고 `teardown()`에서 `input::remove();` **앞**에 다음을 넣는다.

```cpp
    shutdown_scan_panel();   // 워커 스레드를 먼저 정리한다
```

`teardown`은 `cdtb::overlay::detail` 안이므로 `cdtb::render::shutdown_scan_panel()`로 완전한 이름을 쓴다.

- [ ] **Step 4: CMake 등록 후 빌드**

`cdtoybox` 소스에 `src/render/scan_panel.cpp`를 추가한다.

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: 빌드 성공, `54 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(render): 오버레이 메모리 스캔 패널 추가

첫 스캔은 워커 스레드에서 돌린다. 쓰기 가능 영역이 수 GB일 수
있어 렌더 스레드에서 돌리면 게임이 멈춘다. 진행률만 읽어 표시한다.

후보 하나를 고정해 실시간 값을 보고, 주변을 float 격자로 덤프한다.
구조체 필드를 눈으로 찾을 때 쓴다."
```

---

### Task 4: 카메라 접근 계층과 guard 개방

**Files:**
- Create: `src/game/camera.h`, `src/game/camera.cpp`
- Modify: `src/core/guard.cpp`
- Modify: `src/render/scan_panel.cpp` (카메라 패널 추가)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `safe_read_float`, `safe_write_float` (Task 1)
- Produces:
  - `struct cdtb::game::CameraOffsets { int pos_x, pos_y, pos_z, rot_pitch, rot_yaw, rot_roll, fov; }` — 바이트 오프셋, `-1`이면 미확정
  - `struct cdtb::game::CameraView { float pos[3]; float rot[3]; float fov; }`
  - `void cdtb::game::set_base(std::uintptr_t)` / `std::uintptr_t cdtb::game::base()`
  - `void cdtb::game::set_offsets(const CameraOffsets&)` / `CameraOffsets cdtb::game::offsets()`
  - `bool cdtb::game::read_view(CameraView* out)` — 확정된 필드만 채우고, 미확정은 0
  - `bool cdtb::game::write_fov(float)`
  - `void cdtb::render::draw_camera_panel()`

- [ ] **Step 1: 헤더 작성**

`src/game/camera.h`:

```cpp
#pragma once

#include <cstdint>

namespace cdtb::game {

// 바이트 오프셋. -1은 아직 확정되지 않았다는 뜻이다.
//
// 값을 런타임에 바꿀 수 있게 두는 것이 핵심이다. 역공학은
// 추측과 확인의 반복인데, 오프셋이 코드에 박혀 있으면 추측마다
// 빌드·배포·게임 재시작이 필요하다.
struct CameraOffsets {
    int pos_x = -1;
    int pos_y = -1;
    int pos_z = -1;
    int rot_pitch = -1;
    int rot_yaw = -1;
    int rot_roll = -1;
    int fov = -1;
};

struct CameraView {
    float pos[3]{};
    float rot[3]{};
    float fov = 0.0f;
};

void set_base(std::uintptr_t addr);
std::uintptr_t base();

void set_offsets(const CameraOffsets& o);
CameraOffsets offsets();

// 확정된 오프셋의 필드만 채운다. 베이스가 0이면 false.
bool read_view(CameraView* out);

bool write_fov(float value);

}  // namespace cdtb::game
```

- [ ] **Step 2: 구현 작성**

`src/game/camera.cpp`:

```cpp
#include "game/camera.h"

#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

std::uintptr_t g_base = 0;
CameraOffsets g_off;

bool read_at(int off, float* out) {
    if (off < 0 || g_base == 0) return false;
    return mem::safe_read_float(g_base + static_cast<std::uintptr_t>(off),
                                out);
}

}  // namespace

void set_base(std::uintptr_t addr) { g_base = addr; }
std::uintptr_t base() { return g_base; }

void set_offsets(const CameraOffsets& o) { g_off = o; }
CameraOffsets offsets() { return g_off; }

bool read_view(CameraView* out) {
    if (out == nullptr || g_base == 0) return false;
    read_at(g_off.pos_x, &out->pos[0]);
    read_at(g_off.pos_y, &out->pos[1]);
    read_at(g_off.pos_z, &out->pos[2]);
    read_at(g_off.rot_pitch, &out->rot[0]);
    read_at(g_off.rot_yaw, &out->rot[1]);
    read_at(g_off.rot_roll, &out->rot[2]);
    read_at(g_off.fov, &out->fov);
    return true;
}

bool write_fov(float value) {
    if (g_base == 0 || g_off.fov < 0) return false;
    return mem::safe_write_float(
        g_base + static_cast<std::uintptr_t>(g_off.fov), value);
}

}  // namespace cdtb::game
```

- [ ] **Step 3: guard 개방**

`src/core/guard.cpp`를 통째로 다음으로 바꾼다.

```cpp
#include "core/guard.h"

namespace cdtb::guard {

bool is_safe_to_modify() {
    // Crimson Desert는 2026-03-19 출시 시점부터 현재 빌드(2.00.01)까지
    // co-op / PvP / 매치메이킹이 전무한 순수 싱글플레이다. 0단계에서
    // 온라인 인프라로 의심한 PartyManager / GuildManager는 BlackSpace
    // 엔진이 BDO와 공유하는 스캐폴딩이고, LinkingCheckAsync는 계정
    // 연동이지 게임 세션이 아니다.
    //
    // 관문 자체는 남긴다. Pearl Abyss가 멀티플레이를 검토 중이라고
    // 밝혔으므로, 출시되면 이 함수 하나만 고쳐 모든 쓰기 기능을
    // 한곳에서 차단할 수 있어야 한다.
    return true;
}

}  // namespace cdtb::guard
```

- [ ] **Step 4: 카메라 패널 추가**

`src/render/scan_panel.cpp` 상단에 `#include "game/camera.h"`와 `#include "core/guard.h"`를 추가하고, `void draw_scan_panel()` **앞**에 다음 함수를 익명 네임스페이스 밖(`cdtb::render` 안)에 추가한다.

```cpp
void draw_camera_panel() {
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("카메라");

    static char base_buf[32] = "0";
    ImGui::SetNextItemWidth(220);
    ImGui::InputText("베이스 (16진)", base_buf, sizeof(base_buf),
                     ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    if (ImGui::Button("적용")) {
        game::set_base(std::strtoull(base_buf, nullptr, 16));
    }
    ImGui::SameLine();
    if (ImGui::Button("고정 주소 사용")) {
        std::snprintf(base_buf, sizeof(base_buf), "%llX",
                      static_cast<unsigned long long>(g_pinned));
        game::set_base(g_pinned);
    }

    ImGui::Text("현재 베이스: 0x%llX",
                static_cast<unsigned long long>(game::base()));
    ImGui::Separator();

    game::CameraOffsets o = game::offsets();
    bool changed = false;
    ImGui::TextDisabled("오프셋 (-1 = 미확정)");
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("pos.x", &o.pos_x);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("pos.y", &o.pos_y);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("pos.z", &o.pos_z);
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("pitch", &o.rot_pitch);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("yaw", &o.rot_yaw);
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("roll", &o.rot_roll);
    ImGui::SetNextItemWidth(90);
    changed |= ImGui::InputInt("fov", &o.fov);
    if (changed) game::set_offsets(o);

    ImGui::Separator();
    game::CameraView v;
    if (game::read_view(&v)) {
        ImGui::Text("pos  %10.3f  %10.3f  %10.3f", v.pos[0], v.pos[1],
                    v.pos[2]);
        ImGui::Text("rot  %10.4f  %10.4f  %10.4f", v.rot[0], v.rot[1],
                    v.rot[2]);
        ImGui::Text("fov  %10.4f", v.fov);
    } else {
        ImGui::TextDisabled("베이스가 설정되지 않았습니다");
    }

    ImGui::Separator();
    static float fov_write = 60.0f;
    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("FOV 값", &fov_write);
    ImGui::SameLine();
    if (!guard::is_safe_to_modify()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "쓰기 차단됨");
    } else if (ImGui::Button("FOV 쓰기")) {
        log::infof("FOV 쓰기 {} : {}", fov_write,
                   game::write_fov(fov_write) ? "성공" : "실패");
    }
    ImGui::End();
}
```

`draw_scan_panel()`의 `ImGui::End();` 뒤에서 이 함수를 부른다. `scan_panel.h`에 선언을 추가한다.

```cpp
void draw_camera_panel();
```

`<cstdlib>`(`strtoull`)를 include 한다.

- [ ] **Step 5: 빌드하고 배포**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: 빌드 성공, `54 tests, 0 failures`.

게임이 종료된 상태에서:

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1
rm -f "E:/SteamLibrary/steamapps/common/Crimson Desert/bin64/CDToybox.log"
```

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(game): 카메라 접근 계층 추가하고 쓰기 관문을 연다

베이스 주소와 오프셋을 런타임에 바꿀 수 있게 뒀다. 역공학은
추측과 확인의 반복인데, 오프셋이 코드에 박혀 있으면 추측마다
빌드와 게임 재시작이 필요하다. 0단계에서 그 비용이 얼마나 큰지
겪었다.

guard를 연다. 이 게임은 출시부터 현재 빌드까지 co-op과 PvP가
전무한 순수 싱글플레이임을 확인했다. 관문 자체는 남겨 멀티플레이가
출시되면 이 함수 하나로 전부 차단할 수 있게 한다."
```

---

### Task 5: 인게임 역공학 세션 — M1 완료와 M2 수행

이 태스크는 코드를 쓰지 않는다. Task 1~4가 만든 도구로 게임에서 값을 찾는 **탐색 활동**이며, 산출물은 확정된 오프셋 숫자다.

**Files:**
- Modify: `src/game/camera.cpp` (찾은 오프셋을 기본값으로 굳힐 때만)
- Create: `docs/superpowers/specs/2026-08-31-camera-offsets.md` (발견 기록)

**Interfaces:**
- Consumes: Task 4의 오버레이 카메라 패널과 스캔 패널
- Produces: `CameraOffsets`의 확정 값, 카메라 베이스를 얻는 방법의 기록

- [ ] **Step 1: 게임 저장 후 스캔 준비**

게임을 실행하고 **반드시 세이브한다.** 값을 쓰는 실험을 하므로 되돌릴 지점이 필요하다.

`Insert`로 오버레이를 연다. "메모리 스캔" 창이 보여야 한다.

- [ ] **Step 2: FOV 후보 좁히기**

FOV는 값을 모르므로 "변함/안 변함"으로 좁힌다.

1. `찾을 값`에 아무 값(예: `0`), `오차`에 `100000` 을 넣고 `첫 스캔` — 사실상 모든 float을 후보로 잡는다. 상한(100만)에 걸리면 대신 3번 방법을 쓴다.
2. 게임에서 시야각이 바뀌는 동작(달리기, 조준, 줌)을 한 뒤 `변함`
3. 시야각이 그대로인 상태에서 `안 변함`
4. 2~3을 번갈아 반복해 후보를 수십 개 이하로 줄인다

상한에 걸리면: FOV는 보통 `50`~`90` 사이의 도(degree) 또는 `0.8`~`1.6` 라디안이다. `찾을 값 = 60`, `오차 = 30`으로 첫 스캔해 범위를 좁힌 뒤 위 절차를 이어간다.

- [ ] **Step 3: 후보를 하나씩 검증**

후보를 클릭해 고정하고, "카메라" 창에서 `고정 주소 사용` → `fov` 오프셋에 `0` → `FOV 값`에 평소와 크게 다른 값(예: `20` 또는 `120`)을 넣고 `FOV 쓰기`.

**M1 완료 기준**: 화면 시야각이 눈에 띄게 변한다. 변하면 그 주소가 FOV이고, 카메라 구조체를 찾은 것이다.

변하지 않으면 다음 후보로 넘어간다. 값이 즉시 되돌아가면 게임이 매 프레임 덮어쓰는 것이므로 **그 사실을 기록한다** — M3의 접근을 결정하는 증거다.

- [ ] **Step 4: 구조체 둘러보기 (M2)**

FOV 주소를 고정한 상태에서 "주변 덤프 (float)"를 편다.

캐릭터를 움직이며 어떤 슬롯이 좌표처럼 변하는지 관찰한다. 위치는 보통 **연속한 3개 float**이고, 값의 크기가 서로 비슷하며, 이동 방향에 따라 함께 변한다. 회전은 시점을 돌릴 때 변하고, 라디안이면 `-3.14`~`3.14`, 도면 `-180`~`180` 범위다.

찾은 슬롯의 오프셋을 "카메라" 창의 `pos.x` 등에 입력하면 값이 바로 표시된다. 빌드가 필요 없다.

- [ ] **Step 5: 오프셋 검증**

**M2 완료 기준** 두 가지를 모두 만족해야 한다.

1. 캐릭터를 특정 방향으로 이동하면 해당 좌표 하나가 단조롭게 변한다
2. 시점을 좌우로 돌리면 yaw가, 위아래로 돌리면 pitch가 변한다

- [ ] **Step 6: 발견 기록**

`docs/superpowers/specs/2026-08-31-camera-offsets.md`에 다음을 적는다.

- 확정된 오프셋 표 (`pos_x` 등, 바이트 단위)
- 회전의 단위(라디안/도)와 범위
- 카메라 베이스 주소를 얻은 방법과 그 값이 재실행 시 바뀌는지
- **M3 결정 증거**: 값을 쓴 뒤 다음 프레임에 유지되는가, 덮어써지는가

- [ ] **Step 7: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "docs: 카메라 오프셋 확정

M1과 M2를 인게임 세션으로 완료했다. FOV 쓰기로 시야각 변화를
확인해 카메라 구조체를 특정했고, 주변 덤프로 위치와 회전
오프셋을 확정했다.

M3 접근 결정에 필요한 증거도 함께 기록했다."
```

---

## Self-Review

**스펙 커버리지**

| 스펙 항목 | 담당 |
|---|---|
| §4.1 `writable_regions()` | Task 1 |
| §4.1 `FloatScan` 전 메서드 | Task 2 |
| §4.2 `game/camera` | Task 4 |
| §4.3 오버레이 스캔 패널(입력·버튼·목록·고정·진행률) | Task 3 |
| §4.4 `guard` 개방 | Task 4 Step 3 |
| §5 M1 절차와 완료 기준 | Task 5 Step 1~3 |
| §5 M2 절차와 완료 기준 | Task 5 Step 4~5 |
| §5 M3 결정 증거 수집 | Task 5 Step 3, Step 6 |
| §8 리스크 1 (워커 스레드 + 진행률) | Task 3 |
| §8 리스크 2 (SEH 영역 건너뛰기) | Task 1 |
| §8 리스크 3 (주소 불안정) | Task 5 Step 6에 기록. 안정화는 M2 이후 별도 |

누락 없음. M3·M4는 이 계획의 범위가 아니며, 스펙 §6에 따라 M2 결과를 본 뒤 별도로 계획한다.

**타입 일관성**

- `Range` — 기존 `mem/scanner.h`. Task 1·2에서 동일하게 사용.
- `scan_region_floats` — Task 1에서 정의, Task 2 `FloatScan::first`가 호출. 시그니처 일치.
- `safe_read_float(std::uintptr_t, float*)` — Task 1 정의, Task 2·3·4가 사용. 포인터 인자 형태 일관.
- `CameraOffsets` / `CameraView` — Task 4에서 정의, 같은 태스크의 패널이 사용.
- `g_pinned` — Task 3의 익명 네임스페이스 변수. Task 4의 `draw_camera_panel`이 같은 번역 단위(`scan_panel.cpp`)에 있으므로 접근 가능.

**미해결로 남긴 것**

- 카메라 베이스 주소는 M1에서 절대주소로 다룬다. 게임 재실행마다 달라지므로 시그니처나 포인터 체인으로 안정화해야 하지만, 그것은 오프셋이 확정된 뒤의 작업이다. Task 5 Step 6에서 재실행 시 변하는지를 기록해 다음 계획의 입력으로 삼는다.
- `FloatScan::first`가 `kMaxCandidates`만큼(8MB) 미리 할당한다. 첫 스캔 직후 `resize`로 줄이므로 상주 비용은 아니다.
