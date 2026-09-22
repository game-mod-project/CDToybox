# 아이템 설명 · 효과 컬럼 구현 플랜

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 아이템 목록 · 인벤토리 창에 "설명" 열(칸 = 첫 줄, 툴팁 = 전문)을 넣고 필터 검색이 설명에도 걸리게 한다(PR1). 이어 효과 수치를 붙일 발판 — 확정 경로 걷기와 버프 데이터 실측 도구 — 를 만들고, 실측으로 해석기 설계 근거를 모은다(PR2 1단계).

**Architecture:** 설명은 아이템 표를 만들 때 이름과 같은 자리에서 `resolve()` 로 풀어 정리본을 `ItemCatalogEntry::desc` 에 담는다(표는 게시 뒤 불변). 정리는 순수 함수 `game/item_text` 가, 칸 그리기는 `render::desc_cell` 하나가 맡아 두 창이 같게 그린다. 검색은 `game::passes()` 의 새 인자 `text` 로 설명을 받는다. 효과는 확정 경로(스펙 §4.1)를 걷는 `game/item_effects` 를 가짜 메모리 시험으로 먼저 굳히고, 그것을 쓰는 `cdtb_probe effects` 로 버프 데이터 9종의 필드를 잰다. **필드 뜻을 재기 전에는 해석기 코드를 쓰지 않는다** — 해석기 · 게시 · 화면(PR2 2단계)은 태스크 9 의 실측 근거로 태스크 10 에서 이 문서에 덧붙인다(지금 쓰면 추측이다).

**Tech Stack:** C++20 / MSVC, CMake + Ninja(`scripts/build.ps1`), Dear ImGui 1.92.9b, 자체 하니스(`tests/harness.h`: `TEST` · `CHECK` · `CHECK_EQ`), 가짜 메모리(`tests/fake_memory.h`).

**Spec:** `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` (§2 화면 · §3 설명 · §4 효과 · §5 검색 · §6 오류 · §7 검증 · §8 파일)

## Global Constraints

- 저장소 `E:/CDToybox`. PR1 브랜치 `feat/item-description`(develop `b62dd57` 위, 스펙 `606e309`). PR2 브랜치 `feat/item-effects` — PR1 머지 뒤 develop 에서 새로 딴다. 모든 git · 파일 명령에 절대경로 또는 `git -C E:/CDToybox`. `cd` 뒤 상대경로 금지(호출 사이 cwd 가 리셋된다).
- 이 트리는 여러 세션이 같이 쓴다 — **커밋 · 머지 직전 `git -C E:/CDToybox branch --show-current`** 가 기대한 브랜치인지 본다. 남의 트리 · 브랜치는 건드리지 않는다.
- main/develop 직접 push 금지. PR 은 develop 으로, 기본 merge commit. 머지한 브랜치는 로컬 · 원격 모두 직접 지운다(`archive/*` 는 안 지운다).
- 빌드 `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`, 시험 `E:/CDToybox/build/cdtb_tests.exe` — 끝줄 `N tests, 0 failures`. 기준선 **759 tests**. 러너에 거르기가 없어 전부 돈다 — 새 시험 이름의 `[ FAIL ]` / `[  OK  ]` 줄을 본다. 코드 PR 은 둘 다 통과해야 머지한다(CI 가 없어 로컬 검증이 대신한다).
- 새 `.cpp` 는 `CMakeLists.txt` 목록에 직접 넣는다(GLOB 없음). `items.cpp` 를 쓰는 목록은 셋이다 — DLL `cdtoybox` · `cdtb_tests` · `cdtb_probe`.
- 게임 메모리는 **읽기만**. 게임 함수 호출 · 쓰기 없음.
- **추측 금지.** 스펙 · 이 플랜에 없는 필드 뜻 · 오프셋 · 문자열 규칙을 만들어 넣지 말 것. 모자라면 멈추고 `NEEDS_CONTEXT` 로 보고한다.
- 화면 문구는 합쇼체("-습니다"). 코드 주석 · 문서는 평서체("-다") — 둘레 파일의 말투를 따른다.
- 콘솔 창 금지: `cdtb_probe` · `py` 는 `Start-Process -WindowStyle Hidden -Wait -RedirectStandardOutput <파일>` 로 돌리고 파일을 읽는다.
- 커밋 메시지는 한국어(`feat(items): …` · `test(…)` · `docs(…)`), 끝 줄 `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`. 태스크당 한 커밋.
- Edit 도구가 훅(semgrep)에 막히면 Write 로 파일을 통째로 쓰거나 python 정확 치환(발생 1회 단언). bash 히어독으로 C++ 를 쓰지 않는다.
- 배포는 develop 정본에서만(`git -C E:/CDToybox merge-base --is-ancestor origin/develop HEAD` 가 0), `bin64/xinput1_4.dll` 을 먼저 따로 복사해 두고(`deploy.ps1` 은 백업하지 않는다), **게임이 켜져 있으면 배포하지 않는다.**

---

## 파일 구조

| 파일 | 역할 | 태스크 | 컴파일 |
|---|---|---|---|
| `src/game/localization.{h,cpp}` | `resolve()` 읽기 상한 인자, `kLocMaxText` · `kLocMaxDescText` | 1 | DLL · tests · probe |
| `src/game/item_text.{h,cpp}` (신규) | `clean_item_desc` · `desc_first_line` — 순수 함수 | 2 | DLL · tests · probe |
| `tests/item_text_tests.cpp` (신규) | 정리 시험 13개 | 2 | tests |
| `src/game/items.{h,cpp}` | `ItemEntry::desc_key`(+0xB8), `ItemCatalogEntry::desc` | 3 | DLL · tests · probe |
| `src/game/item_view.{h,cpp}` | `passes(..., text)`, `filter_items` 가 `desc` 를 넘긴다 | 4 | DLL · tests · probe |
| `src/render/item_style.{h,cpp}` | `desc_cell(desc)` — 첫 줄 + 줄바꿈 툴팁 | 5 | DLL |
| `src/render/item_panel.cpp` | 설명 열(색인 6), 검색 힌트 | 5 | DLL |
| `src/render/inventory_panel.cpp` | `Row::desc`, 설명 열(색인 8), 검색에 설명, 힌트 | 5 | DLL |
| `docs/…` | 스펙 머리말 · STATUS §1.7 · README · CLAUDE.md 숫자 | 6 | — |
| `src/game/item_effects.{h,cpp}` (신규) | 확정 경로 걷기 `walk_item_effects` | 7 | tests · probe (DLL 은 2단계) |
| `tests/item_effects_tests.cpp` (신규) | 걷기 시험 6개 | 7 | tests |
| `tools/probe/main.cpp` | `effects <키> [바이트]` · `effects all` · `effects status` · `effects buff` | 8 | probe |

시험 개수 진행: 759 → T1 761 → T2 774 → T3 778 → T4 782 → T5 782 → T7 788.

## 실행 순서와 게임 세션

1. 태스크 1~5 (게임 불필요) → 태스크 6 으로 PR1 머지. 배포는 **게임이 꺼진 뒤**.
2. 태스크 7 · 8 코드 (게임 불필요, `feat/item-effects`).
3. **게임 한 판**에 몰아서: PR1 화면 확인(태스크 6 Step 7) + `effects` 도구 확인(태스크 8 Step 4) + 버프 실측(태스크 9) + 사용자 툴팁 캡처 한 번(태스크 9 Step 6). 사용자에게 게임 왕복을 여러 번 부탁하지 않는다.
4. 태스크 9 근거를 스펙에 적고 → 태스크 10 으로 2단계 플랜을 덧붙인다.

---

## PR1 — 설명 열 + 검색

### Task 1: 현지화 읽기 상한 인자

**Files:**
- Modify: `src/game/localization.h` (상한 상수, `resolve` 선언)
- Modify: `src/game/localization.cpp` (`kMaxText` 제거, `read_pool_text` · `resolve` 에 상한)
- Test: `tests/localization_tests.cpp`

**Interfaces:**
- Produces:
  - `inline constexpr std::size_t kLocMaxText = 512;`
  - `inline constexpr std::size_t kLocMaxDescText = 2048;`
  - `bool resolve(const mem::Reader& reader, const LocSystem& sys, std::uint64_t key, std::string* text_out, int* category_out, std::size_t max_len = kLocMaxText);` — 기존 호출(인자 다섯)은 그대로 컴파일된다.

- [ ] **Step 1: 실패하는 시험을 쓴다** — `tests/localization_tests.cpp` 의 `TEST(resolve_handles_empty_category_without_reading_array) { … }` 바로 뒤에 넣는다.

```cpp
// 설명은 이름보다 길다(2949 최장 503바이트). 기본 상한 512 안에서 널을 못 찾는
// 긴 문자열은 실패다 - 잘린 문장을 내보이지 않는다. 상한을 올리면 읽는다.
TEST(resolve_default_limit_rejects_text_longer_than_512) {
    Fixture f;
    f.mem.heap.resize(0x2000, 0);
    const std::string long_text(600, 'a');
    f.mem.put_str(Fixture::kPool + 0x100, long_text.c_str());
    f.mem.put_u32(Fixture::kEntries + 0x18, 0x100u);   // 항목 A 가 긴 문자열을 가리킨다
    LocSystem s = f.system();
    s.pool_size = 0x1000;
    std::string text;
    CHECK(!cdtb::game::resolve(f.mem, s, Fixture::kKeyA, &text, nullptr));
}

TEST(resolve_reads_long_text_under_the_desc_limit) {
    Fixture f;
    f.mem.heap.resize(0x2000, 0);
    const std::string long_text(600, 'a');
    f.mem.put_str(Fixture::kPool + 0x100, long_text.c_str());
    f.mem.put_u32(Fixture::kEntries + 0x18, 0x100u);
    LocSystem s = f.system();
    s.pool_size = 0x1000;
    std::string text;
    CHECK(cdtb::game::resolve(f.mem, s, Fixture::kKeyA, &text, nullptr,
                              cdtb::game::kLocMaxDescText));
    CHECK_EQ(text.size(), static_cast<std::size_t>(600));
}
```

- [ ] **Step 2: 실패를 본다**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`
Expected: 빌드 실패 — `kLocMaxDescText` 가 없다(C2039 · C2065) 또는 `resolve` 인자 수(C2660).

- [ ] **Step 3: 구현**

`src/game/localization.h` — `#include <cstdint>` 위에 `#include <cstddef>` 를 넣는다. `inline constexpr int kLocCategoryCount = 0x36;` 바로 뒤에:

```cpp
// 문자열 하나를 읽는 상한. 이 안에서 널 종단을 못 찾으면 실패로 친다 -
// 잘린 문장을 내보이지 않는다. 이름은 기본 상한 안에 든다.
inline constexpr std::size_t kLocMaxText = 512;
// 아이템 설명용 상한. 2949 실측 최장 503바이트의 네 배다
// (specs/2026-09-22-item-description-effects-design.md §3).
inline constexpr std::size_t kLocMaxDescText = 2048;
```

파일 끝의 `resolve` 선언과 주석을 바꾼다:

