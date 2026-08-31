# CDToybox 0단계 구현 계획 — 오버레이 + 스캐너 골대

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Crimson Desert 2.00.01 프로세스 안에서 우리 코드가 실행되고, D3D12 화면에 ImGui 오버레이를 그리고, 실행 섹션을 패턴으로 탐색할 수 있음을 증명하는 기반을 만든다.

**Architecture:** `xinput1_4.dll` 프록시로 프로세스에 진입한다(게임이 정적 import). D3D12/DXGI vtable을 더미 객체로 획득해 MinHook로 `Present`·`ResizeBuffers`·`ExecuteCommandLists`를 후킹하고, `Present` 안에서 ImGui를 그린다. 게임 지식이 필요한 코드는 전부 1단계 이후로 미루고, 0단계는 `mem`·`core`·`render`·`input` 네 인터페이스만 세운다.

**Tech Stack:** C++20 / MSVC 14.44 x64 / CMake + Ninja (VS Build Tools 번들) / Dear ImGui (win32 + dx12 백엔드) / MinHook

**Spec:** `docs/superpowers/specs/2026-08-31-stage0-scaffold-design.md`

## Global Constraints

- 언어 표준: **C++20**. 컴파일러: **MSVC 14.44.35207 x64**. 생성기: **Ninja**.
- CMake·Ninja는 PATH에 없다. 반드시 다음 절대경로를 쓴다.
  - `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`
  - `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe`
  - `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat`
- DLL 산출물 이름은 반드시 **`xinput1_4.dll`** 이다.
- 게임 경로: `E:\SteamLibrary\steamapps\common\Crimson Desert\bin64`
- 네임스페이스 루트는 `cdtb`.
- **어떤 실패도 게임을 죽이지 않는다.** 초기화·훅 실패는 로그만 남기고 게임은 정상 진행한다.
- 스캔 범위는 섹션 **이름**이 아니라 `IMAGE_SCN_MEM_EXECUTE` **플래그**로 판정한다. 이 게임은 실행 코드가 `.text`(12KB)가 아니라 `.text1`(21MB)에 있다.
- 게임 상태를 **쓰는** 기능은 0단계에서 일절 만들지 않는다.
- 브랜치: `develop`에서 `feat/stage0-scaffold`를 분기해 작업한다. `main`에는 직접 커밋하지 않는다.

---

## File Structure

| 파일 | 책임 |
|---|---|
| `CMakeLists.txt` | 빌드 정의. `cdtb_core`(정적) · `cdtoybox`(DLL) · `cdtb_tests`(실행) 세 타깃 |
| `scripts/build.ps1` | vcvarsall 활성화 + 구성 + 빌드 |
| `scripts/deploy.ps1` | 산출물을 게임 bin64로 복사 |
| `tests/harness.h` · `tests/main.cpp` | 외부 의존 없는 최소 테스트 하네스 |
| `src/mem/scanner.{h,cpp}` | 패턴 파싱과 바이트 범위 검색. **게임·Windows 무관 순수 함수** |
| `src/mem/module.{h,cpp}` | 모듈 베이스 조회, 실행 가능 섹션 범위 열거 |
| `src/mem/hook.{h,cpp}` | MinHook RAII 래퍼 |
| `src/core/log.{h,cpp}` | 파일 로거 |
| `src/core/config.{h,cpp}` | INI 로드·저장 |
| `src/core/guard.{h,cpp}` | 온라인 세션 가드 (0단계는 stub) |
| `src/proxy/xinput_proxy.{h,cpp}` · `exports.def` | 원본 XInput 위임 스텁 7개 |
| `src/dllmain.cpp` | DllMain + 초기화 워커 스레드 |
| `src/render/d3d12_hook.{h,cpp}` | vtable 주소 획득, 훅 3개, 프레임 진입점 |
| `src/render/overlay.{h,cpp}` | ImGui 수명 관리와 진단 UI |
| `src/input/wndproc.{h,cpp}` | WndProc 서브클래싱, 토글 키 |

`scanner`가 순수 함수인 것이 핵심이다. 0단계에서 자동 테스트가 가능한 유일한 지점이고, 1단계 이후 모든 역공학이 이 위에서 돌아간다.

---

### Task 1: 빌드 기반과 테스트 하네스

**Files:**
- Create: `.gitmodules` (submodule 추가로 자동 생성)
- Create: `CMakeLists.txt`
- Create: `scripts/build.ps1`
- Create: `tests/harness.h`
- Create: `tests/main.cpp`
- Create: `tests/smoke_tests.cpp`

**Interfaces:**
- Consumes: 없음 (최초 태스크)
- Produces: `TEST(name)` / `CHECK(cond)` / `CHECK_EQ(a,b)` 매크로, `registry()` 반환 `std::vector<TestCase>&`, 전역 `int g_failures`. CMake 타깃 이름 `cdtb_core` · `cdtoybox` · `cdtb_tests`.

- [ ] **Step 1: 작업 브랜치 생성 및 의존성 추가**

```bash
cd /e/CDToybox
git checkout develop
git checkout -b feat/stage0-scaffold
git submodule add https://github.com/ocornut/imgui.git external/imgui
git submodule add https://github.com/TsudaKageyu/minhook.git external/minhook
```

- [ ] **Step 2: 의존성 취득 확인**

```bash
ls external/imgui/imgui.cpp external/imgui/backends/imgui_impl_dx12.cpp external/minhook/src/hook.c
```

Expected: 세 파일 모두 존재.

- [ ] **Step 3: 테스트 하네스 작성**

`tests/harness.h`:

```cpp
#pragma once
#include <cstdio>
#include <vector>

struct TestCase { const char* name; void (*fn)(); };
std::vector<TestCase>& registry();
extern int g_failures;

#define TEST(name)                                                            \
    static void name();                                                       \
    static struct name##_reg_t {                                              \
        name##_reg_t() { registry().push_back({#name, name}); }               \
    } name##_reg_inst;                                                        \
    static void name()

#define CHECK(cond)                                                           \
    do {                                                                      \
        if (!(cond)) {                                                        \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        auto _a = (a);                                                        \
        auto _b = (b);                                                        \
        if (!(_a == _b)) {                                                    \
            std::printf("    FAIL %s:%d  %s == %s\n", __FILE__, __LINE__,     \
                        #a, #b);                                              \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)
```

`tests/main.cpp`:

```cpp
#include "harness.h"

int g_failures = 0;

std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

int main() {
    int total = 0;
    for (auto& tc : registry()) {
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.fn();
        std::printf(g_failures == before ? "[  OK  ] %s\n" : "[ FAIL ] %s\n",
                    tc.name);
        ++total;
    }
    std::printf("\n%d tests, %d failures\n", total, g_failures);
    return g_failures ? 1 : 0;
}
```

`tests/smoke_tests.cpp`:

```cpp
#include "harness.h"

TEST(harness_reports_success) {
    CHECK(true);
    CHECK_EQ(1 + 1, 2);
}
```

- [ ] **Step 4: CMakeLists.txt 작성**

`cdtb_core`는 아직 소스가 없으므로 이 태스크에서는 만들지 않는다. Task 2에서 첫 소스와 함께 추가한다.

```cmake
cmake_minimum_required(VERSION 3.20)
project(CDToybox CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------- MinHook
add_library(minhook STATIC
    external/minhook/src/buffer.c
    external/minhook/src/hook.c
    external/minhook/src/trampoline.c
    external/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC external/minhook/include)

# ------------------------------------------------------------------ ImGui
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_win32.cpp
    external/imgui/backends/imgui_impl_dx12.cpp
)
target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
)

# ------------------------------------------------------------------ Tests
add_executable(cdtb_tests
    tests/main.cpp
    tests/smoke_tests.cpp
)
target_include_directories(cdtb_tests PRIVATE tests src)
# 후킹 테스트에서 대상 함수가 인라인/폴딩되지 않도록 최적화를 끈다.
target_compile_options(cdtb_tests PRIVATE /Od)
target_link_options(cdtb_tests PRIVATE /OPT:NOICF)
```

- [ ] **Step 5: 빌드 스크립트 작성**

`scripts/build.ps1`:

```powershell
$ErrorActionPreference = "Stop"
$BT      = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$Cmake   = "$BT\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$Ninja   = "$BT\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$VcVars  = "$BT\VC\Auxiliary\Build\vcvarsall.bat"
$Root    = Split-Path -Parent $PSScriptRoot
$Build   = Join-Path $Root "build"

$configure = "`"$Cmake`" -S `"$Root`" -B `"$Build`" -G Ninja " +
             "-DCMAKE_MAKE_PROGRAM=`"$Ninja`" -DCMAKE_BUILD_TYPE=RelWithDebInfo"
$compile   = "`"$Cmake`" --build `"$Build`""

cmd /c "`"$VcVars`" x64 >nul && $configure && $compile"
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Host "build ok -> $Build"
```

- [ ] **Step 6: 빌드하고 테스트가 통과하는지 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: 빌드 성공. `1 tests, 0 failures` 출력, 종료 코드 0.

- [ ] **Step 7: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "build: CMake 빌드 기반과 테스트 하네스 추가