```cpp
// 카테고리 0..kLocCategoryCount-1 을 훑어 키를 이분 탐색하고 문자열을
// 읽는다. category_out 은 널이어도 된다. max_len 은 읽기 상한(바이트)이다.
bool resolve(const mem::Reader& reader, const LocSystem& sys,
             std::uint64_t key, std::string* text_out, int* category_out,
             std::size_t max_len = kLocMaxText);
```

`src/game/localization.cpp` — 첫 익명 이름공간에서 다음 두 줄을 지운다:

```cpp
// 문자열 하나를 읽을 때의 상한. 이름·설명이라 이 정도면 넉넉하다.
constexpr std::size_t kMaxText = 512;
```

`read_pool_text` 의 머리와 첫 세 줄을 바꾼다(나머지 본문은 그대로):

```cpp
bool read_pool_text(const mem::Reader& reader, const LocSystem& sys,
                    std::uint32_t offset, std::size_t max_len,
                    std::string* out) {
    if (offset == kUnresolved || offset >= sys.pool_size) return false;

    std::size_t n = sys.pool_size - offset;
    if (n > max_len) n = max_len;
```

`resolve` 정의의 머리를

```cpp
bool resolve(const mem::Reader& reader, const LocSystem& sys,
             std::uint64_t key, std::string* text_out, int* category_out,
             std::size_t max_len) {
```

로, 안의 호출을 `if (!read_pool_text(reader, sys, offset, max_len, text_out)) return false;` 로 바꾼다.

- [ ] **Step 4: 통과를 본다**

Run: 빌드 → `E:/CDToybox/build/cdtb_tests.exe`
Expected: `[  OK  ] resolve_default_limit_rejects_text_longer_than_512`, `[  OK  ] resolve_reads_long_text_under_the_desc_limit`, 끝줄 `761 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add src/game/localization.h src/game/localization.cpp tests/localization_tests.cpp
git -C E:/CDToybox commit -m "feat(loc): resolve 에 읽기 상한 인자 - 설명은 2048" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

첫 줄 출력이 `feat/item-description` 이 아니면 커밋하지 말고 멈춘다.

---

### Task 2: 설명 문구 정리 (`item_text`)

**Files:**
- Create: `src/game/item_text.h`, `src/game/item_text.cpp`
- Create: `tests/item_text_tests.cpp`
- Modify: `CMakeLists.txt` (세 목록에 `item_text.cpp`, 시험 목록에 `item_text_tests.cpp`)

**Interfaces:**
- Produces:
  - `std::string clean_item_desc(std::string_view raw);`
  - `std::string_view desc_first_line(std::string_view cleaned);` — 돌려주는 view 는 `cleaned` 를 가리킨다.

규칙의 근거는 스펙 §3 "표기" 전수다: 태그는 `<br/>` 하나, 자리표시는 `{Staticinfo:<표>:<키>#<표시 문구>}`(`#` 는 늘 하나, 전부 닫힘), 옆 공백 10건. **전수에 없는 표기는 손대지 않는다.** 시험 문자열은 같은 구조를 가진 합성 문장이다.

- [ ] **Step 1: 실패하는 시험을 쓴다** — `tests/item_text_tests.cpp` 를 새로 쓴다.

```cpp
#include <string>
#include <string_view>

#include "game/item_text.h"
#include "harness.h"

namespace {
using cdtb::game::clean_item_desc;
using cdtb::game::desc_first_line;
}  // namespace

// 규칙은 2949 설명 6736개 전수에서 나왔다(스펙 §3 표기). 문장은 같은 구조의 합성이다.

TEST(clean_item_desc_turns_br_into_newline) {
    CHECK_EQ(clean_item_desc("앞 문장.<br/>뒤 문장."), std::string("앞 문장.\n뒤 문장."));
}

TEST(clean_item_desc_keeps_the_blank_line_of_a_double_br) {
    // 연속 <br/><br/> 가 196건 - 문단 사이 빈 줄이다.
    CHECK_EQ(clean_item_desc("첫 문단.<br/><br/>둘째 문단."),
             std::string("첫 문단.\n\n둘째 문단."));
}

TEST(clean_item_desc_replaces_a_knowledge_placeholder_with_its_label) {
    CHECK_EQ(clean_item_desc("{Staticinfo:Knowledge:Knowledge_Hp#생명}이 회복된다."),
             std::string("생명이 회복된다."));
}

TEST(clean_item_desc_keeps_spaces_inside_an_item_placeholder_label) {
    CHECK_EQ(clean_item_desc("재료: {Staticinfo:Item:Recipe_Item_Skill_AbyssGear_SwordAura_LV1"
                             "#기어 제작법 : 바람 가르기}를 만든다."),
             std::string("재료: 기어 제작법 : 바람 가르기를 만든다."));
}

TEST(clean_item_desc_leaves_an_unclosed_placeholder_alone) {
    // 전수에는 없다. 모르는 꼴은 손대지 않는다.
    CHECK_EQ(clean_item_desc("{Staticinfo:Item:X#라벨"), std::string("{Staticinfo:Item:X#라벨"));
}

TEST(clean_item_desc_leaves_a_placeholder_without_a_label_alone) {
    CHECK_EQ(clean_item_desc("{Staticinfo:Item:X}"), std::string("{Staticinfo:Item:X}"));
}

TEST(clean_item_desc_trims_spaces_around_line_breaks) {
    // <br/> 옆에 공백이 붙은 설명이 10건이다.
    CHECK_EQ(clean_item_desc("앞 문장. <br/> 뒤 문장."), std::string("앞 문장.\n뒤 문장."));
}

TEST(clean_item_desc_keeps_bracket_headings) {
    // [효과] 139건 · [QA] 14건은 평문이다.
    CHECK_EQ(clean_item_desc("[효과] 공격력 증가"), std::string("[효과] 공격력 증가"));
}

TEST(clean_item_desc_of_empty_is_empty) {
    CHECK(clean_item_desc("").empty());
}

TEST(desc_first_line_takes_the_first_line) {
    CHECK(desc_first_line("첫 줄\n둘째 줄") == std::string_view("첫 줄"));
}

TEST(desc_first_line_skips_leading_blank_lines) {
    CHECK(desc_first_line("\n\n둘째 줄") == std::string_view("둘째 줄"));
}

TEST(desc_first_line_of_one_line_is_the_whole) {
    CHECK(desc_first_line("한 줄") == std::string_view("한 줄"));
}

TEST(desc_first_line_of_empty_is_empty) {
    CHECK(desc_first_line("").empty());
}
```

`CMakeLists.txt` 의 `cdtb_tests` 시험 목록에서 `    tests/item_view_tests.cpp` 줄 바로 뒤에 `    tests/item_text_tests.cpp` 를 넣는다.

- [ ] **Step 2: 실패를 본다**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`
Expected: 빌드 실패 — `game/item_text.h` 를 못 연다(C1083).

- [ ] **Step 3: 구현**

`src/game/item_text.h`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace cdtb::game {

// 게임 아이템 설명을 화면용으로 정리한다(순수 함수, ImGui 를 모른다).
//
// 2949 설명 6736개 전수(specs/2026-09-22-item-description-effects-design.md §3 표기):
//   - 태그는 `<br/>` 하나다(2656회, 연속 196건). '\n' 으로 바꾼다.
//   - 자리표시 `{Staticinfo:<표>:<키>#<표시 문구>}` 56건 - `#` 뒤 표시 문구로 바꾼다.
//     예 `{Staticinfo:Knowledge:Knowledge_Hp#생명}` -> `생명`.
//   - 줄마다 앞뒤 ASCII 공백을 지운다(`<br/>` 옆 공백 10건).
// 전수에 없는 표기(안 닫힌 자리표시, `#` 없는 자리표시 등)는 손대지 않고 그대로 둔다.
std::string clean_item_desc(std::string_view raw);

// 정리된 설명의 첫 줄 - 설명 칸의 요약이다. 빈 줄은 건너뛴다. 없으면 빈 view.
// 돌려주는 view 는 `cleaned` 를 가리킨다 - 원본이 살아 있는 동안만 쓴다.
std::string_view desc_first_line(std::string_view cleaned);

}  // namespace cdtb::game
```

`src/game/item_text.cpp`:

```cpp
#include "game/item_text.h"

namespace cdtb::game {
namespace {

constexpr std::string_view kLineBreak = "<br/>";
constexpr std::string_view kPlaceholder = "{Staticinfo:";

// 줄마다 앞뒤 ASCII 공백을 지운다.
std::string trim_lines(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    std::size_t start = 0;
    for (;;) {
        std::size_t end = s.find('\n', start);
        if (end == std::string::npos) end = s.size();
        std::size_t a = start;
        std::size_t b = end;
        while (a < b && s[a] == ' ') ++a;
        while (b > a && s[b - 1] == ' ') --b;
        out.append(s, a, b - a);
        if (end == s.size()) break;
        out += '\n';
        start = end + 1;
    }
    return out;
}

}  // namespace

std::string clean_item_desc(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw.compare(i, kLineBreak.size(), kLineBreak) == 0) {
            out += '\n';
            i += kLineBreak.size();
            continue;
        }
        if (raw.compare(i, kPlaceholder.size(), kPlaceholder) == 0) {
            const std::size_t close = raw.find('}', i);
            const std::size_t hash = raw.find('#', i);
            // hash 가 npos 면 `hash < close` 가 거짓이라 그대로 둔다.
            if (close != std::string_view::npos && hash < close) {
                out.append(raw.substr(hash + 1, close - hash - 1));
                i = close + 1;
                continue;
            }
        }
        out += raw[i];
        ++i;
    }
    return trim_lines(out);
}

std::string_view desc_first_line(std::string_view cleaned) {
    std::size_t start = 0;
    while (start < cleaned.size()) {
        std::size_t end = cleaned.find('\n', start);
        if (end == std::string_view::npos) end = cleaned.size();
        if (end > start) return cleaned.substr(start, end - start);
        start = end + 1;
    }
    return {};
}

}  // namespace cdtb::game
```

`CMakeLists.txt` — `src/game/items.cpp` 줄 바로 뒤에 `    src/game/item_text.cpp` 를 **세 곳 모두** 넣는다. 세 목록의 앞뒤 줄이 달라 이렇게 가린다:
- DLL `cdtoybox`: `    src/game/freecam.cpp` / `    src/game/localization.cpp` / `    src/game/items.cpp` 다음.
- `cdtb_tests`: `    src/game/localization.cpp` / `    src/game/items.cpp` / (다음 줄 `    src/game/inventory.cpp`) 사이.
- `cdtb_probe`: `    src/game/analysis.cpp` / `    src/game/localization.cpp` / `    src/game/items.cpp` 다음.

넣은 뒤 `grep -c "src/game/item_text.cpp" E:/CDToybox/CMakeLists.txt` 가 3 인지 본다.

- [ ] **Step 4: 통과를 본다**

Run: 빌드 → `E:/CDToybox/build/cdtb_tests.exe`
Expected: 위 13개 전부 `[  OK  ]`, 끝줄 `774 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add src/game/item_text.h src/game/item_text.cpp tests/item_text_tests.cpp CMakeLists.txt
git -C E:/CDToybox commit -m "feat(items): 설명 문구 정리 - <br/> · 자리표시 · 줄 공백 (전수 규칙)" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: 아이템 표에 설명

**Files:**
- Modify: `src/game/items.h` (`ItemEntry::desc_key`, `ItemCatalogEntry::desc`)
- Modify: `src/game/items.cpp` (`kRecDescKey`, 읽기, 풀기)
- Test: `tests/items_tests.cpp` (가짜 힙에 설명 키 · 설명 문자열, 시험 4개)

**Interfaces:**
- Consumes: `resolve(..., kLocMaxDescText)` (Task 1), `clean_item_desc` (Task 2)
- Produces: `std::uint64_t ItemEntry::desc_key;` `std::string ItemCatalogEntry::desc;` — 정리본, 없으면 빈 문자열.

- [ ] **Step 1: 가짜 힙을 넓히고 실패하는 시험을 쓴다** — `tests/items_tests.cpp`

(a) 레코드 반복문에서 이름 키를 쓰는 줄

```cpp
            mem.put_u64(rec + 0x28,
                        (static_cast<std::uint64_t>(keys[i]) << 32) | 0x70ull);
```

바로 뒤에:

```cpp
            // 설명 현지화 키는 +0xB8 이다(스펙 §3). 필드 0x71.
            mem.put_u64(rec + 0xB8,
                        (static_cast<std::uint64_t>(keys[i]) << 32) | 0x71ull);
```

(b) `static constexpr std::uint32_t kLocPoolSize = 0x40;` 를 `= 0x200;` 으로, 그 위 배치 주석의 `0x0B00 항목 3개` 를 `0x0B00 항목 6개` 로.

(c) `build_localization()` 안에서 `mem.put_u32(kLocCats + kLocCategory * 16 + 8, 3);` 부터 `mem.put_str(kLocPool + 0x20, "지속 보급 화살");` 까지를 다음으로 바꾼다:

```cpp
        mem.put_u32(kLocCats + kLocCategory * 16 + 8, 6);

        // 이름(0x70)과 설명(0x71)을 아이템마다 붙여 둔다. (키 << 32) | 필드 라
        // 이 차례가 곧 오름차순이다 - 이분 탐색이 성립한다. 항목 0 은 여전히
        // 아이템 A 의 이름이다(이름 없음 시험이 그 키를 망가뜨린다).
        const std::uint32_t keys[3] = {kKeyA, kKeyB, kKeyC};
        for (int i = 0; i < 3; ++i) {
            for (int fld = 0; fld < 2; ++fld) {
                const int n = i * 2 + fld;
                const std::size_t e = kLocEntries + n * 0x20;
                mem.put_u64(kLocPtrs + n * 8, mem.heap_addr(e));
                mem.put_u64(e + 0x10, (static_cast<std::uint64_t>(keys[i]) << 32) |
                                          (0x70ull + static_cast<std::uint64_t>(fld)));
                mem.put_u32(e + 0x18, static_cast<std::uint32_t>(n * 0x40));
            }
        }
        mem.put_str(kLocPool + 0x000, "편전");
        mem.put_str(kLocPool + 0x040, "짧은 화살.<br/>활로 쏜다.");
        mem.put_str(kLocPool + 0x080, "화살");
        mem.put_str(kLocPool + 0x0C0, "{Staticinfo:Knowledge:Knowledge_Hp#생명} 회복");
        mem.put_str(kLocPool + 0x100, "지속 보급 화살");
        mem.put_str(kLocPool + 0x140, "보급 화살.");
```

(자리 확인: 항목 6개 `0xB00..0xBC0` < 풀 `0xC00`, 포인터 `0xA80..0xAB0` < `0xB00`, 풀 `0xC00..0xE00` < 레코드 `0x1000`.)

(d) `TEST(build_item_catalog_fails_when_manager_is_bad) { … }` 바로 뒤에 시험 넷:

```cpp
TEST(read_item_table_reads_the_desc_key_at_0xB8) {
    Fixture f;
    std::vector<ItemEntry> out;
    CHECK(cdtb::game::read_item_table(f.mem, f.manager(), &out, 0));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK_EQ(out[0].desc_key, 0x0000089800000071ull);   // 2200
        CHECK_EQ(out[2].desc_key,
                 (static_cast<std::uint64_t>(Fixture::kKeyC) << 32) | 0x71ull);
    }
}

TEST(build_item_catalog_fills_cleaned_descriptions) {
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK_EQ(out[0].desc, std::string("짧은 화살.\n활로 쏜다."));
        CHECK_EQ(out[1].desc, std::string("생명 회복"));
        CHECK_EQ(out[2].desc, std::string("보급 화살."));
    }
}

TEST(build_item_catalog_leaves_desc_empty_when_the_desc_key_is_zero) {
    // 설명이 없어도 아이템은 남고 이름도 그대로다(스펙 §6).
    Fixture f;
    f.mem.put_u64(Fixture::kRecords + Fixture::kRecStride + 0xB8, 0);   // 레코드 1
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), f.loc_system(),
                                         &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) {
        CHECK(out[1].desc.empty());
        CHECK_EQ(out[1].name, std::string("화살"));
    }
}

TEST(build_item_catalog_has_no_desc_without_localization) {
    Fixture f;
    std::vector<cdtb::game::ItemCatalogEntry> out;
    CHECK(cdtb::game::build_item_catalog(f.mem, f.manager(), LocSystem{}, &out));
    CHECK_EQ(out.size(), static_cast<std::size_t>(3));
    if (out.size() == 3) CHECK(out[0].desc.empty());
}
```

- [ ] **Step 2: 실패를 본다**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`
Expected: 빌드 실패 — `'desc_key': is not a member of 'cdtb::game::ItemEntry'`(C2039) 등.

- [ ] **Step 3: 구현**

`src/game/items.h` — `struct ItemEntry` 의 `std::uint64_t name_key = 0;   // 레코드가 들고 있는 현지화 키` 바로 뒤에:

```cpp
    std::uint64_t desc_key = 0;   // 레코드 +0xB8 의 설명 현지화 키((키 << 32) | 0x71)
```

`struct ItemCatalogEntry` 의 `std::string name;             // 빈 문자열이면 현지화 표에 없는 것` 바로 뒤에:

```cpp
    // 정리한 설명(clean_item_desc). 빈 문자열이면 설명 키가 없거나 안 풀린 것이다.
    std::string desc;
```

`src/game/items.cpp` — `#include "core/write_log.h"` 뒤에 `#include "game/item_text.h"` 를 넣는다. `constexpr std::size_t kRecNameKey = 0x28;   // u64 이름 현지화 키` 바로 뒤에:

```cpp
// u64 설명 현지화 키. `_itemDesc`(+0xB0) 객체의 키 칸이다 - 이름 키 +0x28 이
// `_itemName`(+0x20) 의 키 칸인 것과 같은 꼴(스펙 §3, 6816개 전수).
constexpr std::size_t kRecDescKey = 0xB8;
```

`read_item_table` 의 `if (!reader.read_value(e.record + kRecNameKey, &e.name_key)) continue;` 바로 뒤에:

```cpp
        // 설명 키. 못 읽으면 0 으로 둔다 - 설명만 빠진다.
        reader.read_value(e.record + kRecDescKey, &e.desc_key);
```

`build_item_catalog` 의 `if (has_loc) {` 블록을 다음으로 바꾼다:

```cpp
        if (has_loc) {
            // 못 풀려도 항목은 남긴다. 키는 있는 아이템이다.
            resolve(reader, sys, e.name_key, &entry.name, nullptr);
            // 설명은 이름보다 길다(최장 503바이트) - 상한을 올려 읽는다.
            std::string raw;
            if (e.desc_key != 0 &&
                resolve(reader, sys, e.desc_key, &raw, nullptr, kLocMaxDescText)) {
                entry.desc = clean_item_desc(raw);
            }
        }
```

- [ ] **Step 4: 통과를 본다**

Run: 빌드 → 시험
Expected: 새 넷 `[  OK  ]`, 기존 `build_item_catalog_*` · `read_item_table_*` 전부 `[  OK  ]`, 끝줄 `778 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add src/game/items.h src/game/items.cpp tests/items_tests.cpp
git -C E:/CDToybox commit -m "feat(items): 아이템 표에 설명 - 레코드 +0xB8 키를 풀어 정리본으로" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: 검색에 설명

**Files:**
- Modify: `src/game/item_view.h` (`ItemFilter::query` 주석, `passes` 선언, `item_sort_from_specs` 주석)
- Modify: `src/game/item_view.cpp` (`passes`, `filter_items`)
- Test: `tests/item_view_tests.cpp`

**Interfaces:**
- Consumes: `ItemCatalogEntry::desc` (Task 3)
- Produces: `bool passes(const ItemFilter& f, std::string_view name, int grade, int category, std::uint32_t key, EquipOwner owner, std::string_view text = {});` — `text` 가 비어 있지 않으면 검색어를 거기에도 건다. `hide_unnamed` 는 여전히 먼저 본다. `match_key` 와 무관하다.

- [ ] **Step 1: 실패하는 시험을 쓴다** — `tests/item_view_tests.cpp`

(a) `TEST(passes_agrees_with_filter_items)` 의 첫 줄 `const auto named = sample();` 을

```cpp
    const auto named = [] {
        auto v = sample();
        v[2].desc = "나무를 베는 데 쓴다";   // 설명으로만 걸리는 줄
        return v;
    }();
```

로, `ItemFilter fs[6];` 을 `ItemFilter fs[7];` 로 바꾸고 `fs[5].category = 56;` 뒤에 `fs[6].query = "나무를";` 을 넣는다. 안의 호출을

```cpp
                const bool p = cdtb::game::passes(f, e.name, e.grade,
                                                  e.category, e.key, e.owner,
                                                  e.desc);
```

로 바꾼다.

(b) `TEST(passes_with_key_off_drops_unnamed_even_if_key_matches) { … }` 바로 뒤에:

```cpp
TEST(passes_matches_the_description_text) {
    ItemFilter f;
    f.query = "관통력";
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared,
                             "막강한 관통력과 파괴력"));
    CHECK(!cdtb::game::passes(f, "화살", 0, 0, 50001u, EquipOwner::Shared,
                              "기본 화살"));
}

TEST(passes_matches_description_even_when_key_is_off) {
    // 인벤토리는 키로는 안 찾지만 설명으로는 찾는다(스펙 §5).
    ItemFilter f;
    f.query = "관통력";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "편전", 0, 0, 2200u, EquipOwner::Shared,
                             "막강한 관통력"));
}

TEST(passes_still_hides_unnamed_even_if_the_description_matches) {
    ItemFilter f;
    f.query = "관통력";
    f.hide_unnamed = true;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u, EquipOwner::Shared,
                              "관통력"));
}

TEST(filter_items_matches_the_catalog_description) {
    auto all = sample();
    all[2].desc = "나무를 베는 데 쓴다";
    ItemFilter f;
    f.query = "나무를";
    const auto out = cdtb::game::filter_items(all, f);
    CHECK_EQ(out.size(), static_cast<std::size_t>(1));
    if (!out.empty()) CHECK_EQ(out[0]->key, 950002u);
}
```

- [ ] **Step 2: 실패를 본다**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`
Expected: 빌드 실패 — `passes` 인자 수(C2660).