ImGui와 MinHook을 submodule로 고정하고, 외부 의존이 없는
최소 테스트 하네스를 세운다. 후킹 테스트에서 대상 함수가
인라인되거나 COMDAT 폴딩되지 않도록 테스트 타깃만 /Od와
/OPT:NOICF로 빌드한다."
```

---

### Task 2: 패턴 스캐너 (TDD)

**Files:**
- Create: `src/mem/scanner.h`
- Create: `src/mem/scanner.cpp`
- Create: `tests/scanner_tests.cpp`
- Modify: `CMakeLists.txt` (`cdtb_core` 타깃 신설, 테스트에 링크)

**Interfaces:**
- Consumes: Task 1의 `TEST` / `CHECK` / `CHECK_EQ`
- Produces:
  - `cdtb::mem::PatternBytes` = `std::vector<std::optional<std::uint8_t>>`
  - `cdtb::mem::Range { const std::uint8_t* begin; std::size_t size; }`
  - `std::optional<PatternBytes> cdtb::mem::parse_pattern(std::string_view)`
  - `const std::uint8_t* cdtb::mem::find_first(Range, const PatternBytes&)`
  - `std::vector<const std::uint8_t*> cdtb::mem::find_all(Range, const PatternBytes&, std::size_t max)`
  - CMake 타깃 `cdtb_core`

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/scanner_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/scanner.h"

using namespace cdtb::mem;

TEST(parse_plain_bytes) {
    auto p = parse_pattern("48 8B 05");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).has_value() && p->at(0).value() == 0x48);
    CHECK(p->at(1).has_value() && p->at(1).value() == 0x8B);
    CHECK(p->at(2).has_value() && p->at(2).value() == 0x05);
}

TEST(parse_wildcard) {
    auto p = parse_pattern("48 ?? 05");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).has_value());
    CHECK(!p->at(1).has_value());
    CHECK(p->at(2).has_value());
}

TEST(parse_is_case_insensitive_and_tolerates_extra_spaces) {
    auto p = parse_pattern("  4d ?  0F  ");
    CHECK(p.has_value());
    CHECK_EQ(p->size(), std::size_t{3});
    CHECK(p->at(0).value() == 0x4D);
    CHECK(!p->at(1).has_value());
    CHECK(p->at(2).value() == 0x0F);
}

TEST(parse_rejects_bad_input) {
    CHECK(!parse_pattern("").has_value());
    CHECK(!parse_pattern("   ").has_value());
    CHECK(!parse_pattern("ZZ").has_value());
    CHECK(!parse_pattern("48 8").has_value());
    CHECK(!parse_pattern("48 8B0").has_value());
}

static const std::uint8_t kBuf[] = {
    0x48, 0x8B, 0x05, 0x11, 0x22, 0x33, 0x44,
    0x90, 0x90,
    0x48, 0x8B, 0x05, 0xAA, 0xBB, 0xCC, 0xDD,
};
static Range buf() { return Range{kBuf, sizeof(kBuf)}; }

TEST(find_first_at_start) {
    auto p = parse_pattern("48 8B 05");
    CHECK_EQ(find_first(buf(), *p), kBuf);
}

TEST(find_first_at_end) {
    auto p = parse_pattern("AA BB CC DD");
    CHECK_EQ(find_first(buf(), *p), kBuf + 12);
}

TEST(find_first_returns_null_when_absent) {
    auto p = parse_pattern("DE AD BE EF");
    CHECK_EQ(find_first(buf(), *p), nullptr);
}

TEST(find_first_honours_wildcards) {
    auto p = parse_pattern("48 8B 05 ?? ?? CC DD");
    CHECK_EQ(find_first(buf(), *p), kBuf + 9);
}

TEST(find_first_rejects_pattern_longer_than_range) {
    auto p = parse_pattern("48 8B 05");
    CHECK_EQ(find_first(Range{kBuf, 2}, *p), nullptr);
}

TEST(find_first_handles_empty_range) {
    auto p = parse_pattern("48");
    CHECK_EQ(find_first(Range{nullptr, 0}, *p), nullptr);
}

TEST(find_all_collects_every_hit_in_order) {
    auto p = parse_pattern("48 8B 05");
    auto hits = find_all(buf(), *p, 16);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits[0], kBuf);
    CHECK_EQ(hits[1], kBuf + 9);
}

TEST(find_all_respects_max) {
    auto p = parse_pattern("48 8B 05");
    auto hits = find_all(buf(), *p, 1);
    CHECK_EQ(hits.size(), std::size_t{1});
    CHECK_EQ(hits[0], kBuf);
}
```

- [ ] **Step 2: `cdtb_core` 타깃을 추가하고 테스트가 컴파일 실패하는 것을 확인**

`CMakeLists.txt`의 `# ---- Tests` 블록 **위**에 다음을 넣는다.

```cmake
# ------------------------------------------------------------------- Core
add_library(cdtb_core STATIC
    src/mem/scanner.cpp
)
target_include_directories(cdtb_core PUBLIC src)
```

그리고 테스트 타깃을 수정한다.

```cmake
add_executable(cdtb_tests
    tests/main.cpp
    tests/smoke_tests.cpp
    tests/scanner_tests.cpp
)
target_include_directories(cdtb_tests PRIVATE tests)
target_link_libraries(cdtb_tests PRIVATE cdtb_core)
target_compile_options(cdtb_tests PRIVATE /Od)
target_link_options(cdtb_tests PRIVATE /OPT:NOICF)
```

Run: `powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: FAIL — `mem/scanner.h` 를 찾을 수 없다는 컴파일 오류.

- [ ] **Step 3: 헤더 작성**

`src/mem/scanner.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cdtb::mem {

// nullopt 원소는 와일드카드(??)를 뜻한다.
using PatternBytes = std::vector<std::optional<std::uint8_t>>;

struct Range {
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
};

// "48 8B 05 ?? ?? ?? ??" 형식을 파싱한다.
// 공백 구분, 대소문자 무관, 와일드카드는 ? 또는 ??.
// 토큰이 하나도 없거나 잘못된 토큰이 있으면 nullopt.
std::optional<PatternBytes> parse_pattern(std::string_view text);

// 첫 일치 위치. 없으면 nullptr.
const std::uint8_t* find_first(Range range, const PatternBytes& pattern);

// 최대 max개의 일치 위치를 앞에서부터 모은다.
std::vector<const std::uint8_t*> find_all(Range range,
                                          const PatternBytes& pattern,
                                          std::size_t max);

}  // namespace cdtb::mem
```

- [ ] **Step 4: 구현 작성**

`src/mem/scanner.cpp`:

```cpp
#include "mem/scanner.h"