- [ ] **Step 3: 구현**

`src/game/item_view.h` — `std::string query;          // 이름 또는 키에 걸린다` 를 `std::string query;          // 이름 · 설명(text) 또는 키에 걸린다` 로. `passes` 선언과 주석을:

```cpp
// 한 항목이 필터를 통과하는가. filter_items 가 이것을 부르고, 인벤토리
// 창은 자기 행에 직접 건다 - 거르는 규칙이 두 곳에 있으면 갈라진다.
// text 는 이름 말고 검색어를 더 걸 문구다 - 아이템 설명(스펙 §5). 비면 안 본다.
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key, EquipOwner owner,
            std::string_view text = {});
```

`item_sort_from_specs` 주석의 `키, 2 등급, 3 분류, **4 전용, 5 이름**이다.` 를 `키, 2 등급, 3 분류, **4 전용, 5 이름**, 6 설명(정렬 없음)이다.` 로.

`src/game/item_view.cpp` — `passes` 정의 머리를

```cpp
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key, EquipOwner owner,
            std::string_view text) {
```

로 바꾸고, 이름 일치 블록

```cpp
    if (!name.empty() && name.find(f.query) != std::string_view::npos) {
        return true;
    }
```

바로 뒤에:

```cpp
    // 설명에도 건다(스펙 §5). 키를 안 보는 창(인벤토리)에서도 설명은 본다.
    if (!text.empty() && text.find(f.query) != std::string_view::npos) {
        return true;
    }
```

`filter_items` 의 호출을 `if (!passes(filter, e.name, e.grade, e.category, e.key, e.owner, e.desc)) {` 로.

- [ ] **Step 4: 통과를 본다**

Run: 빌드 → 시험
Expected: 새 넷과 `passes_agrees_with_filter_items` 가 `[  OK  ]`, 끝줄 `782 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add src/game/item_view.h src/game/item_view.cpp tests/item_view_tests.cpp
git -C E:/CDToybox commit -m "feat(items): 필터 검색이 설명에도 걸린다" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: 설명 칸 — 두 창

ImGui 층이라 단위 시험이 없다. 빌드가 통과하고 기존 시험이 그대로인 것을 보고, 화면은 태스크 6 Step 7 에서 본다.

**Files:**
- Modify: `src/render/item_style.h`, `src/render/item_style.cpp` (`desc_cell`)
- Modify: `src/render/item_panel.cpp` (열 7개, 설명 칸, 힌트)
- Modify: `src/render/inventory_panel.cpp` (`Row::desc`, 열 10개, 설명 칸, 검색, 힌트)

**Interfaces:**
- Consumes: `desc_first_line` (Task 2), `ItemCatalogEntry::desc` (Task 3), `passes(..., text)` (Task 4)
- Produces: `void render::desc_cell(const std::string& desc);`

- [ ] **Step 1: `desc_cell` 을 만든다**

`src/render/item_style.h` — `const char* category_name(std::uint8_t c);` 바로 뒤에:

```cpp
// 설명 칸(스펙 §2). 칸에는 첫 줄만 그리고 - 열 폭에서 잘린다 - 마우스를 올리면
// 전문을 줄바꿈해 보인다. desc 가 비면 아무것도 안 그린다(툴팁도 없다).
void desc_cell(const std::string& desc);
```

`src/render/item_style.cpp` — include 목록에 `#include <string_view>` 와 `#include "game/item_text.h"` 를 넣고, `category_name` 정의 뒤(같은 `cdtb::render` 이름공간 안)에:

```cpp
void desc_cell(const std::string& desc) {
    if (desc.empty()) return;
    const std::string_view line = game::desc_first_line(desc);
    ImGui::TextUnformatted(line.data(), line.data() + line.size());
    // BeginItemTooltip 은 잠깐 멈췄을 때만 뜬다(ImGuiHoveredFlags_ForTooltip) - 줄을 훑으며
    // 지나갈 때마다 큰 툴팁이 깜빡이지 않는다. 설명은 최장 503바이트라 줄바꿈이 필요하다.
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(desc.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
```

- [ ] **Step 2: 아이템 목록 창**

`src/render/item_panel.cpp`:

- 필터바 옵션 주석 `// 이 창의 필터바 옵션. 힌트("이름 또는 키로 검색")와 match_key 가 한 쌍이다.` 를 `힌트("이름 · 설명 · 키로 검색")` 로, `o.hint = "이름 또는 키로 검색";` 을 `o.hint = "이름 · 설명 · 키로 검색";` 으로.
- `if (ImGui::BeginTable("items", 6, kFlags)) {` 를 `7` 로.
- `ImGui::TableSetupColumn("이름", ImGuiTableColumnFlags_WidthStretch);` 바로 뒤에:

```cpp
        // 설명(스펙 §2). 정렬 색인(item_sort_from_specs)을 안 밀게 맨 끝에 두고
        // 정렬하지 않는다.
        ImGui::TableSetupColumn("설명", ImGuiTableColumnFlags_WidthStretch |
                                            ImGuiTableColumnFlags_NoSort);
```

- 줄 그리기에서 5번 칸(아이콘 + 이름)의 `if (e.name.empty()) { … } else { … }` 블록 바로 뒤, 반복문을 닫는 `}` 앞에:

```cpp
            ImGui::TableSetColumnIndex(6);
            desc_cell(e.desc);
```

- [ ] **Step 3: 인벤토리 창**

`src/render/inventory_panel.cpp`:

- `struct Row` 의 `std::string name;` 바로 뒤에:

```cpp
    std::string desc;                // 아이템표의 정리된 설명 - 설명 칸과 검색에 쓴다
```

- `refresh` 의 `if (const auto* e = game::item_by_key(r.key)) {` 블록 안, `r.name = e->name;` 바로 뒤에 `r.desc = e->desc;`.
- 필터바 옵션 주석의 `힌트("이름으로 검색")와 match_key=false 가 한 쌍이다.` 를 `힌트("이름 · 설명으로 검색")와 match_key=false 가 한 쌍이다.` 로, `o.hint = "이름으로 검색";` 을 `o.hint = "이름 · 설명으로 검색";` 으로.
- `if (ImGui::BeginTable("inv", 9,` 를 `10` 으로.
- `ImGui::TableSetupColumn("소켓", ImGuiTableColumnFlags_WidthStretch);` 바로 뒤에:

```cpp
        // 설명(스펙 §2). apply_sort 의 색인 0~7 을 안 밀게 소켓 뒤에 두고 정렬하지
        // 않는다. 버튼 열은 9 로 밀리지만 NoSort 라 정렬과 무관하다.
        ImGui::TableSetupColumn("설명", ImGuiTableColumnFlags_WidthStretch |
                                            ImGuiTableColumnFlags_NoSort);
```

- 거르기 호출을

```cpp
            if (!game::passes(filter, r.name, r.grade, r.category, r.key,
                              r.owner, r.desc)) {
```

로.
- 소켓 칸

```cpp
            ImGui::TableNextColumn();
            if (!r.text.sockets.empty()) {
                ImGui::TextUnformatted(r.text.sockets.c_str());
            }
```

바로 뒤(버튼 열의 `ImGui::TableNextColumn();` 앞)에:

```cpp
            ImGui::TableNextColumn();
            desc_cell(r.desc);
```

- [ ] **Step 4: 빌드와 시험**

Run: 빌드 → 시험
Expected: 빌드 성공, 끝줄 `782 tests, 0 failures`(바뀐 시험 없음).

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add src/render/item_style.h src/render/item_style.cpp src/render/item_panel.cpp src/render/inventory_panel.cpp
git -C E:/CDToybox commit -m "feat(ui): 아이템 목록 · 인벤토리에 설명 열 - 칸은 첫 줄, 툴팁은 전문" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: PR1 마무리 — 문서 · PR · 배포 · 화면 확인

**Files:**
- Modify: `docs/superpowers/specs/2026-09-22-item-description-effects-design.md` (머리말 상태)
- Modify: `docs/STATUS.md` (§1.7 끝에 한 문단)
- Modify: `docs/README.md` (편수 · §11 표 두 줄)
- Modify: `CLAUDE.md` (시험 개수 · 스펙 편수)

- [ ] **Step 1: 문서를 고친다**

스펙 첫 줄 `> **상태 — 📐 설계 (2026-09-22 사용자 승인, 구현 전).** 화면 · 데이터 · 검증 · 진행 네 부분을` 을

```
> **상태 — 🔨 PR1(설명 열 · 검색) 구현 · PR2(효과) 실측 전 (2026-09-22).** 화면 · 데이터 · 검증 · 진행 네 부분을
```

로 바꾸고, 머리말 셋째 줄 뒤에 `> 구현 계획: \`plans/2026-09-22-item-description-effects.md\`.` 를 한 줄 더한다.

`docs/STATUS.md` §1.7 의 마지막 문단(`**argv 는 시스템 ANSI 코드페이지로 온다**` 로 시작해 `변환이 필요 없다.` 로 끝나는 것) 뒤에 빈 줄과:

```
**설명(2026-09-22).** 레코드 `+0xB8` 이 설명 현지화 키(`(키 << 32) | 0x71`)를 들고 있다 —
6816개 전부, 그중 6736개가 풀린다. 표를 만들 때 이름과 함께 풀어 정리본(`<br/>` → 줄바꿈,
`{Staticinfo:…#표시}` → 표시 문구)을 `ItemCatalogEntry::desc` 에 담고, 아이템 목록 · 인벤토리
창의 "설명" 열(칸 = 첫 줄, 툴팁 = 전문)과 필터 검색에 쓴다. 근거는
`specs/2026-09-22-item-description-effects-design.md` §3.
```

`docs/README.md`:
- 5행 `조사 기록 60편 · 계획 6편이` → `조사 기록 61편 · 계획 7편이`.
- 8행 `나머지 11편 — 계획 6편 ·` → `나머지 13편 — 계획 7편 ·`, 9행 `` `boss-room-action-limit` · `game-update-2949` — 은 그 배너가 없고,`` → `` `boss-room-action-limit` · `game-update-2949` · `item-description-effects-design` — 은 그 배너가 없고,``.
- §11 표의 마지막 줄(`| [panel-shared-widgets-review](…) | ✅ 완료 | Critical/Important 없음 |`) 뒤에:

```
| [item-description-effects-design](superpowers/specs/2026-09-22-item-description-effects-design.md) | 🔨 PR1 구현 · PR2 실측 전 | 설명 열 · 검색(PR1). 설명 표기 전수 §3, 효과 경로 · 전수 §4.1 |
| [plans/item-description-effects](superpowers/plans/2026-09-22-item-description-effects.md) | 🔨 진행 중 | PR1 태스크 1~6, PR2 1단계 7~10(2단계는 실측 뒤 덧붙인다) |
```

`CLAUDE.md`:
- 96행 `(759개, 2026-09-22)` → Step 2 에서 본 실제 개수(예상 `782개`).
- 156행 `설계 (60편)` → `설계 (61편)`.

- [ ] **Step 2: 전체 빌드 · 시험**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1` → `E:/CDToybox/build/cdtb_tests.exe`
Expected: 빌드 성공, `782 tests, 0 failures`. 개수가 다르면 CLAUDE.md 에 실제 값을 쓴다.

- [ ] **Step 3: 커밋 · 푸시**

```bash
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox add docs/superpowers/specs/2026-09-22-item-description-effects-design.md docs/STATUS.md docs/README.md CLAUDE.md
git -C E:/CDToybox commit -m "docs(items): 설명 열 구현 기록 - STATUS · 문서 지도 · 숫자" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git -C E:/CDToybox push origin feat/item-description
```

- [ ] **Step 4: PR 을 연다**

```bash
gh pr create --repo game-mod-project/CDToybox --base develop --head feat/item-description --title "feat(items): 아이템 설명 열 + 검색 (PR1)" --body-file <본문 파일>
```

본문(파일로 써서 넘긴다): 무엇(두 창 설명 열 · 툴팁 · 검색), 근거(스펙 §3 전수 — 설명 키 +0xB8 6816개, 표기 `<br/>` · 자리표시), 검증(`scripts\build.ps1` 성공 · `cdtb_tests` 782/0), 화면 확인은 배포 뒤(태스크 6 Step 7), 끝 줄 `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.

- [ ] **Step 5: 머지 · 브랜치 정리**

빌드 · 시험이 녹색인 것을 다시 확인하고(Step 2), `git -C E:/CDToybox branch --show-current` 뒤:

```bash
gh pr merge <번호> --repo game-mod-project/CDToybox --merge
git -C E:/CDToybox checkout develop
git -C E:/CDToybox pull --ff-only origin develop
git -C E:/CDToybox branch -d feat/item-description
git -C E:/CDToybox push origin --delete feat/item-description
```

`branch -d` 가 거부하면 memory `branch-cleanup-procedure` 절차(SHA 백업 → `merge-base --is-ancestor` 판정)를 따른다.

- [ ] **Step 6: 배포 (게임이 꺼져 있을 때만)**

```powershell
Get-Process -Name CrimsonDesert -ErrorAction SilentlyContinue   # 아무것도 안 나와야 한다
git -C E:/CDToybox merge-base --is-ancestor origin/develop HEAD; $LASTEXITCODE   # 0
Copy-Item "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\xinput1_4.dll" "E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\xinput1_4.dll.bak-item-desc"
pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1
pwsh -NoProfile -File E:/CDToybox/scripts/deploy.ps1
```

게임이 켜져 있으면 배포하지 않고 다음 단계로 넘긴다 — 다음 게임 세션 전에 배포한다.

- [ ] **Step 7: 화면 확인 (다음 게임 세션, 태스크 8 · 9 와 같은 판)**

확인할 것 넷:
1. 아이템 목록 창 맨 끝에 "설명" 열이 있고 칸에 첫 줄이 보인다.
2. 편전(2200) 줄의 설명 칸에 마우스를 멈추면 전문이 줄바꿈되어 두 줄로 뜬다.
3. 아이템 목록 검색에 `관통력` 을 넣으면 편전이 남는다.
4. 인벤토리 창 소켓 열 뒤에 "설명" 열이 있고, 검색이 설명에도 걸린다.

방법은 `scripts/overlay-check.ps1`(`Toggle` · `Cap` · `ClickAt`, PrintWindow 캡처 · PostMessage 입력, 포그라운드를 뺏지 않는다)과 memory `overlay-screen-check-postmessage` 를 따른다. 1 · 3 · 4 번은 캡처와 클릭 · 입력으로 본다. 2 번(툴팁)은 그 방법으로 안 될 수 있다 — 스크립트 머리말대로 실제 커서가 창 밖이면 `WM_MOUSELEAVE` 가 마우스 위치를 지우는데, `BeginItemTooltip` 은 몇 프레임 멈춰 있어야 뜬다. 안 되면 사용자에게 **한 번만** 두 창의 스크린샷(설명 툴팁 하나 포함)을 부탁한다. 결과를 STATUS §1.7 설명 문단 끝에 `화면 확인 (날짜).` 로 적는다(PR2 문서 커밋에 같이 싣는다).

---

## PR2 1단계 — 효과 경로 걷기와 실측

PR1 이 develop 에 머지된 뒤 시작한다.

```bash
git -C E:/CDToybox checkout develop
git -C E:/CDToybox pull --ff-only origin develop
git -C E:/CDToybox checkout -b feat/item-effects
```

### Task 7: 효과 경로 걷기 (`item_effects`)

경로와 포인터 모양은 스펙 §4.1 에 2026-09-22 재실측으로 확정돼 있다(헤딘 · 프레야 · 떡고기구이 · 편전 홉별 원값, 6816개 전수). 개수 상한은 전수 최댓값의 네 배 이상을 2의 거듭제곱으로 올린 것이다(설명 상한 2048 과 같은 규칙).

**Files:**
- Create: `src/game/item_effects.h`, `src/game/item_effects.cpp`
- Create: `tests/item_effects_tests.cpp`
- Modify: `CMakeLists.txt` (`cdtb_tests` 소스 목록에 `item_effects.cpp`, 시험 목록에 `item_effects_tests.cpp`)

**Interfaces:**
- Produces:
  - `struct EffectTables { std::uintptr_t item_use = 0; std::uintptr_t skill = 0; };`
  - `using ClassOf = std::function<std::string(std::uintptr_t)>;`
  - `struct EffectBuff { std::uint32_t use_row, skill_row, value2, level; std::uintptr_t object; std::string cls; };`
  - `struct EffectWalk { std::vector<EffectBuff> buffs; int skill_uses = 0; int other_uses = 0; bool rejected = false; };`
  - `bool walk_item_effects(const mem::Reader&, const EffectTables&, std::uintptr_t item_record, const ClassOf&, EffectWalk* out);`
  - 상수 `kItemUseSkillClass`, `kEffectMaxUses = 128`, `kEffectMaxSkills = 4`, `kEffectMaxLevels = 64`, `kEffectMaxBuffs = 16`.

- [ ] **Step 1: 실패하는 시험을 쓴다** — `tests/item_effects_tests.cpp` 를 새로 쓴다.

```cpp
#include <cstdint>
#include <map>
#include <string>

#include "fake_memory.h"
#include "game/item_effects.h"
#include "harness.h"

namespace {

using cdtb::game::EffectTables;
using cdtb::game::EffectWalk;
using cdtb::tests::FakeMemory;

// 확정 경로(스펙 §4.1)를 흉내낸 가짜 힙.
//
//   0x0000  ItemUseInfoManager  +0x30 개수 3, +0x58 -> 0x0100
//   0x0040  SkillInfoManager    +0x30 개수 2, +0x58 -> 0x0140
//   0x0100  사용 레코드 포인터 3개 -> 0x0400 · 0x0440 · 0x0480
//   0x0140  스킬 레코드 포인터 2개 -> 0x0700 · 0x0740
//   0x0200  ItemInfo 레코드     +0x80 -> 0x0300, +0x88 = 2
//   0x0300  사용 행 [1, 2]
//   0x0440  사용 행 1  +0x18 -> 0x0500 (ItemUseData_Skill)
//   0x0480  사용 행 2  +0x18 -> 0x0540 (ItemUseData_DestroyOnly)
//   0x0500  ItemUseData_Skill  +0x30 -> 0x0600, +0x38 = 1
//   0x0600  {스킬 행 1, 값2 2}
//   0x0740  스킬 행 1  +0x18 -> 0x0800, +0x20 = 2
//   0x0800  단계 0 {0x0900, 2} · 단계 1 {0x0910, 1}   (16바이트 칸)
//   0x0900  버프 포인터 [0x0A00, 0x0A40] · 0x0910 [0x0A80]
struct Fixture {
    FakeMemory mem;
    std::map<std::uintptr_t, std::string> classes;

    Fixture() {
        mem.heap.assign(0x1000, 0);
        mem.put_u32(0x0000 + 0x30, 3);
        mem.put_u64(0x0000 + 0x58, mem.heap_addr(0x0100));
        mem.put_u32(0x0040 + 0x30, 2);
        mem.put_u64(0x0040 + 0x58, mem.heap_addr(0x0140));
        mem.put_u64(0x0100, mem.heap_addr(0x0400));
        mem.put_u64(0x0108, mem.heap_addr(0x0440));
        mem.put_u64(0x0110, mem.heap_addr(0x0480));
        mem.put_u64(0x0140, mem.heap_addr(0x0700));
        mem.put_u64(0x0148, mem.heap_addr(0x0740));

        mem.put_u64(0x0200 + 0x80, mem.heap_addr(0x0300));
        mem.put_u32(0x0200 + 0x88, 2);
        mem.put_u32(0x0300, 1);
        mem.put_u32(0x0304, 2);

        mem.put_u64(0x0440 + 0x18, mem.heap_addr(0x0500));
        mem.put_u64(0x0480 + 0x18, mem.heap_addr(0x0540));
        mem.put_u64(0x0500 + 0x30, mem.heap_addr(0x0600));
        mem.put_u32(0x0500 + 0x38, 1);
        mem.put_u32(0x0600, 1);   // 스킬 행
        mem.put_u32(0x0604, 2);   // 값2

        mem.put_u64(0x0740 + 0x18, mem.heap_addr(0x0800));
        mem.put_u32(0x0740 + 0x20, 2);
        mem.put_u64(0x0800, mem.heap_addr(0x0900));
        mem.put_u32(0x0808, 2);
        mem.put_u64(0x0810, mem.heap_addr(0x0910));
        mem.put_u32(0x0818, 1);
        mem.put_u64(0x0900, mem.heap_addr(0x0A00));
        mem.put_u64(0x0908, mem.heap_addr(0x0A40));
        mem.put_u64(0x0910, mem.heap_addr(0x0A80));

        classes[mem.heap_addr(0x0500)] = ".?AVItemUseData_Skill@pa@@";
        classes[mem.heap_addr(0x0540)] = ".?AVItemUseData_DestroyOnly@pa@@";
        classes[mem.heap_addr(0x0A00)] = ".?AVVaryStatBuffData@pa@@";
        classes[mem.heap_addr(0x0A40)] = ".?AVVoidActiveBuffData@pa@@";
        classes[mem.heap_addr(0x0A80)] = ".?AVChangeBuffLevelBuffData@pa@@";
    }

    EffectTables tables() const {
        return EffectTables{mem.heap_addr(0x0000), mem.heap_addr(0x0040)};
    }
    std::uintptr_t item() const { return mem.heap_addr(0x0200); }

    bool walk(EffectWalk* out) const {
        const cdtb::game::ClassOf class_of = [this](std::uintptr_t a) {
            const auto it = classes.find(a);
            return it != classes.end() ? it->second : std::string();
        };
        return cdtb::game::walk_item_effects(mem, tables(), item(), class_of, out);
    }
};

}  // namespace

TEST(walk_item_effects_follows_the_confirmed_path) {
    Fixture f;
    EffectWalk w;
    CHECK(f.walk(&w));
    CHECK(!w.rejected);
    CHECK_EQ(w.skill_uses, 1);
    CHECK_EQ(w.other_uses, 1);
    CHECK_EQ(w.buffs.size(), static_cast<std::size_t>(3));
    if (w.buffs.size() == 3) {
        CHECK_EQ(w.buffs[0].use_row, 1u);
        CHECK_EQ(w.buffs[0].skill_row, 1u);
        CHECK_EQ(w.buffs[0].value2, 2u);
        CHECK_EQ(w.buffs[0].level, 0u);
        CHECK_EQ(w.buffs[0].object, f.mem.heap_addr(0x0A00));
        CHECK_EQ(w.buffs[0].cls, std::string(".?AVVaryStatBuffData@pa@@"));
        CHECK_EQ(w.buffs[1].cls, std::string(".?AVVoidActiveBuffData@pa@@"));
        CHECK_EQ(w.buffs[2].level, 1u);
        CHECK_EQ(w.buffs[2].cls, std::string(".?AVChangeBuffLevelBuffData@pa@@"));
    }
}

TEST(walk_item_effects_is_empty_for_an_item_without_uses) {
    // 바람 가르기처럼 사용이 없는 아이템은 효과가 없을 뿐 실패가 아니다.
    Fixture f;
    f.mem.put_u32(0x0200 + 0x88, 0);
    EffectWalk w;
    CHECK(f.walk(&w));
    CHECK(!w.rejected);
    CHECK(w.buffs.empty());
}

TEST(walk_item_effects_counts_non_skill_uses_without_buffs) {
    // 슬롯 등록 · 먹이 주기 같은 사용은 효과로 치지 않는다(스펙 §4.4).
    Fixture f;
    f.mem.put_u32(0x0200 + 0x88, 1);
    f.mem.put_u32(0x0300, 2);   // DestroyOnly 만
    EffectWalk w;
    CHECK(f.walk(&w));
    CHECK_EQ(w.other_uses, 1);
    CHECK_EQ(w.skill_uses, 0);
    CHECK(w.buffs.empty());
}

TEST(walk_item_effects_rejects_a_row_past_the_table) {
    // StaticInfo 는 행 첨자다 - 행이 표 개수 이상이면 구조가 밀린 것이다(스펙 §6).
    Fixture f;
    f.mem.put_u32(0x0300, 3);   // 사용 표는 3행(0..2)
    EffectWalk w;
    CHECK(!f.walk(&w));
    CHECK(w.rejected);
    CHECK(w.buffs.empty());
}

TEST(walk_item_effects_rejects_an_absurd_level_count) {
    Fixture f;
    f.mem.put_u32(0x0740 + 0x20, cdtb::game::kEffectMaxLevels + 1);
    EffectWalk w;
    CHECK(!f.walk(&w));
    CHECK(w.rejected);
}

TEST(walk_item_effects_rejects_when_a_table_is_missing) {
    Fixture f;
    EffectTables t = f.tables();
    t.skill = 0;
    EffectWalk w;
    const cdtb::game::ClassOf none = [](std::uintptr_t) { return std::string(); };
    CHECK(!cdtb::game::walk_item_effects(f.mem, t, f.item(), none, &w));
    CHECK(w.rejected);
}
```

`CMakeLists.txt` 의 `cdtb_tests` 시험 목록에서 `    tests/item_text_tests.cpp` 뒤에 `    tests/item_effects_tests.cpp` 를 넣는다. 소스 쪽은 **`cdtb_tests` 목록에만** 넣는다 — `inventory.cpp` / `item_view.cpp` / `icon_map.cpp` 세 줄 순서는 DLL · probe 목록에도 똑같이 있어 그것으로는 못 가린다. 태스크 2 뒤 `cdtb_tests` 목록에만 있는 네 줄 `    src/game/items.cpp` / `    src/game/item_text.cpp` / `    src/game/inventory.cpp` / `    src/game/item_view.cpp` 를 찾아 그 `item_view.cpp` 뒤에 `    src/game/item_effects.cpp` 를 넣는다(DLL · probe 는 `item_text.cpp` 다음이 `roster.cpp` 다). 넣은 뒤 `grep -c "src/game/item_effects.cpp" E:/CDToybox/CMakeLists.txt` 가 1 이다.

- [ ] **Step 2: 실패를 본다**

Run: `pwsh -NoProfile -File E:/CDToybox/scripts/build.ps1`
Expected: 빌드 실패 — `game/item_effects.h` 를 못 연다(C1083).

- [ ] **Step 3: 구현**

`src/game/item_effects.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mem/reader.h"

namespace cdtb::game {

// 아이템 효과 경로를 걷는다. 읽기만 한다 - 게임 함수를 부르지 않는다.
//
// 확정 경로(2949 재실측 - specs/2026-09-22-item-description-effects-design.md §4.1):
//
//   ItemInfo 레코드 +0x80  u32* 사용 행 배열,  +0x88 u32 개수
//     -> ItemUseInfoManager 레코드[행] +0x18  ItemUseData_* 객체 포인터(다형)
//     -> ItemUseData_Skill +0x30  {u32 스킬 행, u32 값2}* (8바이트 칸),  +0x38 u32 개수
//     -> SkillInfoManager 레코드[스킬 행] +0x18  단계 칸 배열(16바이트 {ptr, u32 개수}),
//                                         +0x20  u32 단계 수
//     -> 단계 칸 ptr = BuffData 객체 포인터 배열
//
// StaticInfo 표는 행 첨자다(memory staticinfo-index-not-key) - 행은 표 개수 미만이어야 한다.
// 버프 데이터의 필드 뜻은 아직 모른다(§4.2) - 여기서는 객체와 클래스 이름까지만 낸다.

inline constexpr const char* kItemUseSkillClass = ".?AVItemUseData_Skill@pa@@";

// 구조가 밀렸을 때 엉뚱한 개수를 끝까지 따라가지 않게 하는 상한. 6816개 전수 최댓값
// (사용 17 · 스킬 칸 1 · 단계 15 · 단계당 버프 4, 스펙 §4.1)의 네 배 이상을 2의
// 거듭제곱으로 올렸다.
inline constexpr std::uint32_t kEffectMaxUses = 128;
inline constexpr std::uint32_t kEffectMaxSkills = 4;
inline constexpr std::uint32_t kEffectMaxLevels = 64;
inline constexpr std::uint32_t kEffectMaxBuffs = 16;

struct EffectTables {
    std::uintptr_t item_use = 0;   // ItemUseInfoManager 인스턴스(find_static_manager)
    std::uintptr_t skill = 0;      // SkillInfoManager 인스턴스
};

// 객체 주소 -> RTTI 클래스 이름. 실제로는 mem::Rtti::class_of_object 를 넘긴다.
using ClassOf = std::function<std::string(std::uintptr_t)>;

// 단계 하나 안의 버프 데이터 하나.
struct EffectBuff {
    std::uint32_t use_row = 0;     // ItemUseInfo 행
    std::uint32_t skill_row = 0;   // SkillInfo 행
    // 스킬 칸의 둘째 u32. 1-기준 단계 번호다(전수 근거 스펙 §4.1). 걷기는 거르지 않고
    // 모든 단계를 낸다 - 고르는 것은 해석하는 쪽이다.
    std::uint32_t value2 = 0;
    std::uint32_t level = 0;       // 단계 칸 첨자(0부터)
    std::uintptr_t object = 0;     // BuffData 객체
    std::string cls;               // 그 RTTI 클래스 이름
};

struct EffectWalk {
    std::vector<EffectBuff> buffs;
    int skill_uses = 0;      // ItemUseData_Skill 사용 수
    int other_uses = 0;      // 스킬이 아닌 사용 수 - 효과로 치지 않는다(§4.4)
    bool rejected = false;   // 개수 · 행이 말이 안 돼 이 아이템을 버렸다(§6)
};

// item_record 는 ItemInfo 레코드 주소(ItemEntry::record). 사용이 없는 아이템은 true
// 에 빈 결과다. 읽기 실패 · 말 안 되는 개수나 행이면 out->rejected 를 세우고 buffs 를
// 비운 채 false.
bool walk_item_effects(const mem::Reader& reader, const EffectTables& tables,
                       std::uintptr_t item_record, const ClassOf& class_of,
                       EffectWalk* out);

}  // namespace cdtb::game
```

`src/game/item_effects.cpp`:

```cpp
#include "game/item_effects.h"

namespace cdtb::game {
namespace {

// --- ItemInfo 레코드 ---
constexpr std::size_t kItemUseRows = 0x80;       // u32* 사용 행 배열
constexpr std::size_t kItemUseCount = 0x88;      // u32 개수
// --- StaticInfo 매니저(아이템 표와 같은 꼴) ---
constexpr std::size_t kMgrCount = 0x30;          // u32 개수
constexpr std::size_t kMgrRecords = 0x58;        // 레코드 포인터 배열
// --- ItemUseInfo 레코드 ---
constexpr std::size_t kUseData = 0x18;           // ItemUseData_* 객체 포인터
// --- ItemUseData_Skill ---
constexpr std::size_t kSkillEntries = 0x30;      // {u32 스킬 행, u32 값2}*
constexpr std::size_t kSkillEntryCount = 0x38;   // u32
constexpr std::size_t kSkillEntrySize = 8;
// --- SkillInfo 레코드 ---
constexpr std::size_t kSkillLevels = 0x18;       // 16바이트 단계 칸 배열
constexpr std::size_t kSkillLevelCount = 0x20;   // u32
constexpr std::size_t kLevelSize = 16;           // {ptr +0, u32 개수 +8}

bool read_table(const mem::Reader& r, std::uintptr_t mgr, std::uint32_t* count,
                std::uintptr_t* records) {
    std::uint32_t n = 0;
    std::uint64_t recs = 0;
    if (mgr == 0 || !r.read_value(mgr + kMgrCount, &n) ||
        !r.read_value(mgr + kMgrRecords, &recs) || n == 0 || recs == 0) {
        return false;
    }
    *count = n;
    *records = static_cast<std::uintptr_t>(recs);
    return true;
}

// 행 첨자로 레코드 주소를 얻는다. 행이 표 개수 이상이거나 널이면 0.
std::uintptr_t record_at(const mem::Reader& r, std::uintptr_t records,
                         std::uint32_t count, std::uint32_t row) {
    if (row >= count) return 0;
    std::uint64_t rec = 0;
    if (!r.read_value(records + static_cast<std::uintptr_t>(row) * 8, &rec)) {
        return 0;
    }
    return static_cast<std::uintptr_t>(rec);
}

}  // namespace

bool walk_item_effects(const mem::Reader& reader, const EffectTables& tables,
                       std::uintptr_t item_record, const ClassOf& class_of,
                       EffectWalk* out) {
    if (out == nullptr) return false;
    *out = EffectWalk{};
    const auto reject = [out]() {
        out->buffs.clear();
        out->rejected = true;
        return false;
    };

    std::uint32_t use_n = 0;
    std::uint32_t skill_n = 0;
    std::uintptr_t use_recs = 0;
    std::uintptr_t skill_recs = 0;
    if (item_record == 0 ||
        !read_table(reader, tables.item_use, &use_n, &use_recs) ||
        !read_table(reader, tables.skill, &skill_n, &skill_recs)) {
        return reject();
    }

    std::uint64_t rows = 0;
    std::uint32_t nrows = 0;
    if (!reader.read_value(item_record + kItemUseRows, &rows) ||
        !reader.read_value(item_record + kItemUseCount, &nrows)) {
        return reject();
    }
    if (nrows == 0) return true;   // 사용이 없는 아이템
    if (rows == 0 || nrows > kEffectMaxUses) return reject();

    for (std::uint32_t u = 0; u < nrows; ++u) {
        std::uint32_t row = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(rows) + u * 4, &row)) {
            return reject();
        }
        const std::uintptr_t use = record_at(reader, use_recs, use_n, row);
        if (use == 0) return reject();
        std::uint64_t data = 0;
        if (!reader.read_value(use + kUseData, &data) || data == 0) return reject();
        if (class_of(static_cast<std::uintptr_t>(data)) != kItemUseSkillClass) {
            ++out->other_uses;
            continue;
        }
        ++out->skill_uses;

        std::uint64_t ents = 0;
        std::uint32_t nents = 0;
        if (!reader.read_value(static_cast<std::uintptr_t>(data) + kSkillEntries, &ents) ||
            !reader.read_value(static_cast<std::uintptr_t>(data) + kSkillEntryCount, &nents) ||
            nents > kEffectMaxSkills || (nents != 0 && ents == 0)) {
            return reject();
        }
        for (std::uint32_t s = 0; s < nents; ++s) {
            const std::uintptr_t ent =
                static_cast<std::uintptr_t>(ents) + s * kSkillEntrySize;
            std::uint32_t srow = 0;
            std::uint32_t value2 = 0;
            if (!reader.read_value(ent, &srow) || !reader.read_value(ent + 4, &value2)) {
                return reject();
            }
            const std::uintptr_t skill = record_at(reader, skill_recs, skill_n, srow);
            if (skill == 0) return reject();
            std::uint64_t levels = 0;
            std::uint32_t nlv = 0;
            if (!reader.read_value(skill + kSkillLevels, &levels) ||
                !reader.read_value(skill + kSkillLevelCount, &nlv) ||
                nlv > kEffectMaxLevels || (nlv != 0 && levels == 0)) {
                return reject();
            }
            for (std::uint32_t L = 0; L < nlv; ++L) {
                const std::uintptr_t lv =
                    static_cast<std::uintptr_t>(levels) + L * kLevelSize;
                std::uint64_t arr = 0;
                std::uint32_t nb = 0;
                if (!reader.read_value(lv, &arr) || !reader.read_value(lv + 8, &nb) ||
                    nb > kEffectMaxBuffs || (nb != 0 && arr == 0)) {
                    return reject();
                }
                for (std::uint32_t b = 0; b < nb; ++b) {
                    std::uint64_t obj = 0;
                    if (!reader.read_value(static_cast<std::uintptr_t>(arr) + b * 8, &obj) ||
                        obj == 0) {
                        return reject();
                    }
                    EffectBuff eb;
                    eb.use_row = row;
                    eb.skill_row = srow;
                    eb.value2 = value2;
                    eb.level = L;
                    eb.object = static_cast<std::uintptr_t>(obj);
                    eb.cls = class_of(eb.object);
                    out->buffs.push_back(std::move(eb));
                }
            }
        }
    }
    return true;
}

}  // namespace cdtb::game
```

- [ ] **Step 4: 통과를 본다**

Run: 빌드 → 시험
Expected: 새 여섯 `[  OK  ]`, 끝줄 `788 tests, 0 failures`.

- [ ] **Step 5: 커밋**

```bash
git -C E:/CDToybox branch --show-current   # feat/item-effects
git -C E:/CDToybox add src/game/item_effects.h src/game/item_effects.cpp tests/item_effects_tests.cpp CMakeLists.txt
git -C E:/CDToybox commit -m "feat(effects): 확정 경로 걷기 - 아이템 -> 사용 -> 스킬 -> 단계 -> 버프 데이터" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: 실측 도구 `cdtb_probe effects`