namespace cdtb::mem {
namespace {

// 16진 문자 하나를 0~15로. 실패 시 -1.
int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

}  // namespace

std::optional<PatternBytes> parse_pattern(std::string_view text) {
    PatternBytes out;
    std::size_t i = 0;

    while (i < text.size()) {
        if (is_space(text[i])) { ++i; continue; }

        // 토큰의 끝을 찾는다.
        std::size_t start = i;
        while (i < text.size() && !is_space(text[i])) ++i;
        const std::string_view tok = text.substr(start, i - start);

        if (tok == "?" || tok == "??") {
            out.push_back(std::nullopt);
            continue;
        }
        if (tok.size() != 2) return std::nullopt;

        const int hi = hex_value(tok[0]);
        const int lo = hex_value(tok[1]);
        if (hi < 0 || lo < 0) return std::nullopt;

        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    if (out.empty()) return std::nullopt;
    return out;
}

const std::uint8_t* find_first(Range range, const PatternBytes& pattern) {
    if (range.begin == nullptr || pattern.empty()) return nullptr;
    if (range.size < pattern.size()) return nullptr;

    const std::size_t last = range.size - pattern.size();
    for (std::size_t off = 0; off <= last; ++off) {
        const std::uint8_t* at = range.begin + off;
        bool matched = true;
        for (std::size_t k = 0; k < pattern.size(); ++k) {
            const auto& want = pattern[k];
            if (want.has_value() && at[k] != want.value()) {
                matched = false;
                break;
            }
        }
        if (matched) return at;
    }
    return nullptr;
}

std::vector<const std::uint8_t*> find_all(Range range,
                                          const PatternBytes& pattern,
                                          std::size_t max) {
    std::vector<const std::uint8_t*> hits;
    if (range.begin == nullptr || pattern.empty() || max == 0) return hits;
    if (range.size < pattern.size()) return hits;

    Range cursor = range;
    while (hits.size() < max) {
        const std::uint8_t* hit = find_first(cursor, pattern);
        if (hit == nullptr) break;
        hits.push_back(hit);

        const std::size_t consumed =
            static_cast<std::size_t>(hit - cursor.begin) + 1;
        if (consumed >= cursor.size) break;
        cursor.begin += consumed;
        cursor.size -= consumed;
    }
    return hits;
}

}  // namespace cdtb::mem
```

- [ ] **Step 5: 테스트가 통과하는지 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `13 tests, 0 failures`, 종료 코드 0.

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(mem): 패턴 스캐너 추가

IDA 스타일 패턴 문자열을 파싱하고 바이트 범위에서 검색한다.
Windows나 게임에 의존하지 않는 순수 함수로 두어 게임 없이
검증할 수 있게 했다. 1단계 이후의 역공학 작업은 전부 이
인터페이스 위에서 이뤄진다."
```

---

### Task 3: 모듈 조회와 실행 섹션 열거

**Files:**
- Create: `src/mem/module.h`
- Create: `src/mem/module.cpp`
- Create: `tests/module_tests.cpp`
- Modify: `CMakeLists.txt` (`cdtb_core`에 `module.cpp`, 테스트에 `module_tests.cpp` 추가)

**Interfaces:**
- Consumes: `cdtb::mem::Range` (Task 2)
- Produces:
  - `cdtb::mem::ModuleInfo { const std::uint8_t* base; std::size_t size; }`
  - `std::optional<ModuleInfo> cdtb::mem::find_module(const wchar_t* name)` — `name`이 `nullptr`이면 주 실행 모듈
  - `std::vector<Range> cdtb::mem::executable_ranges(ModuleInfo)`

- [ ] **Step 1: 실패하는 테스트 작성**

테스트 실행 파일 자신의 모듈을 대상으로 검증한다. 게임이 없어도 돌아간다.

`tests/module_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/module.h"

#include <cstring>

using namespace cdtb::mem;

TEST(find_module_returns_main_executable_for_null) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    CHECK(m->base != nullptr);
    CHECK(m->size > 0);
    // PE 이미지는 'MZ'로 시작한다.
    CHECK_EQ(m->base[0], std::uint8_t{'M'});
    CHECK_EQ(m->base[1], std::uint8_t{'Z'});
}

TEST(find_module_finds_a_loaded_system_dll) {
    auto m = find_module(L"kernel32.dll");
    CHECK(m.has_value());
    CHECK(m->base != nullptr);
}

TEST(find_module_returns_nullopt_for_unloaded_name) {
    auto m = find_module(L"cdtb_definitely_not_loaded_xyz.dll");
    CHECK(!m.has_value());
}

TEST(executable_ranges_are_non_empty_and_inside_module) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    auto ranges = executable_ranges(*m);
    CHECK(!ranges.empty());
    for (const auto& r : ranges) {
        CHECK(r.begin >= m->base);
        CHECK(r.size > 0);
        CHECK(r.begin + r.size <= m->base + m->size);
    }
}

// 스캐너가 실제 모듈에서 동작하는지 확인한다.
// 테스트 실행 파일 자신에 심은 고유 마커를 실행 섹션이 아닌
// 전체 이미지 범위에서 찾는다.
static volatile const std::uint8_t kMarker[16] = {
    0xC0, 0xDE, 0x70, 0x0B, 0x0C, 0xAF, 0xE1, 0x23,
    0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xED,
};

TEST(scanner_finds_marker_inside_own_module_image) {
    auto m = find_module(nullptr);
    CHECK(m.has_value());
    auto pat = parse_pattern(
        "C0 DE 70 0B 0C AF E1 23 45 67 89 AB CD EF FE ED");
    CHECK(pat.has_value());
    const std::uint8_t* hit =
        find_first(Range{m->base, m->size}, *pat);
    CHECK(hit != nullptr);
    CHECK_EQ(hit, const_cast<const std::uint8_t*>(kMarker));
}
```

- [ ] **Step 2: CMake에 파일을 등록하고 컴파일 실패를 확인**

`CMakeLists.txt`에서 `cdtb_core`와 `cdtb_tests`의 소스 목록을 수정한다.

```cmake
add_library(cdtb_core STATIC
    src/mem/scanner.cpp
    src/mem/module.cpp
)
```

```cmake
add_executable(cdtb_tests
    tests/main.cpp
    tests/smoke_tests.cpp
    tests/scanner_tests.cpp
    tests/module_tests.cpp
)
```

Run: `powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: FAIL — `mem/module.h` 없음.

- [ ] **Step 3: 헤더 작성**

`src/mem/module.h`:

```cpp
#pragma once

#include "mem/scanner.h"

namespace cdtb::mem {

struct ModuleInfo {
    const std::uint8_t* base = nullptr;
    std::size_t size = 0;
};

// name이 nullptr이면 주 실행 모듈. 로드돼 있지 않으면 nullopt.
std::optional<ModuleInfo> find_module(const wchar_t* name);

// IMAGE_SCN_MEM_EXECUTE가 설정된 섹션의 범위만 반환한다.
// 이 게임은 Denuvo가 섹션을 재배치해 실행 코드가 .text가 아니라
// .text1에 있으므로, 섹션 이름으로 판정해서는 안 된다.
std::vector<Range> executable_ranges(ModuleInfo mod);

}  // namespace cdtb::mem
```

- [ ] **Step 4: 구현 작성**

`src/mem/module.cpp`:

```cpp
#include "mem/module.h"

#include <windows.h>

namespace cdtb::mem {

std::optional<ModuleInfo> find_module(const wchar_t* name) {
    const HMODULE h = ::GetModuleHandleW(name);
    if (h == nullptr) return std::nullopt;

    const auto* base = reinterpret_cast<const std::uint8_t*>(h);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;

    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return std::nullopt;

    ModuleInfo info;
    info.base = base;
    info.size = nt->OptionalHeader.SizeOfImage;
    return info;
}

std::vector<Range> executable_ranges(ModuleInfo mod) {
    std::vector<Range> out;
    if (mod.base == nullptr) return out;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(mod.base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return out;

    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS64*>(mod.base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return out;

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    const WORD count = nt->FileHeader.NumberOfSections;

    for (WORD i = 0; i < count; ++i) {
        const auto& s = sec[i];
        if ((s.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0) continue;

        const DWORD size = s.Misc.VirtualSize != 0 ? s.Misc.VirtualSize
                                                   : s.SizeOfRawData;
        if (size == 0) continue;
        if (s.VirtualAddress + size > mod.size) continue;

        out.push_back(Range{mod.base + s.VirtualAddress, size});
    }
    return out;
}

}  // namespace cdtb::mem
```

- [ ] **Step 5: 테스트가 통과하는지 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `18 tests, 0 failures`.

만약 `scanner_finds_marker_inside_own_module_image`가 실패하면 마커가 최적화로 제거된 것이다. `kMarker`가 `volatile const`인지, 테스트 타깃에 `/Od`가 걸려 있는지 확인한다.

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(mem): 모듈 조회와 실행 섹션 열거 추가

섹션 이름이 아니라 IMAGE_SCN_MEM_EXECUTE 플래그로 스캔 범위를
판정한다. Crimson Desert는 Denuvo가 섹션을 재배치해 실행 코드가
.text(12KB)가 아니라 .text1(21MB)에 있어, 관례적인 .text 스캔은
아무것도 찾지 못한다.

테스트는 실행 파일 자신의 모듈을 대상으로 하므로 게임 없이 돈다."
```

---

### Task 4: 로그 · 설정 · 가드

**Files:**
- Create: `src/core/log.h`, `src/core/log.cpp`
- Create: `src/core/config.h`, `src/core/config.cpp`
- Create: `src/core/guard.h`, `src/core/guard.cpp`
- Create: `tests/core_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `cdtb::log::Level` (`Info` / `Warn` / `Error`)
  - `void cdtb::log::init(const std::wstring& path)`
  - `void cdtb::log::write(Level, std::string_view)`
  - `template<class... A> void cdtb::log::infof(std::format_string<A...>, A&&...)` — `warnf`, `errorf` 동일
  - `void cdtb::log::shutdown()`
  - `struct cdtb::Config { int toggle_key = VK_INSERT(0x2D); int unload_key = VK_END(0x23); bool show_diagnostics = true; }`
  - `Config cdtb::config::load(const std::wstring& path)`
  - `bool cdtb::config::save(const std::wstring& path, const Config&)`
  - `bool cdtb::guard::is_safe_to_modify()` — 0단계에서는 항상 `false`

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/core_tests.cpp`:

```cpp
#include "harness.h"
#include "core/config.h"
#include "core/guard.h"
#include "core/log.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static std::wstring temp_path(const wchar_t* leaf) {
    return (fs::temp_directory_path() / leaf).wstring();
}

TEST(log_writes_lines_to_file) {
    const std::wstring p = temp_path(L"cdtb_log_test.log");
    fs::remove(p);

    cdtb::log::init(p);
    cdtb::log::write(cdtb::log::Level::Info, "hello");
    cdtb::log::infof("value={}", 42);
    cdtb::log::shutdown();

    std::ifstream in(p);
    CHECK(in.good());
    std::string all((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
    CHECK(all.find("hello") != std::string::npos);
    CHECK(all.find("value=42") != std::string::npos);
    CHECK(all.find("INFO") != std::string::npos);
    in.close();
    fs::remove(p);
}

TEST(config_returns_defaults_when_file_missing) {
    const std::wstring p = temp_path(L"cdtb_cfg_missing.ini");
    fs::remove(p);
    const cdtb::Config c = cdtb::config::load(p);
    CHECK_EQ(c.toggle_key, 0x2D);
    CHECK_EQ(c.unload_key, 0x23);
    CHECK_EQ(c.show_diagnostics, true);
}

TEST(config_round_trips) {
    const std::wstring p = temp_path(L"cdtb_cfg_roundtrip.ini");
    fs::remove(p);

    cdtb::Config w;
    w.toggle_key = 0x77;        // F8
    w.unload_key = 0x78;        // F9
    w.show_diagnostics = false;
    CHECK(cdtb::config::save(p, w));

    const cdtb::Config r = cdtb::config::load(p);
    CHECK_EQ(r.toggle_key, 0x77);
    CHECK_EQ(r.unload_key, 0x78);
    CHECK_EQ(r.show_diagnostics, false);
    fs::remove(p);
}

TEST(config_ignores_garbage_lines) {
    const std::wstring p = temp_path(L"cdtb_cfg_garbage.ini");
    {
        std::ofstream out(p);
        out << "[CDToybox]\n";
        out << "this line has no equals sign\n";
        out << "toggle_key = 0x50\n";
        out << "unknown_key = 999\n";
        out << "; comment\n";
    }
    const cdtb::Config c = cdtb::config::load(p);
    CHECK_EQ(c.toggle_key, 0x50);
    CHECK_EQ(c.unload_key, 0x23);   // 기본값 유지
    fs::remove(p);
}

TEST(guard_refuses_modification_in_stage0) {
    CHECK_EQ(cdtb::guard::is_safe_to_modify(), false);
}
```

- [ ] **Step 2: CMake 등록 후 컴파일 실패 확인**

`cdtb_core` 소스에 `src/core/log.cpp`, `src/core/config.cpp`, `src/core/guard.cpp`를, `cdtb_tests` 소스에 `tests/core_tests.cpp`를 추가한다.

Run: `powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: FAIL — `core/log.h` 없음.

- [ ] **Step 3: 로그 구현**

`src/core/log.h`:

```cpp
#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace cdtb::log {

enum class Level { Info, Warn, Error };

void init(const std::wstring& path);
void write(Level level, std::string_view message);
void shutdown();

template <class... A>
void infof(std::format_string<A...> f, A&&... a) {
    write(Level::Info, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void warnf(std::format_string<A...> f, A&&... a) {
    write(Level::Warn, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void errorf(std::format_string<A...> f, A&&... a) {
    write(Level::Error, std::format(f, std::forward<A>(a)...));
}

}  // namespace cdtb::log
```

`src/core/log.cpp`:

```cpp
#include "core/log.h"

#include <chrono>
#include <fstream>
#include <mutex>

namespace cdtb::log {
namespace {

std::mutex g_mutex;
std::ofstream g_file;

const char* level_tag(Level l) {
    switch (l) {
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        default:           return "INFO ";
    }
}

}  // namespace

void init(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    g_file.open(path, std::ios::out | std::ios::trunc);
}

void write(Level level, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (!g_file.is_open()) return;

    const auto now = std::chrono::system_clock::now();
    g_file << std::format("[{:%H:%M:%S}] {} {}\n",
                          std::chrono::floor<std::chrono::milliseconds>(now),
                          level_tag(level), message);
    g_file.flush();   // 크래시 직전 줄을 잃지 않기 위해 매번 flush 한다.
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
}

}  // namespace cdtb::log
```

- [ ] **Step 4: 설정 구현**

`src/core/config.h`:

```cpp
#pragma once

#include <string>

namespace cdtb {

struct Config {
    int toggle_key = 0x2D;        // VK_INSERT
    int unload_key = 0x23;        // VK_END
    bool show_diagnostics = true;
};

namespace config {

// 파일이 없거나 읽을 수 없으면 기본값을 반환한다.
Config load(const std::wstring& path);
bool save(const std::wstring& path, const Config& c);

}  // namespace config
}  // namespace cdtb
```

`src/core/config.cpp`:

```cpp
#include "core/config.h"

#include <charconv>
#include <fstream>
#include <string_view>

namespace cdtb::config {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                          s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// 10진과 0x 접두 16진을 모두 받는다. 실패 시 fallback.
int to_int(std::string_view v, int fallback) {
    int base = 10;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        base = 16;
        v.remove_prefix(2);
    }
    int out = 0;
    const auto* first = v.data();
    const auto* last = v.data() + v.size();
    const auto res = std::from_chars(first, last, out, base);
    if (res.ec != std::errc{} || res.ptr != last) return fallback;
    return out;
}

}  // namespace

Config load(const std::wstring& path) {
    Config c;
    std::ifstream in(path);
    if (!in.good()) return c;

    std::string line;
    while (std::getline(in, line)) {
        std::string_view sv = trim(line);
        if (sv.empty() || sv.front() == ';' || sv.front() == '#' ||
            sv.front() == '[') {
            continue;
        }
        const std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;

        const std::string_view key = trim(sv.substr(0, eq));
        const std::string_view val = trim(sv.substr(eq + 1));
        if (val.empty()) continue;

        if (key == "toggle_key") {
            c.toggle_key = to_int(val, c.toggle_key);
        } else if (key == "unload_key") {
            c.unload_key = to_int(val, c.unload_key);
        } else if (key == "show_diagnostics") {
            c.show_diagnostics = (to_int(val, 1) != 0);
        }
    }
    return c;
}

bool save(const std::wstring& path, const Config& c) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.good()) return false;
    out << "[CDToybox]\n";
    out << "; Win32 virtual-key code\n";
    out << "toggle_key = 0x" << std::hex << c.toggle_key << "\n";
    out << "unload_key = 0x" << std::hex << c.unload_key << "\n";
    out << std::dec;
    out << "show_diagnostics = " << (c.show_diagnostics ? 1 : 0) << "\n";
    return out.good();
}

}  // namespace cdtb::config
```

- [ ] **Step 5: 가드 stub 구현**

`src/core/guard.h`:

```cpp
#pragma once

namespace cdtb::guard {

// 게임 상태를 수정해도 안전한지 판정한다.
//
// 이 게임은 PartyManager / GuildManager / LinkingCheckAsync 등
// 온라인 인프라를 갖고 있다. 실제 세션 판정은 1단계에서 구현하며,
// 그때까지는 항상 false를 반환해 쓰기 기능이 만들어지는 것을 막는다.
bool is_safe_to_modify();

}  // namespace cdtb::guard
```

`src/core/guard.cpp`:

```cpp
#include "core/guard.h"

namespace cdtb::guard {

bool is_safe_to_modify() {
    return false;   // 0단계: 쓰기 기능 없음
}

}  // namespace cdtb::guard
```

- [ ] **Step 6: 테스트가 통과하는지 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `23 tests, 0 failures`.

- [ ] **Step 7: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(core): 로그 · 설정 · 온라인 가드 추가

로그는 매 줄 flush 한다. 크래시 직전 줄이 원인 추적의 유일한
단서이기 때문이다.

가드는 항상 false를 반환하는 stub이다. 실제 세션 판정을 구현하기
전까지 게임 상태를 쓰는 기능이 만들어지지 않도록 막는 장치다."
```

---

### Task 5: MinHook 래퍼

**Files:**
- Create: `src/mem/hook.h`, `src/mem/hook.cpp`
- Create: `tests/hook_tests.cpp`
- Modify: `CMakeLists.txt` (`cdtb_core`에 `hook.cpp` 추가, `minhook` 링크)

**Interfaces:**
- Consumes: 없음
- Produces:
  - `bool cdtb::mem::hook_init()`
  - `void cdtb::mem::hook_shutdown()`
  - `bool cdtb::mem::hook_install(void* target, void* detour, void** original)`
  - `bool cdtb::mem::hook_remove(void* target)`
  - `class cdtb::mem::Hook` — 생성자 `(void* target, void* detour, void** original)`, `bool ok() const`, 소멸자에서 해제

- [ ] **Step 1: 실패하는 테스트 작성**

`tests/hook_tests.cpp`:

```cpp
#include "harness.h"
#include "mem/hook.h"

using namespace cdtb::mem;

// MinHook은 최소 5바이트의 프롤로그를 필요로 한다.
// 인라인·폴딩을 막고 함수를 충분히 크게 유지한다.
__declspec(noinline) int target_fn(int x) {
    volatile int a = x;
    a += 1;
    a *= 2;
    a -= 2;
    return a;   // == x * 2
}

using TargetFn = int (*)(int);
static TargetFn o_target = nullptr;

__declspec(noinline) int detour_fn(int x) {
    return o_target(x) + 1;
}

TEST(hook_redirects_and_restores) {
    CHECK(hook_init());

    CHECK(hook_install(reinterpret_cast<void*>(&target_fn),
                       reinterpret_cast<void*>(&detour_fn),
                       reinterpret_cast<void**>(&o_target)));
    CHECK(o_target != nullptr);
    CHECK_EQ(target_fn(5), 11);        // 5*2 + 1

    CHECK(hook_remove(reinterpret_cast<void*>(&target_fn)));
    CHECK_EQ(target_fn(5), 10);        // 원복

    hook_shutdown();
}

TEST(hook_raii_releases_on_scope_exit) {
    CHECK(hook_init());
    {
        Hook h(reinterpret_cast<void*>(&target_fn),
               reinterpret_cast<void*>(&detour_fn),
               reinterpret_cast<void**>(&o_target));
        CHECK(h.ok());
        CHECK_EQ(target_fn(5), 11);
    }
    CHECK_EQ(target_fn(5), 10);        // 소멸자가 해제했다
    hook_shutdown();
}

TEST(hook_install_rejects_null_target) {
    CHECK(hook_init());
    void* dummy = nullptr;
    CHECK(!hook_install(nullptr, reinterpret_cast<void*>(&detour_fn), &dummy));
    hook_shutdown();
}
```

- [ ] **Step 2: CMake 등록 후 컴파일 실패 확인**

`cdtb_core`에 `src/mem/hook.cpp`를 추가하고 minhook을 링크한다.

```cmake
add_library(cdtb_core STATIC
    src/mem/scanner.cpp
    src/mem/module.cpp
    src/mem/hook.cpp
    src/core/log.cpp
    src/core/config.cpp
    src/core/guard.cpp
)
target_include_directories(cdtb_core PUBLIC src)
target_link_libraries(cdtb_core PUBLIC minhook)
```

`cdtb_tests` 소스에 `tests/hook_tests.cpp`를 추가한다.

Run: `powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: FAIL — `mem/hook.h` 없음.

- [ ] **Step 3: 헤더 작성**

`src/mem/hook.h`:

```cpp
#pragma once

namespace cdtb::mem {

// 프로세스당 1회. 중복 호출은 성공으로 취급한다.
bool hook_init();
void hook_shutdown();

// target을 detour로 바꾸고 원본 트램폴린을 *original에 넣은 뒤 활성화한다.
bool hook_install(void* target, void* detour, void** original);
bool hook_remove(void* target);

// 스코프를 벗어나면 해제하는 RAII 래퍼.
class Hook {
public:
    Hook(void* target, void* detour, void** original);
    ~Hook();

    Hook(const Hook&) = delete;
    Hook& operator=(const Hook&) = delete;

    bool ok() const { return installed_; }

private:
    void* target_ = nullptr;
    bool installed_ = false;
};

}  // namespace cdtb::mem
```

- [ ] **Step 4: 구현 작성**

`src/mem/hook.cpp`:

```cpp
#include "mem/hook.h"

#include <MinHook.h>

#include "core/log.h"

namespace cdtb::mem {
namespace {
bool g_initialized = false;
}  // namespace

bool hook_init() {
    if (g_initialized) return true;
    const MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        log::errorf("MH_Initialize 실패: {}", static_cast<int>(s));
        return false;
    }
    g_initialized = true;
    return true;
}

void hook_shutdown() {
    if (!g_initialized) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_initialized = false;
}

bool hook_install(void* target, void* detour, void** original) {
    if (target == nullptr || detour == nullptr || original == nullptr) {
        return false;
    }
    if (!hook_init()) return false;

    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) {
        log::errorf("MH_CreateHook({}) 실패: {}", target,
                    static_cast<int>(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::errorf("MH_EnableHook({}) 실패: {}", target,
                    static_cast<int>(s));
        MH_RemoveHook(target);
        return false;
    }
    return true;
}

bool hook_remove(void* target) {
    if (target == nullptr || !g_initialized) return false;
    MH_DisableHook(target);
    return MH_RemoveHook(target) == MH_OK;
}

Hook::Hook(void* target, void* detour, void** original) : target_(target) {
    installed_ = hook_install(target, detour, original);
}

Hook::~Hook() {
    if (installed_) hook_remove(target_);
}

}  // namespace cdtb::mem
```

- [ ] **Step 5: 테스트가 통과하는지 확인**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
./build/cdtb_tests.exe
```

Expected: `26 tests, 0 failures`.

`target_fn(5)`이 후킹 후에도 10을 반환하면 컴파일러가 호출을 인라인한 것이다. `/Od`와 `__declspec(noinline)`이 적용됐는지 확인한다.

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(mem): MinHook RAII 래퍼 추가

설치 실패를 예외가 아니라 반환값으로 보고한다. 게임 렌더
스레드에서 예외가 전파되면 즉시 크래시이기 때문이다.

테스트는 테스트 실행 파일 안의 지역 함수를 후킹해 검증하므로
게임 없이 돈다."
```

---

### Task 6: XInput 프록시와 DLL 진입점

**Files:**
- Create: `src/proxy/xinput_proxy.h`, `src/proxy/xinput_proxy.cpp`
- Create: `src/proxy/exports.def`
- Create: `src/dllmain.cpp`
- Create: `scripts/deploy.ps1`
- Modify: `CMakeLists.txt` (`cdtoybox` DLL 타깃 신설)

**Interfaces:**
- Consumes: `cdtb::log::*` (Task 4)
- Produces:
  - `bool cdtb::proxy::load_original()` — 원본 XInput 로드
  - `void cdtb::proxy::unload_original()`
  - `std::wstring cdtb::self_directory()` — 우리 DLL이 있는 디렉터리 (뒤에 `\` 포함)
  - 산출물 `xinput1_4.dll` (export 7개)

- [ ] **Step 1: 프록시 헤더 작성**

`src/proxy/xinput_proxy.h`:

```cpp
#pragma once

namespace cdtb::proxy {

// C:\Windows\System32\XINPUT1_4.dll 을 절대경로로 로드한다.
// 상대경로를 쓰면 애플리케이션 디렉터리가 먼저 검색되어
// 자기 자신을 다시 로드하게 되므로 절대 사용하지 않는다.
bool load_original();
void unload_original();

}  // namespace cdtb::proxy
```

- [ ] **Step 2: 프록시 구현 작성**

`src/proxy/xinput_proxy.cpp`:

```cpp
#include "proxy/xinput_proxy.h"

#include <windows.h>
#include <xinput.h>

#include <string>

#include "core/log.h"

namespace {

HMODULE g_original = nullptr;

using PFN_GetState   = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using PFN_SetState   = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using PFN_GetCaps    = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using PFN_Enable     = void(WINAPI*)(BOOL);
using PFN_GetBattery = DWORD(WINAPI*)(DWORD, BYTE,
                                      XINPUT_BATTERY_INFORMATION*);
using PFN_GetKey     = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
using PFN_GetAudio   = DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);

PFN_GetState   o_GetState   = nullptr;
PFN_SetState   o_SetState   = nullptr;
PFN_GetCaps    o_GetCaps    = nullptr;
PFN_Enable     o_Enable     = nullptr;
PFN_GetBattery o_GetBattery = nullptr;
PFN_GetKey     o_GetKey     = nullptr;
PFN_GetAudio   o_GetAudio   = nullptr;

}  // namespace

namespace cdtb::proxy {

bool load_original() {
    wchar_t dir[MAX_PATH]{};
    const UINT n = ::GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    std::wstring path(dir, n);
    path += L"\\XINPUT1_4.dll";

    g_original = ::LoadLibraryW(path.c_str());
    if (g_original == nullptr) return false;

    o_GetState = reinterpret_cast<PFN_GetState>(
        ::GetProcAddress(g_original, "XInputGetState"));
    o_SetState = reinterpret_cast<PFN_SetState>(
        ::GetProcAddress(g_original, "XInputSetState"));
    o_GetCaps = reinterpret_cast<PFN_GetCaps>(
        ::GetProcAddress(g_original, "XInputGetCapabilities"));
    o_Enable = reinterpret_cast<PFN_Enable>(
        ::GetProcAddress(g_original, "XInputEnable"));
    o_GetBattery = reinterpret_cast<PFN_GetBattery>(
        ::GetProcAddress(g_original, "XInputGetBatteryInformation"));
    o_GetKey = reinterpret_cast<PFN_GetKey>(
        ::GetProcAddress(g_original, "XInputGetKeystroke"));
    o_GetAudio = reinterpret_cast<PFN_GetAudio>(
        ::GetProcAddress(g_original, "XInputGetAudioDeviceIds"));

    return o_GetState != nullptr && o_SetState != nullptr;
}

void unload_original() {
    if (g_original != nullptr) {
        ::FreeLibrary(g_original);
        g_original = nullptr;
    }
}

}  // namespace cdtb::proxy

// ---------------------------------------------------------------- exports
//
// 원본 획득에 실패했으면 ERROR_DEVICE_NOT_CONNECTED(1167)를 반환한다.
// 게임은 컨트롤러 없음으로 인식하고 정상 진행한다.

extern "C" {

DWORD WINAPI XInputGetState(DWORD i, XINPUT_STATE* s) {
    if (o_GetState == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetState(i, s);
}

DWORD WINAPI XInputSetState(DWORD i, XINPUT_VIBRATION* v) {
    if (o_SetState == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_SetState(i, v);
}

DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD f,
                                   XINPUT_CAPABILITIES* c) {
    if (o_GetCaps == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetCaps(i, f, c);
}

void WINAPI XInputEnable(BOOL enable) {
    if (o_Enable != nullptr) o_Enable(enable);
}

DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE t,
                                         XINPUT_BATTERY_INFORMATION* b) {
    if (o_GetBattery == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetBattery(i, t, b);
}

DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD r, PXINPUT_KEYSTROKE k) {
    if (o_GetKey == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetKey(i, r, k);
}

DWORD WINAPI XInputGetAudioDeviceIds(DWORD i, LPWSTR rid, UINT* rc,
                                     LPWSTR cid, UINT* cc) {
    if (o_GetAudio == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetAudio(i, rid, rc, cid, cc);
}

}  // extern "C"
```

- [ ] **Step 3: export 정의 작성**

`src/proxy/exports.def` — 서수는 시스템 `XINPUT1_4.dll`에서 실측한 값이다. `@1`(DllMain)은 우리 것이 별개이므로 export하지 않고, `@6`·`@9`는 시스템 DLL에도 없다.

```
EXPORTS
    XInputGetState              @2
    XInputSetState              @3
    XInputGetCapabilities       @4
    XInputEnable                @5
    XInputGetBatteryInformation @7
    XInputGetKeystroke          @8
    XInputGetAudioDeviceIds     @10
```

- [ ] **Step 4: DllMain 작성**

`src/dllmain.cpp` — 이 태스크에서는 로그 초기화까지만 한다. 렌더 훅은 Task 7에서 붙인다.

```cpp
#include <windows.h>

#include <string>

#include "core/config.h"
#include "core/log.h"
#include "proxy/xinput_proxy.h"

namespace {
HMODULE g_self = nullptr;
}

namespace cdtb {

// 우리 DLL이 있는 디렉터리. 뒤에 역슬래시가 붙는다.
std::wstring self_directory() {
    wchar_t buf[MAX_PATH]{};
    const DWORD n = ::GetModuleFileNameW(g_self, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L".\\";
    std::wstring p(buf, n);
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? L".\\" : p.substr(0, slash + 1);
}

}  // namespace cdtb

namespace {

DWORD WINAPI init_thread(LPVOID) {
    const std::wstring dir = cdtb::self_directory();

    cdtb::log::init(dir + L"CDToybox.log");
    cdtb::log::infof("CDToybox 0단계 시작");

    const cdtb::Config cfg = cdtb::config::load(dir + L"CDToybox.ini");
    cdtb::log::infof("설정: toggle=0x{:X} unload=0x{:X} diagnostics={}",
                     cfg.toggle_key, cfg.unload_key, cfg.show_diagnostics);

    cdtb::log::infof("초기화 완료");
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        ::DisableThreadLibraryCalls(module);

        // 실패해도 TRUE를 반환한다. 스텁이 ERROR_DEVICE_NOT_CONNECTED를
        // 돌려주므로 게임은 컨트롤러 없음으로 인식하고 진행한다.
        // FALSE를 반환하면 게임 로드 자체가 중단된다.
        cdtb::proxy::load_original();

        // 로더 락 안에서는 무거운 작업을 하지 않는다.
        const HANDLE t = ::CreateThread(nullptr, 0, init_thread, nullptr, 0,
                                        nullptr);
        if (t != nullptr) ::CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        cdtb::log::shutdown();
    }
    return TRUE;
}
```

- [ ] **Step 5: CMake에 DLL 타깃 추가**

```cmake
# -------------------------------------------------------------------- DLL
add_library(cdtoybox SHARED
    src/dllmain.cpp
    src/proxy/xinput_proxy.cpp
    src/proxy/exports.def
)
set_target_properties(cdtoybox PROPERTIES
    OUTPUT_NAME "xinput1_4"
    PREFIX ""
)
target_link_libraries(cdtoybox PRIVATE cdtb_core)
```

- [ ] **Step 6: 배포 스크립트 작성**

`scripts/deploy.ps1`:

```powershell
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Src  = Join-Path $Root "build\xinput1_4.dll"
$Dst  = "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64"

if (-not (Test-Path $Src)) { throw "빌드 산출물이 없습니다: $Src" }
Copy-Item $Src (Join-Path $Dst "xinput1_4.dll") -Force
Write-Host "배포 완료 -> $Dst\xinput1_4.dll"
```

- [ ] **Step 7: 빌드하고 export를 검증**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
```

이어서 산출물의 export 서수를 확인한다.

```powershell
$bt = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
$dumpbin = Get-ChildItem "$bt\VC\Tools\MSVC" -Recurse -Filter dumpbin.exe |
           Where-Object { $_.FullName -like "*Hostx64\x64*" } |
           Select-Object -First 1 -ExpandProperty FullName
& $dumpbin /exports "E:\CDToybox\build\xinput1_4.dll"
```

Expected: 서수 2, 3, 4, 5, 7, 8, 10에 각각 `XInputGetState`, `XInputSetState`, `XInputGetCapabilities`, `XInputEnable`, `XInputGetBatteryInformation`, `XInputGetKeystroke`, `XInputGetAudioDeviceIds`.

- [ ] **Step 8: 인게임 로드 검증**

```powershell
powershell -ExecutionPolicy Bypass -File E:\CDToybox\scripts\deploy.ps1
```

게임을 실행한 뒤 확인한다.

```bash
cat "E:/SteamLibrary/steamapps/common/Crimson Desert/bin64/CDToybox.log"
```

Expected:
- 게임이 정상적으로 실행되어 타이틀 화면까지 진입한다.
- `CDToybox.log`에 "CDToybox 0단계 시작", 설정 줄, "초기화 완료"가 있다.
- 컨트롤러가 연결돼 있다면 게임 내에서 정상 동작한다.

게임이 실행되지 않으면 즉시 `bin64\xinput1_4.dll`을 삭제해 원상 복구한 뒤 원인을 조사한다.

- [ ] **Step 9: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(proxy): xinput1_4.dll 프록시와 DLL 진입점 추가

dinput8은 게임 import 테이블에 없어 로드되지 않는다. 실측으로
확인한 후보 중 XInput이 렌더 체인과 무관해 가장 안전하다.

원본은 GetSystemDirectoryW로 얻은 절대경로로 로드한다. 상대경로를
쓰면 애플리케이션 디렉터리가 먼저 검색되어 자기 자신을 다시
로드한다.

DllMain은 로더 락 안이므로 워커 스레드로 초기화를 넘긴다."
```

---

### Task 7: D3D12 vtable 획득과 훅 설치

**Files:**
- Create: `src/render/d3d12_hook.h`, `src/render/d3d12_hook.cpp`
- Modify: `src/dllmain.cpp` (초기화에서 훅 설치 호출)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `cdtb::mem::hook_install` (Task 5), `cdtb::log::*` (Task 4)
- Produces:
  - `struct cdtb::render::VTableAddresses { void* present; void* resize_buffers; void* execute_command_lists; }`
  - `bool cdtb::render::acquire_vtable_addresses(VTableAddresses& out)`
  - `bool cdtb::render::install_hooks()`
  - `void cdtb::render::remove_hooks()`
  - `ID3D12CommandQueue* cdtb::render::captured_queue()`
  - 콜백 선언 `void cdtb::render::on_frame(IDXGISwapChain3*)` — 실제 정의는 Task 8의 `overlay.cpp`
  - 콜백 선언 `void cdtb::render::on_resize()` — 실제 정의는 Task 8

- [ ] **Step 1: 헤더 작성**

`src/render/d3d12_hook.h`:

```cpp
#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

namespace cdtb::render {

struct VTableAddresses {
    void* present = nullptr;
    void* resize_buffers = nullptr;
    void* execute_command_lists = nullptr;
};

// 더미 D3D12 객체를 만들어 vtable에서 함수 주소를 읽는다.
// 실패해도 게임에는 영향이 없다.
bool acquire_vtable_addresses(VTableAddresses& out);

bool install_hooks();
void remove_hooks();

// ExecuteCommandLists 훅이 최초로 캡처한 게임의 커맨드큐.
// 캡처 전에는 nullptr.
ID3D12CommandQueue* captured_queue();

// 아래 둘은 overlay.cpp가 정의한다.
// Present 훅이 원본을 호출하기 직전에 부른다.
void on_frame(IDXGISwapChain3* swap_chain);
// ResizeBuffers 훅이 원본을 호출하기 직전에 부른다.
void on_resize();

}  // namespace cdtb::render
```

- [ ] **Step 2: 구현 작성**

`src/render/d3d12_hook.cpp`:

```cpp
#include "render/d3d12_hook.h"

#include <windows.h>

#include "core/log.h"
#include "mem/hook.h"

namespace cdtb::render {
namespace {

// IDXGISwapChain vtable
//   IUnknown 0..2, IDXGIObject 3..6, IDXGIDeviceSubObject 7,
//   IDXGISwapChain: Present=8 ... ResizeBuffers=13
constexpr int kIdxPresent = 8;
constexpr int kIdxResizeBuffers = 13;

// ID3D12CommandQueue vtable
//   IUnknown 0..2, ID3D12Object 3..6, ID3D12DeviceChild 7,
//   ID3D12Pageable(추가 없음),
//   ID3D12CommandQueue: UpdateTileMappings=8, CopyTileMappings=9,
//                       ExecuteCommandLists=10
constexpr int kIdxExecuteCommandLists = 10;

using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT,
                                                      UINT, UINT,
                                                      DXGI_FORMAT, UINT);
using PFN_ExecuteCommandLists =
    void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT,
                             ID3D12CommandList* const*);

PFN_Present o_Present = nullptr;
PFN_ResizeBuffers o_ResizeBuffers = nullptr;
PFN_ExecuteCommandLists o_ExecuteCommandLists = nullptr;

VTableAddresses g_addrs;
ID3D12CommandQueue* g_queue = nullptr;
bool g_disabled = false;

// __try/__except는 소멸자를 가진 C++ 객체와 같은 함수에 있을 수 없다(C2712).
// 그래서 SEH 껍데기와 본문을 분리한다.
void guarded_frame(IDXGISwapChain3* sc) {
    __try {
        on_frame(sc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

void guarded_resize() {
    __try {
        on_resize();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_disabled = true;
    }
}

HRESULT STDMETHODCALLTYPE hk_Present(IDXGISwapChain3* sc, UINT interval,
                                     UINT flags) {
    if (!g_disabled && g_queue != nullptr) guarded_frame(sc);
    return o_Present(sc, interval, flags);
}

HRESULT STDMETHODCALLTYPE hk_ResizeBuffers(IDXGISwapChain3* sc, UINT count,
                                           UINT w, UINT h, DXGI_FORMAT fmt,
                                           UINT flags) {
    if (!g_disabled) guarded_resize();
    return o_ResizeBuffers(sc, count, w, h, fmt, flags);
}

void STDMETHODCALLTYPE hk_ExecuteCommandLists(ID3D12CommandQueue* q, UINT n,
                                              ID3D12CommandList* const* l) {
    if (g_queue == nullptr && q != nullptr) {
        D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_queue = q;
        }
    }
    o_ExecuteCommandLists(q, n, l);
}

}  // namespace

ID3D12CommandQueue* captured_queue() { return g_queue; }

bool acquire_vtable_addresses(VTableAddresses& out) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ::DefWindowProcW;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CDToyboxDummyWnd";
    if (::RegisterClassExW(&wc) == 0) {
        log::errorf("더미 윈도우 클래스 등록 실패: {}", ::GetLastError());
        return false;
    }

    HWND hwnd = ::CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
                                  0, 0, 1, 1, nullptr, nullptr, wc.hInstance,
                                  nullptr);
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    IDXGISwapChain* swap = nullptr;
    bool ok = false;

    do {
        if (hwnd == nullptr) break;
        if (FAILED(::D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                       IID_PPV_ARGS(&device)))) {
            log::errorf("D3D12CreateDevice 실패");
            break;
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)))) {
            break;
        }
        if (FAILED(::CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) break;

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 1;
        sd.BufferDesc.Height = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (FAILED(factory->CreateSwapChain(queue, &sd, &swap))) break;

        void** sc_vt = *reinterpret_cast<void***>(swap);
        void** q_vt = *reinterpret_cast<void***>(queue);
        out.present = sc_vt[kIdxPresent];
        out.resize_buffers = sc_vt[kIdxResizeBuffers];
        out.execute_command_lists = q_vt[kIdxExecuteCommandLists];
        ok = true;
    } while (false);

    if (swap != nullptr) swap->Release();
    if (factory != nullptr) factory->Release();
    if (queue != nullptr) queue->Release();
    if (device != nullptr) device->Release();
    if (hwnd != nullptr) ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (ok) {
        log::infof("vtable 획득: Present={} ResizeBuffers={} Exec={}",
                   out.present, out.resize_buffers,
                   out.execute_command_lists);
    }
    return ok;
}

bool install_hooks() {
    if (!acquire_vtable_addresses(g_addrs)) return false;
    if (!mem::hook_init()) return false;

    bool ok = true;
    ok &= mem::hook_install(g_addrs.present,
                            reinterpret_cast<void*>(&hk_Present),
                            reinterpret_cast<void**>(&o_Present));
    ok &= mem::hook_install(g_addrs.resize_buffers,
                            reinterpret_cast<void*>(&hk_ResizeBuffers),
                            reinterpret_cast<void**>(&o_ResizeBuffers));
    ok &= mem::hook_install(g_addrs.execute_command_lists,
                            reinterpret_cast<void*>(&hk_ExecuteCommandLists),
                            reinterpret_cast<void**>(&o_ExecuteCommandLists));

    log::infof("훅 설치 {}", ok ? "성공" : "실패");
    return ok;
}

void remove_hooks() {
    mem::hook_remove(g_addrs.execute_command_lists);
    mem::hook_remove(g_addrs.resize_buffers);
    mem::hook_remove(g_addrs.present);
    g_queue = nullptr;
    log::infof("훅 해제 완료");
}

}  // namespace cdtb::render
```

- [ ] **Step 3: 임시 `on_frame` / `on_resize` 정의 추가**

Task 8에서 `overlay.cpp`가 이 둘을 정의하지만, 이 태스크만으로 링크가 되어야 한다. `src/render/d3d12_hook.cpp` 맨 아래에 임시 정의를 넣고 Task 8 Step 3에서 제거한다.

```cpp
// --- Task 8에서 overlay.cpp로 옮긴다 -------------------------------------
namespace cdtb::render {
void on_frame(IDXGISwapChain3*) {}
void on_resize() {}
}  // namespace cdtb::render
```

- [ ] **Step 4: dllmain에서 훅 설치 호출**

`src/dllmain.cpp`의 `init_thread`에서 "초기화 완료" 로그 **앞**에 다음을 넣고, 파일 상단에 `#include "render/d3d12_hook.h"`를 추가한다.

```cpp
    if (!cdtb::render::install_hooks()) {
        cdtb::log::errorf("렌더 훅 설치 실패 - 오버레이 없이 계속한다");
    }
```

- [ ] **Step 5: CMake 갱신**

`cdtoybox` 소스에 `src/render/d3d12_hook.cpp`를 추가하고 그래픽 라이브러리를 링크한다.

```cmake
add_library(cdtoybox SHARED
    src/dllmain.cpp
    src/proxy/xinput_proxy.cpp
    src/render/d3d12_hook.cpp
    src/proxy/exports.def
)
set_target_properties(cdtoybox PROPERTIES
    OUTPUT_NAME "xinput1_4"
    PREFIX ""
)
target_link_libraries(cdtoybox PRIVATE cdtb_core d3d12 dxgi)
```

- [ ] **Step 6: 빌드하고 인게임 검증**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1
```

게임 실행 후:

```bash
cat "E:/SteamLibrary/steamapps/common/Crimson Desert/bin64/CDToybox.log"
```

Expected:
- `vtable 획득: Present=0x... ResizeBuffers=0x... Exec=0x...`
- `훅 설치 성공`
- 게임이 정상 실행되고 화면이 평소와 동일하다(아직 그리는 것이 없다).

- [ ] **Step 7: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(render): D3D12 vtable 획득과 훅 3개 설치

더미 디바이스·커맨드큐·스왑체인으로 vtable 주소를 읽는다. 이
주소는 D3D12/DXGI 런타임 소속이므로 게임 패치와 무관하게
안정적이다.

ExecuteCommandLists를 후킹하는 이유는 ImGui DX12 백엔드가 게임의
커맨드큐를 요구하는데 SwapChain만으로는 얻을 수 없기 때문이다.

훅 본문은 SEH 껍데기와 분리했다. MSVC는 소멸자를 가진 C++ 객체가
있는 함수에서 __try를 허용하지 않는다(C2712)."
```

---

### Task 8: ImGui 오버레이와 입력 토글

**Files:**
- Create: `src/render/overlay.h`, `src/render/overlay.cpp`
- Create: `src/input/wndproc.h`, `src/input/wndproc.cpp`
- Modify: `src/render/d3d12_hook.cpp` (Task 7 Step 3의 임시 정의 제거)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `cdtb::render::captured_queue()` (Task 7), `cdtb::Config` (Task 4)
- Produces:
  - `void cdtb::overlay::set_config(const Config&)`
  - `bool cdtb::overlay::is_visible()` / `void cdtb::overlay::toggle()`
  - `void cdtb::overlay::shutdown()` — ImGui·D3D12 리소스 해제, WndProc 원복
  - `void cdtb::input::install(HWND)` / `void cdtb::input::remove()`
  - `bool cdtb::input::is_installed()`

- [ ] **Step 1: 입력 모듈 작성**

`src/input/wndproc.h`:

```cpp
#pragma once

#include <windows.h>

namespace cdtb::input {

void install(HWND hwnd);
void remove();
bool is_installed();

}  // namespace cdtb::input
```

`src/input/wndproc.cpp`:

```cpp
#include "input/wndproc.h"

#include <imgui.h>

#include "core/log.h"
#include "render/overlay.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT,
                                                             WPARAM, LPARAM);

namespace cdtb::input {
namespace {

HWND g_hwnd = nullptr;
WNDPROC g_original = nullptr;

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        if (overlay::handle_hotkey(static_cast<int>(wp))) return 0;
    }

    ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);

    // 오버레이가 열려 있을 때만 입력을 게임에 넘기지 않는다.
    if (overlay::is_visible()) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool mouse_msg = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST);
        const bool key_msg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if ((mouse_msg && io.WantCaptureMouse) ||
            (key_msg && io.WantCaptureKeyboard) || msg == WM_CHAR) {
            return 0;
        }
    }
    return ::CallWindowProcW(g_original, hwnd, msg, wp, lp);
}

}  // namespace

void install(HWND hwnd) {
    if (g_original != nullptr || hwnd == nullptr) return;
    g_hwnd = hwnd;
    g_original = reinterpret_cast<WNDPROC>(::SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(proc)));
    log::infof("WndProc 서브클래싱 설치: hwnd={}", static_cast<void*>(hwnd));
}

void remove() {
    if (g_original == nullptr) return;
    ::SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(g_original));
    g_original = nullptr;
    g_hwnd = nullptr;
    log::infof("WndProc 원복");
}

bool is_installed() { return g_original != nullptr; }

}  // namespace cdtb::input
```

- [ ] **Step 2: 오버레이 작성**

`src/render/overlay.h`:

```cpp
#pragma once

#include <dxgi1_4.h>

#include "core/config.h"

namespace cdtb::overlay {

void set_config(const Config& cfg);

bool is_visible();
void toggle();

// 토글·언로드 키를 처리한다. 소비했으면 true.
bool handle_hotkey(int vk);

// ImGui와 D3D12 리소스를 해제하고 WndProc을 원복한다.
// 프록시 DLL은 정적 import되어 FreeLibrary 할 수 없으므로,
// "언로드"는 기능 비활성화를 뜻한다. 이후 재초기화가 가능해야 한다.
void shutdown();

}  // namespace cdtb::overlay
```

`src/render/overlay.cpp`:

```cpp
#include "render/overlay.h"

#include <d3d12.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>

#include <vector>

#include "core/log.h"
#include "input/wndproc.h"
#include "render/d3d12_hook.h"

// 상태와 헬퍼는 detail에 둔다. cdtb::render::on_frame 이 이 상태에
// 접근해야 하므로 익명 네임스페이스를 쓸 수 없다.
namespace cdtb::overlay::detail {

struct FrameCtx {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12Resource* back_buffer = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
};

Config g_cfg;
bool g_visible = false;
bool g_ready = false;

ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_rtv_heap = nullptr;
ID3D12DescriptorHeap* g_srv_heap = nullptr;
ID3D12GraphicsCommandList* g_cmd_list = nullptr;
std::vector<FrameCtx> g_frames;

void release_resources() {
    for (auto& f : g_frames) {
        if (f.allocator != nullptr) f.allocator->Release();
        if (f.back_buffer != nullptr) f.back_buffer->Release();
    }
    g_frames.clear();

    if (g_cmd_list != nullptr) { g_cmd_list->Release(); g_cmd_list = nullptr; }
    if (g_srv_heap != nullptr) { g_srv_heap->Release(); g_srv_heap = nullptr; }
    if (g_rtv_heap != nullptr) { g_rtv_heap->Release(); g_rtv_heap = nullptr; }
    if (g_device != nullptr) { g_device->Release(); g_device = nullptr; }
}

bool initialize(IDXGISwapChain3* sc) {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc->GetDesc(&desc))) return false;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&g_device)))) return false;

    const UINT count = desc.BufferCount;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = count;
    rtv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(g_device->CreateDescriptorHeap(&rtv_desc,
                                              IID_PPV_ARGS(&g_rtv_heap)))) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 1;
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_device->CreateDescriptorHeap(&srv_desc,
                                              IID_PPV_ARGS(&g_srv_heap)))) {
        return false;
    }

    const UINT rtv_size = g_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        g_rtv_heap->GetCPUDescriptorHandleForHeapStart();

    g_frames.resize(count);
    for (UINT i = 0; i < count; ++i) {
        if (FAILED(g_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_frames[i].allocator)))) {
            return false;
        }
        if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&g_frames[i].back_buffer)))) {
            return false;
        }
        g_device->CreateRenderTargetView(g_frames[i].back_buffer, nullptr,
                                         handle);
        g_frames[i].rtv = handle;
        handle.ptr += rtv_size;
    }

    if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           g_frames[0].allocator, nullptr,
                                           IID_PPV_ARGS(&g_cmd_list)))) {
        return false;
    }
    g_cmd_list->Close();

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(desc.OutputWindow)) return false;
    if (!ImGui_ImplDX12_Init(g_device, static_cast<int>(count),
                             DXGI_FORMAT_R8G8B8A8_UNORM, g_srv_heap,
                             g_srv_heap->GetCPUDescriptorHandleForHeapStart(),
                             g_srv_heap->GetGPUDescriptorHandleForHeapStart())) {
        return false;
    }

    input::install(desc.OutputWindow);
    log::infof("오버레이 초기화 완료: 백버퍼 {}개, hwnd={}", count,
               static_cast<void*>(desc.OutputWindow));
    return true;
}

// Task 9에서 진단 내용으로 채운다.
void draw_ui() {
    ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("CDToybox");
    ImGui::Text("0단계 골대 동작 중");
    ImGui::Separator();
    ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
    ImGui::End();
}

}  // namespace cdtb::overlay::detail

namespace cdtb::overlay {

using namespace detail;

void set_config(const Config& cfg) { g_cfg = cfg; }

bool is_visible() { return g_visible; }

void toggle() {
    g_visible = !g_visible;
    log::infof("오버레이 {}", g_visible ? "표시" : "숨김");
}

bool handle_hotkey(int vk) {
    if (vk == g_cfg.toggle_key) { toggle(); return true; }
    if (vk == g_cfg.unload_key) { shutdown(); return true; }
    return false;
}

void shutdown() {
    if (!g_ready) return;
    g_ready = false;
    g_visible = false;

    input::remove();
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    release_resources();
    log::infof("오버레이 비활성화 - Insert로 재초기화 가능");
}

}  // namespace cdtb::overlay

// ------------------------------------------------ d3d12_hook.h 의 콜백 구현
namespace cdtb::render {

void on_frame(IDXGISwapChain3* sc) {
    using namespace cdtb::overlay::detail;

    if (!g_ready) {
        if (!initialize(sc)) {
            log::errorf("오버레이 초기화 실패 - 리소스를 정리하고 중단한다");
            release_resources();
            return;
        }
        g_ready = true;
    }
    if (!g_visible) return;

    ID3D12CommandQueue* queue = captured_queue();
    if (queue == nullptr) return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_ui();
    ImGui::Render();

    const UINT idx = sc->GetCurrentBackBufferIndex();
    if (idx >= g_frames.size()) return;
    FrameCtx& f = g_frames[idx];

    f.allocator->Reset();
    g_cmd_list->Reset(f.allocator, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = f.back_buffer;
    barrier.Transition.Subresource =
        D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_cmd_list->ResourceBarrier(1, &barrier);

    g_cmd_list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    g_cmd_list->SetDescriptorHeaps(1, &g_srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmd_list);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_cmd_list->ResourceBarrier(1, &barrier);
    g_cmd_list->Close();

    ID3D12CommandList* lists[] = {g_cmd_list};
    queue->ExecuteCommandLists(1, lists);
}

void on_resize() {
    using namespace cdtb::overlay::detail;
    if (!g_ready) return;

    // 백버퍼 참조를 놓지 않으면 ResizeBuffers가 실패한다.
    ImGui_ImplDX12_InvalidateDeviceObjects();
    for (auto& f : g_frames) {
        if (f.back_buffer != nullptr) {
            f.back_buffer->Release();
            f.back_buffer = nullptr;
        }
    }
    g_ready = false;   // 다음 Present에서 재초기화된다
    log::infof("ResizeBuffers 감지 - 다음 프레임에 재초기화한다");
}

}  // namespace cdtb::render
```

- [ ] **Step 3: Task 7의 임시 정의 제거**

`src/render/d3d12_hook.cpp` 맨 아래의 임시 `on_frame` / `on_resize` 정의 블록을 삭제한다. 남겨두면 중복 정의로 링크 오류가 난다.

- [ ] **Step 4: dllmain에서 설정 전달**

`src/dllmain.cpp`의 `init_thread`에서 설정을 읽은 직후, `install_hooks()` **앞**에 다음을 넣고 상단에 `#include "render/overlay.h"`를 추가한다.

```cpp
    cdtb::overlay::set_config(cfg);
```

- [ ] **Step 5: CMake 갱신**

```cmake
add_library(cdtoybox SHARED
    src/dllmain.cpp
    src/proxy/xinput_proxy.cpp
    src/render/d3d12_hook.cpp
    src/render/overlay.cpp
    src/input/wndproc.cpp
    src/proxy/exports.def
)
set_target_properties(cdtoybox PROPERTIES
    OUTPUT_NAME "xinput1_4"
    PREFIX ""
)
target_link_libraries(cdtoybox PRIVATE cdtb_core imgui d3d12 dxgi)
```

- [ ] **Step 6: 빌드하고 인게임 검증**

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1
```

게임 실행 후:

Expected:
- 게임 화면에서 `Insert`를 누르면 "CDToybox" 창이 뜨고 FPS가 갱신된다.
- 다시 `Insert`를 누르면 사라진다.
- 창이 떠 있는 동안 마우스가 창을 조작하고 게임 카메라를 돌리지 않는다.
- 로그에 "오버레이 초기화 완료: 백버퍼 N개" 가 있다.

- [ ] **Step 7: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(render): ImGui 오버레이와 입력 토글 추가

Present 훅에서 게임의 커맨드큐로 ImGui를 그린다. ResizeBuffers
훅은 백버퍼 참조를 놓고 재초기화를 예약한다. 참조를 쥔 채로는
ResizeBuffers가 실패한다.

오버레이가 열린 동안에만 입력을 소비한다. 항상 소비하면 게임
조작이 막힌다."
```

---

### Task 9: 스캐너 진단 UI와 0단계 마감

**Files:**
- Modify: `src/render/overlay.cpp` (`draw_ui` 를 진단 내용으로 채움)
- Create: `src/render/diagnostics.h`, `src/render/diagnostics.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `cdtb::mem::find_module` / `executable_ranges` / `parse_pattern` / `find_all` (Task 2·3)
- Produces:
  - `struct cdtb::render::Diagnostics { ... }` (아래 헤더 참조)
  - `const Diagnostics& cdtb::render::diagnostics()` — 최초 호출 시 1회 계산 후 캐시

- [ ] **Step 1: 진단 헤더 작성**

`src/render/diagnostics.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cdtb::render {

struct ExecRange {
    std::uintptr_t begin = 0;
    std::size_t size = 0;
};

struct Diagnostics {
    // 게임 모듈
    std::uintptr_t game_base = 0;
    std::size_t game_size = 0;
    std::vector<ExecRange> game_exec;

    // 자기 모듈 마커 탐색 (스캐너 정확성)
    bool self_marker_found = false;
    std::uintptr_t self_marker_expected = 0;
    std::uintptr_t self_marker_found_at = 0;

    // 게임 모듈 프롤로그 탐색 (규모·속도)
    std::size_t prologue_hits = 0;
    double prologue_ms = 0.0;

    std::string error;
};

// 최초 호출 시 1회 계산하고 이후 캐시를 반환한다.
const Diagnostics& diagnostics();

}  // namespace cdtb::render
```

- [ ] **Step 2: 진단 구현 작성**

`src/render/diagnostics.cpp`:

```cpp
#include "render/diagnostics.h"

#include <chrono>

#include "core/log.h"
#include "mem/module.h"
#include "mem/scanner.h"

namespace cdtb::render {
namespace {

// 스캐너 정확성 검증용 고유 마커. 우연히 일치할 확률이 없도록
// 16바이트로 두고, 최적화로 제거되지 않도록 volatile로 둔다.
volatile const std::uint8_t kMarker[16] = {
    0xC0, 0xDE, 0x70, 0x0B, 0x0C, 0xAF, 0xE1, 0x23,
    0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0xFE, 0xED,
};

constexpr const char* kMarkerPattern =
    "C0 DE 70 0B 0C AF E1 23 45 67 89 AB CD EF FE ED";

// x64 MSVC의 흔한 함수 프롤로그.
constexpr const char* kProloguePattern =
    "48 89 5C 24 08 57 48 83 EC 20";

Diagnostics compute() {
    using namespace cdtb::mem;
    Diagnostics d;

    // --- 자기 모듈에서 마커 찾기: 스캐너가 정확한가
    d.self_marker_expected =
        reinterpret_cast<std::uintptr_t>(const_cast<const std::uint8_t*>(kMarker));

    const auto self = find_module(L"xinput1_4.dll");
    const auto marker_pat = parse_pattern(kMarkerPattern);
    if (!self.has_value() || !marker_pat.has_value()) {
        d.error = "자기 모듈 또는 마커 패턴을 준비하지 못했다";
        return d;
    }
    if (const std::uint8_t* hit =
            find_first(Range{self->base, self->size}, *marker_pat)) {
        d.self_marker_found = true;
        d.self_marker_found_at = reinterpret_cast<std::uintptr_t>(hit);
    }

    // --- 게임 모듈 실행 섹션에서 프롤로그 세기: 규모에서도 도는가
    const auto game = find_module(nullptr);
    const auto prologue_pat = parse_pattern(kProloguePattern);
    if (!game.has_value() || !prologue_pat.has_value()) {
        d.error = "게임 모듈 또는 프롤로그 패턴을 준비하지 못했다";
        return d;
    }
    d.game_base = reinterpret_cast<std::uintptr_t>(game->base);
    d.game_size = game->size;

    const auto ranges = executable_ranges(*game);
    for (const auto& r : ranges) {
        d.game_exec.push_back(
            ExecRange{reinterpret_cast<std::uintptr_t>(r.begin), r.size});
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (const auto& r : ranges) {
        d.prologue_hits += find_all(r, *prologue_pat, 100000).size();
    }
    const auto t1 = std::chrono::steady_clock::now();
    d.prologue_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    log::infof(
        "진단: 실행 섹션 {}개, 프롤로그 {}회, {:.1f}ms, 마커 {}",
        d.game_exec.size(), d.prologue_hits, d.prologue_ms,
        d.self_marker_found ? "발견" : "미발견");
    return d;
}

}  // namespace

const Diagnostics& diagnostics() {
    static const Diagnostics d = compute();
    return d;
}

}  // namespace cdtb::render
```

- [ ] **Step 3: 진단 UI 작성**

`src/render/overlay.cpp`의 `draw_ui`를 다음으로 교체하고, 파일 상단에 `#include "render/diagnostics.h"`와 `#include "core/guard.h"`를 추가한다.

```cpp
void draw_ui() {
    ImGui::SetNextWindowSize(ImVec2(640, 420), ImGuiCond_FirstUseEver);
    ImGui::Begin("CDToybox — 0단계");

    ImGui::Text("Crimson Desert 2.00.01 / %.1f FPS",
                ImGui::GetIO().Framerate);
    ImGui::Text("쓰기 기능: %s",
                cdtb::guard::is_safe_to_modify() ? "허용" : "차단 (0단계)");

    if (!g_cfg.show_diagnostics) {
        ImGui::End();
        return;
    }

    const auto& d = cdtb::render::diagnostics();
    ImGui::Separator();

    if (!d.error.empty()) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "오류: %s",
                           d.error.c_str());
        ImGui::End();
        return;
    }

    if (ImGui::CollapsingHeader("모듈", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("베이스   0x%llX",
                    static_cast<unsigned long long>(d.game_base));
        ImGui::Text("이미지   %.1f MB",
                    static_cast<double>(d.game_size) / (1024.0 * 1024.0));
        ImGui::Text("실행 섹션 %zu개", d.game_exec.size());
        ImGui::Indent();
        for (const auto& r : d.game_exec) {
            ImGui::Text("0x%llX  %.1f MB",
                        static_cast<unsigned long long>(r.begin),
                        static_cast<double>(r.size) / (1024.0 * 1024.0));
        }
        ImGui::Unindent();
    }

    if (ImGui::CollapsingHeader("스캐너 진단",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        const bool exact = d.self_marker_found &&
                           d.self_marker_found_at == d.self_marker_expected;
        ImGui::TextColored(exact ? ImVec4(0.4f, 1, 0.4f, 1)
                                 : ImVec4(1, 0.4f, 0.4f, 1),
                           "자기 모듈 마커: %s", exact ? "일치" : "불일치");
        ImGui::Text("  기대 0x%llX / 발견 0x%llX",
                    static_cast<unsigned long long>(d.self_marker_expected),
                    static_cast<unsigned long long>(d.self_marker_found_at));
        ImGui::Text("게임 프롤로그: %zu회 / %.1f ms", d.prologue_hits,
                    d.prologue_ms);
    }

    ImGui::Separator();
    ImGui::Text("Insert 토글 · End 비활성화");
    ImGui::End();
}
```

- [ ] **Step 4: CMake 갱신 후 빌드**

`cdtoybox` 소스에 `src/render/diagnostics.cpp`를 추가한다.

```bash
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1
powershell -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1
```

- [ ] **Step 5: 0단계 완료 기준 전체 검증**

게임을 실행하고 스펙 6.3의 7개 항목을 순서대로 확인한다.

| # | 확인 | 기대 |
|---|---|---|
| 1 | 빌드 | 성공, `build/xinput1_4.dll` 생성 |
| 2 | `./build/cdtb_tests.exe` | `26 tests, 0 failures` |
| 3 | 게임 실행 | 타이틀 진입, 컨트롤러 정상 |
| 4 | `Insert` | 오버레이 열림/닫힘 |
| 5 | 오버레이 내용 | 모듈 베이스, 실행 섹션 목록, 마커 "일치", 프롤로그 히트 수와 ms |
| 6 | `End` 후 `Insert` | 비활성화되고 재초기화되며 크래시 없음 |
| 7 | `bin64\CDToybox.log` | 시작·설정·vtable·훅·오버레이·진단 전 단계 기록 |

**실행 섹션 목록에 약 21MB짜리 항목(`.text1`)이 나와야 한다.** 12KB짜리 하나만 보이면 `executable_ranges`가 잘못된 것이다.

- [ ] **Step 6: 커밋**

```bash
cd /e/CDToybox
git add -A
git commit -m "feat(render): 스캐너 진단 UI 추가하고 0단계 마감

게임 자료구조를 모르는 상태에서 스캐너를 검증하기 위해 두 가지를
표시한다. 자기 모듈에 심은 고유 마커로 정확성을, 게임 실행
섹션의 함수 프롤로그 탐색으로 21MB 규모에서의 실용 속도를
증명한다."
```

- [ ] **Step 7: develop으로 병합**

```bash
cd /e/CDToybox
git checkout develop
git merge --no-ff feat/stage0-scaffold -m "Merge feat/stage0-scaffold: 0단계 골대 완성"
git log --oneline --graph -12
```

---

## Self-Review

**스펙 커버리지 확인**

| 스펙 절 | 담당 태스크 |
|---|---|
| 3.1 XInput 프록시, export 7개, 절대경로 로드, 1167 반환 | Task 6 |
| 3.1 DllMain 최소화 + 워커 스레드 | Task 6 Step 4 |
| 3.2 vtable 3개 후킹, 더미 객체 생성 | Task 7 |
| 3.2 Present 최초 초기화 / 매 프레임 | Task 8 |
| 3.2 ResizeBuffers 재생성 | Task 8 (`on_resize`) |
| 3.3 WndProc 서브클래싱, 열린 동안만 소비, Insert | Task 8 |
| 3.4 module — `IMAGE_SCN_MEM_EXECUTE` | Task 3 |
| 3.4 scanner — parse/find_first/find_all | Task 2 |
| 3.4 hook — MinHook RAII | Task 5 |
| 4 데이터 흐름 | Task 6·7·8에 분산 |
| 5 로그 매 줄 flush | Task 4 |
| 5 SEH 껍데기 분리 (C2712) | Task 7 |
| 5 온라인 가드 stub | Task 4 |
| 5 언로드 = 기능 비활성화 | Task 8 (`shutdown`) |
| 6.1 스캐너 단위 테스트 9종 | Task 2 (13개로 확장) |
| 6.2 자기 모듈 마커 + 게임 프롤로그 진단 | Task 9 |
| 6.3 완료 기준 7항목 | Task 9 Step 5 |
| 7 디렉터리 구조 | 전 태스크 |
| 8 빌드 (절대경로, OUTPUT_NAME) | Task 1·6 |

누락 없음.

**미해결로 남긴 항목**

- 스펙 5절의 "초기화 1회 보장을 원자적 플래그로" — Task 8은 단일 `g_ready` bool을 쓴다. Present는 실질적으로 렌더 스레드 하나에서만 호출되므로 0단계에서는 충분하다. Task 8 검증에서 재초기화 경합이 관측되면 `std::atomic_flag`로 승격한다.

**타입 일관성 확인**

- `Range` / `PatternBytes` — Task 2에서 정의, Task 3·9에서 동일 이름으로 사용.
- `ModuleInfo.base`는 `const std::uint8_t*`, `diagnostics()`는 이를 `std::uintptr_t`로 변환해 저장. UI는 `unsigned long long`으로 캐스팅해 출력. 일관됨.
- `on_frame(IDXGISwapChain3*)` / `on_resize()` — Task 7 헤더 선언, Task 8 정의. 시그니처 동일.
- `overlay::shutdown()` — Task 8에서 정의, `handle_hotkey`가 호출. 이름 일치.
- `captured_queue()` — Task 7에서 정의, Task 8 `on_frame`이 호출. 이름 일치.
- `cdtb::overlay::detail` — Task 8에서 상태를 담고, Task 9의 `draw_ui`(같은 네임스페이스 안)와 `cdtb::render::on_frame`(using 선언)이 접근. 일관됨.