모드가 볼 것과 같은 것을 보려고 `walk_item_effects` 를 그대로 쓴다. 표는 넷 다 `find_static_manager` 를 통과하는 것을 실측했다(스펙 §4.1 — ItemUse · Skill, 그리고 Buff 1000018 · Status 1000000). 세 표의 레코드 `+0x08` 은 내부 이름 객체다(`Item_Active_Potion_DDD` · `Hp` · `BuffLevel_MinHP` 로 실측).

**Files:**
- Modify: `tools/probe/main.cpp` (include, 사용법, `cmd_effects`, 분기)
- Modify: `CMakeLists.txt` (`cdtb_probe` 소스 목록에 `item_effects.cpp`)

**Interfaces:**
- Consumes: `walk_item_effects` · `EffectTables` · `ClassOf` (Task 7), `find_item_manager` · `read_item_table` · `ItemEntry` (items.h), `find_static_manager` · `roster_header` · `read_engine_string` (roster.h)

- [ ] **Step 1: 명령을 넣는다**

`tools/probe/main.cpp`:
- include 목록의 `#include "game/items.h"` 뒤에 `#include "game/item_effects.h"`.
- `usage()` 문자열에서 `"  items find <문자열>         이름에 그 문자열이 든 것만\n"` 줄 뒤에:

```cpp
        "  effects <아이템키> [바이트]  효과 경로 + 버프 데이터 덤프(읽기)\n"
        "  effects all                 버프 클래스별 개수와 표본 3개\n"
        "  effects status | buff       능력치 · 버프 표의 행 · 키 · 내부 이름\n"
```

- `void cmd_clan(` 정의 바로 앞에:

```cpp
// StaticInfo 레코드 +0x08 의 내부 이름(_stringKey). 못 읽으면 빈 문자열.
std::string static_row_name(const mem::Reader& reader, std::uintptr_t mgr,
                            std::uint32_t row) {
    std::uint32_t n = 0;
    std::uintptr_t recs = 0;
    if (!game::roster_header(reader, mgr, &n, &recs) || row >= n) return {};
    std::uint64_t rec = 0;
    if (!reader.read_value(recs + static_cast<std::uintptr_t>(row) * 8, &rec) || rec == 0) {
        return {};
    }
    std::uint64_t so = 0;
    if (!reader.read_value(static_cast<std::uintptr_t>(rec) + 0x08, &so) || so == 0) return {};
    return game::read_engine_string(reader, static_cast<std::uintptr_t>(so));
}

// 효과 경로를 걷고 버프 데이터를 덤프한다. 읽기만 한다.
//
//   effects <아이템키> [바이트=0x80]  한 아이템의 사용 · 스킬 · 단계 · 버프 + 객체 바이트
//   effects all                       버프 클래스별 개수와 표본 3개
//   effects status | effects buff     StatusInfo / BuffInfo 의 행 · 키 · 내부 이름
//
// 버프 데이터 9종의 필드 뜻을 재는 도구다(specs/2026-09-22-item-description-effects-design.md
// §4.2). 경로는 game::walk_item_effects 를 그대로 쓴다.
void cmd_effects(const mem::Rtti& rt, const mem::Reader& reader, int argc,
                 char** argv) {
    if (argc < 3) {
        std::printf("사용법: effects <아이템키> [바이트] | effects all | effects status | effects buff\n");
        return;
    }
    const std::string sub = argv[2];

    if (sub == "status" || sub == "buff") {
        const char* cls = (sub == "status") ? ".?AVStatusInfoManager@pa@@"
                                            : ".?AVBuffInfoManager@pa@@";
        std::uintptr_t mgr = 0;
        std::uint32_t n = 0;
        std::uintptr_t recs = 0;
        if (!game::find_static_manager(reader, rt, cls, &mgr) ||
            !game::roster_header(reader, mgr, &n, &recs)) {
            std::printf("%s 를 못 찾았습니다\n", cls);
            return;
        }
        std::printf("%s 0x%llX  %u행\n", cls, static_cast<unsigned long long>(mgr), n);
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint64_t rec = 0;
            if (!reader.read_value(recs + static_cast<std::uintptr_t>(i) * 8, &rec) ||
                rec == 0) {
                continue;
            }
            std::uint32_t key = 0;
            reader.read_value(static_cast<std::uintptr_t>(rec), &key);
            std::printf("  %4u  %8u  %s\n", i, key,
                        static_row_name(reader, mgr, i).c_str());
        }
        return;
    }

    std::uintptr_t item_mgr = 0;
    game::EffectTables t;
    if (!game::find_item_manager(rt, reader, &item_mgr) ||
        !game::find_static_manager(reader, rt, ".?AVItemUseInfoManager@pa@@", &t.item_use) ||
        !game::find_static_manager(reader, rt, ".?AVSkillInfoManager@pa@@", &t.skill)) {
        std::printf("표를 못 찾았습니다 (아이템 0x%llX · 사용 0x%llX · 스킬 0x%llX)\n",
                    static_cast<unsigned long long>(item_mgr),
                    static_cast<unsigned long long>(t.item_use),
                    static_cast<unsigned long long>(t.skill));
        return;
    }
    std::vector<game::ItemEntry> items;
    if (!game::read_item_table(reader, item_mgr, &items, 0)) {
        std::printf("아이템 표를 못 읽었습니다\n");
        return;
    }
    const game::ClassOf class_of = [&rt](std::uintptr_t a) {
        return rt.class_of_object(a);
    };

    if (sub == "all") {
        struct Seen {
            int count = 0;
            std::vector<std::string> samples;
        };
        std::map<std::string, Seen> by_class;
        int walked = 0;
        int rejected = 0;
        for (const auto& e : items) {
            game::EffectWalk w;
            if (!game::walk_item_effects(reader, t, e.record, class_of, &w)) {
                ++rejected;
                continue;
            }
            if (w.skill_uses == 0) continue;
            ++walked;
            for (const auto& b : w.buffs) {
                auto& s = by_class[b.cls];
                ++s.count;
                if (s.samples.size() < 3) {
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "아이템 %u  스킬행 %u  값2 %u  단계 %u",
                                  e.key, b.skill_row, b.value2, b.level);
                    s.samples.push_back(buf);
                }
            }
        }
        std::printf("스킬 사용 아이템 %d개, 버린 것 %d개\n", walked, rejected);
        for (const auto& [cls, s] : by_class) {
            std::printf("%6d  %s\n", s.count, cls.c_str());
            for (const auto& x : s.samples) std::printf("          %s\n", x.c_str());
        }
        return;
    }

    const auto want = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
    std::size_t nbytes = (argc > 3) ? std::strtoull(argv[3], nullptr, 0) : 0x80;
    if (nbytes == 0 || nbytes > 0x400) nbytes = 0x80;
    const game::ItemEntry* item = nullptr;
    for (const auto& e : items) {
        if (e.key == want) {
            item = &e;
            break;
        }
    }
    if (item == nullptr) {
        std::printf("아이템 %u 이 표에 없습니다\n", want);
        return;
    }

    game::EffectWalk w;
    const bool ok = game::walk_item_effects(reader, t, item->record, class_of, &w);
    std::printf("아이템 %u  레코드 0x%llX  스킬 사용 %d · 그 밖 %d · 버프 %zu%s\n", want,
                static_cast<unsigned long long>(item->record), w.skill_uses, w.other_uses,
                w.buffs.size(), ok ? "" : "  (버림: 개수 · 행이 말이 안 된다)");
    std::vector<std::uint8_t> buf(nbytes);
    for (const auto& b : w.buffs) {
        std::printf("\n[사용행 %u] 스킬행 %u (%s) 값2 %u 단계 %u  %s  0x%llX\n", b.use_row,
                    b.skill_row, static_row_name(reader, t.skill, b.skill_row).c_str(),
                    b.value2, b.level, b.cls.c_str(),
                    static_cast<unsigned long long>(b.object));
        if (!reader.read(b.object, buf.data(), buf.size())) {
            std::printf("  (읽기 실패)\n");
            continue;
        }
        for (std::size_t o = 0; o < nbytes; o += 16) {
            std::printf("  +%03zX ", o);
            for (std::size_t k = 0; k < 16 && o + k < nbytes; ++k) {
                std::printf(" %02X", buf[o + k]);
            }
            std::printf("   u32:");
            for (std::size_t k = 0; k + 4 <= 16 && o + k + 4 <= nbytes; k += 4) {
                std::uint32_t v = 0;
                std::memcpy(&v, buf.data() + o + k, 4);
                std::printf(" %u", v);
            }
            std::printf("   f32:");
            for (std::size_t k = 0; k + 4 <= 16 && o + k + 4 <= nbytes; k += 4) {
                float v = 0.0f;
                std::memcpy(&v, buf.data() + o + k, 4);
                std::printf(" %g", v);
            }
            std::printf("\n");
        }
    }
}
```

- 명령 분기의 `if (cmd == "clan") { cmd_clan(rt, reader, argc, argv); return 0; }` 바로 앞에:

```cpp
    if (cmd == "effects") { cmd_effects(rt, reader, argc, argv); return 0; }
```

`CMakeLists.txt` 의 `cdtb_probe` 소스 목록에 `    src/game/item_effects.cpp` 를 넣는다. `inventory.cpp` / `item_view.cpp` / `icon_map.cpp` 순서는 DLL 목록에도 똑같이 있어 못 가린다 — probe 목록에만 있는 네 줄 `    src/game/analysis.cpp` / `    src/game/localization.cpp` / `    src/game/items.cpp` / `    src/game/item_text.cpp` 를 찾아(DLL 은 `analysis` 와 `localization` 사이에 `freecam` 이 있다) 그 `item_text.cpp` 뒤에 넣는다. 넣은 뒤 `grep -c "src/game/item_effects.cpp" E:/CDToybox/CMakeLists.txt` 가 2 다(tests · probe).

- [ ] **Step 2: 빌드와 시험**

Run: 빌드 → 시험
Expected: 빌드 성공(probe 포함), `788 tests, 0 failures`.

- [ ] **Step 3: 커밋 · 푸시**

```bash
git -C E:/CDToybox branch --show-current   # feat/item-effects
git -C E:/CDToybox add tools/probe/main.cpp CMakeLists.txt
git -C E:/CDToybox commit -m "feat(probe): effects - 효과 경로와 버프 데이터 덤프, 클래스 전수, 능력치 · 버프 표 이름" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git -C E:/CDToybox push -u origin feat/item-effects
```

- [ ] **Step 4: 게임에서 도구를 확인한다 (게임 세션, 읽기만)**

```powershell
$probe = "E:\CDToybox\build\cdtb_probe.exe"; $o = "<스크래치>\effects-1000085.txt"
Start-Process -FilePath $probe -ArgumentList "effects 1000085" -WindowStyle Hidden -Wait -RedirectStandardOutput $o
```

Expected (2026-09-22 실측과 같아야 한다):
- `effects 1000085` → `스킬 사용 1 · 그 밖 0 · 버프 60`, 스킬행 1494 `(Item_Active_MP_Ignore_Use)` 값2 2, 단계마다 `IgnoreUseResourceStat` · `BlockRegenerateStat` · `VoidActive` · `VaryDataDefinedStat` 네 개.
- `effects 2200` → `스킬 사용 0 · 그 밖 3 · 버프 0`.
- `effects all` → `스킬 사용 아이템 606개, 버린 것 0개`.

다르면 멈추고 차이를 보고한다(`NEEDS_CONTEXT`) — 걷기나 판정이 틀린 것이다.

---

### Task 9: 버프 데이터 9종 실측 → 스펙 §4.2 근거

코드를 바꾸지 않는다. 산출물은 **스펙 §4.2 의 클래스별 필드 표와 근거**, §4.3 능력치 이름 표의 갱신이다. 모든 명령은 읽기 전용이고 숨긴 창으로 돌린다.

**채택 규칙** — 필드의 뜻은 다음 중 하나로만 정한다. 둘 다 없으면 그 필드는 "모름", 뜻을 아는 필드로 값을 못 만드는 클래스는 "해석 못 한 효과"(개수만)로 남긴다.
- (가) 사용자 툴팁 캡처의 수치와 그 아이템 · 단계의 필드 값이 같다(단위 환산이 있으면 그 환산이 캡처 둘 이상에서 같다).
- (나) 행 번호로 보이는 필드가 그 클래스의 모든 표본에서 표 개수 미만이고, 가리키는 행의 내부 이름(`effects status` / `effects buff`)이 스킬의 내부 이름과 모든 표본에서 맞는다(예: 스킬 `Item_Active_Potion_DDD` ↔ 능력치 `DDD`).
- 개발용 한글 이름표는 근거로 쓰지 않는다(스펙 §4.1).

- [ ] **Step 1: 클래스 전수** — `effects all` 을 파일로 받는다. 9종 + 나머지의 개수와 표본을 본다.

- [ ] **Step 2: 표본 덤프** — 9종마다 서로 다른 스킬 둘 이상을 골라(표본 줄의 아이템 키) `effects <키> 0x80` 을 받는다. 헤딘(1000085) · 프레야(1000028) · 떡고기구이(1003488)는 반드시 포함한다.

- [ ] **Step 3: 표 이름** — `effects status` · `effects buff` 를 받는다(84행 · 292행).

- [ ] **Step 4: 후보를 적는다** — 클래스마다 `+0x08..+0x78` 을 단계 0..n 에 걸쳐 나란히 놓고 표시한다: 단계에 따라 단조로 변하는 값, 단계와 무관하게 같은 작은 정수(표 행 후보), 힙 포인터. 규칙 (나)로 확정되는 행 필드는 여기서 확정한다.

- [ ] **Step 5: 툴팁 캡처 목록을 만든다** — 헤딘 · 프레야 · 떡고기구이 · 바람 가르기(설명만)에, 이 넷이 못 덮는 클래스(예상: `VaryDataDefinedStatRate` · `VaryStatRate` · `VaryStatOverMaxValue` — Step 1 결과로 확정)마다 표본 아이템 하나를 더한다. 아이템 이름은 `cdtb_probe items find` 로 확인한다.

- [ ] **Step 6: 사용자에게 한 번만 부탁한다** — 목록(이름 · 키)과 함께: "게임 인벤토리에서 이 아이템들에 마우스를 올려 툴팁을 캡처해 주십시오. 없는 아이템은 오버레이 지급 창으로 받을 수 있습니다." 한 판에 끝나게 한 번에 전부 부탁한다.

- [ ] **Step 7: 스펙에 근거를 적는다** — §4.2 에 클래스별 표(오프셋 · 형 · 뜻 · 근거: 아이템/단계/캡처 값 또는 행 이름 일치)를 넣고, 각 클래스를 "해석" / "개수만" 으로 판정한다. §4.3 표에 캡처로 확인한 한글 이름을 더한다(Stamina 의 화면 용어 포함). §4.1 의 값2 1-기준을 캡처 수치로 다시 본 결과를 한 줄 적는다. STATUS §1.7 설명 문단에 PR1 화면 확인 결과(태스크 6 Step 7)도 여기서 같이 적는다.

- [ ] **Step 8: 커밋 · 푸시**

```bash
git -C E:/CDToybox branch --show-current   # feat/item-effects
git -C E:/CDToybox add docs/superpowers/specs/2026-09-22-item-description-effects-design.md docs/STATUS.md
git -C E:/CDToybox commit -m "docs(effects): 버프 데이터 9종 필드 실측 근거 · 툴팁 대조" -m "Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
git -C E:/CDToybox push
```

---

### Task 10: 2단계 플랜을 덧붙인다

코드를 바꾸지 않는다. superpowers:writing-plans 규칙(정확한 코드 · TDD · 태스크별 Interfaces)으로 이 문서 끝에 `## PR2 2단계 — 해석기 · 게시 · 화면` 을 덧붙인다. 입력은 태스크 9 가 스펙 §4.2 · §4.3 에 적은 근거뿐이다 — **거기 없는 필드 뜻은 쓰지 않는다.**

담을 태스크(스펙 §4.2~§4.5 · §5 · §6):
- 판정 "해석" 클래스마다 해석기 — 시험은 태스크 9 에서 덤프한 **실제 바이트**를 고정값으로 박는다(캡처 시험 관례). "개수만" 클래스는 셈만.
- 값2 로 단계를 고르고 같은 (스킬 행, 값2) 는 한 번만(스펙 §4.1 떡고기구이), `ChangeBuffLevelBuffData` 는 근거가 있을 때만 한 단계 더 따라가되 깊이 2 제한.
- 효과 줄 문구 — 한글 이름은 §4.3 표에 근거가 있는 것만, 나머지는 내부 이름.
- 불변 스냅샷 게시 `item_effects_for(key)` 와 분석 스레드 호출(`camera.cpp`, 아이템 표 뒤, 실패 시 다음 주기 재시도 · 로그 한 번), DLL 목록에 `item_effects.cpp`.
- 두 창: 요약 칸 규칙(효과가 있으면 효과 요약, 없으면 설명 첫 줄), 툴팁에 효과 목록과 "해석 못 한 효과 N개", 검색 문구에 효과.
- 문서 · PR2 · 배포 · 화면 확인.

덧붙인 뒤 스펙과 대조해 자체 점검(빈칸 · 이름 일치 · 스펙 누락)을 하고, 사용자에게 2단계 실행 방식을 묻는다.

---

## 자체 점검 (작성 시)

- 스펙 대응: §2 화면 → T5 · §3 설명 → T1~T3 · §5 검색 → T4 · §6 오류(설명 없음 · 현지화 늦음 · 표 못 찾음 · 행/개수 이상) → T3 · T7 시험 · §7 단위 시험 · 실측 · 캡처 → T1~T4 · T7 · T9 · §8 PR 둘 → T6 · PR2(T7~T10, 2단계).
- 2단계(해석기 · 게시 · 화면)는 실측 전이라 이 문서에 코드가 없다 — 태스크 10 이 채운다. 사용자 원칙("추측 금지")을 따른 의도된 경계다.
- 이름 일치: `kLocMaxDescText` · `clean_item_desc` · `desc_first_line` · `desc_key` · `desc` · `passes(..., text)` · `desc_cell` · `walk_item_effects` · `EffectTables` · `EffectWalk` · `EffectBuff` · `ClassOf` · `kEffectMax*` 가 정의한 태스크와 쓰는 태스크에서 같다.
