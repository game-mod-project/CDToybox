> **[2026-09-16] 구현 완료.** 필터바(`render/filter_bar`) · 필터 술어
> (`game/item_view`) · 보석 선택기(`render/gem_picker`) 전부 들어왔다.
> `docs/STATUS.md` §1.14. **아래 체크박스는 실행 당시 안 찍었다** — 미완으로 읽지 말 것.

# 패널 공통화 구현 계획 — 필터바·필터 술어·보석 선택기

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 아이템 목록·인벤토리 창이 각자 들고 있던 필터바와 거르는 규칙을 하나로 합치고(1단계, 화면 불변), 장비 창·지급 창의 보석 고르기를 팝업 하나로 통일한다(2단계, 화면 바뀜).

**Architecture:** 거르는 규칙(`passes`)과 Combo 색인 변환(`make_filter`)은 ImGui 를 모르는 `game/item_view` 에 두어 테스트로 덮는다. 그리기(`render/filter_bar`, `render/gem_picker`)는 상태 구조체를 부르는 창이 소유하는 위젯으로 만든다 — 전역 static 을 없앤다. 인벤토리는 그리는 루프 안에서 거르는 지금 방식을 그대로 두고 비교 3줄만 술어 호출로 바꾼다.

**Tech Stack:** C++20 · MSVC 2022 Build Tools · CMake + Ninja (`scripts/build.ps1`) · Dear ImGui · 자체 테스트 하니스 (`tests/harness.h`, `TEST`/`CHECK`/`CHECK_EQ`)

**Spec:** `docs/superpowers/specs/2026-09-10-panel-shared-widgets-design.md`

## Global Constraints

- 저장소는 `E:/CDToybox`. **모든 git·파일 명령에 절대경로 또는 `git -C E:/CDToybox` 를 쓴다.** `cd` 후 상대경로 금지.
- 명령의 경로는 **슬래시**로 쓴다(`E:/CDToybox/...`). Git Bash 에서 따옴표 없는 역슬래시는 먹혀 사라진다. PowerShell 도 슬래시를 받는다.
- 소스는 **BOM 없는 UTF-8**. 컴파일은 `/utf-8`. 한글 주석·문자열 그대로 쓴다.
- 새 `render/*.cpp` 는 **`cdtoybox` 타깃에만** 넣는다. `cdtb_tests`·`cdtb_probe` 는 ImGui 와 링크하지 않는다.
- `main` 은 릴리스 전용. `develop` 에서 분기하고 `develop` 으로 `--no-ff` 머지한다.
- 브랜치 이름: 1단계 `feat/filter-bar-shared`, 2단계 `feat/gem-picker-shared`.
- **커밋 직전마다 `git -C E:/CDToybox branch --show-current` 로 브랜치를 확인한다.** 이 저장소는 워크트리 하나에 세션이 동시에 붙는다. 다른 세션의 미커밋 변경(`git status`)이 보이면 그 파일에 손대지 않는다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` 를 붙인다.
- 1단계는 **화면이 그대로여야 한다.** 검색 버퍼 128, `hide_unnamed` 초기값(아이템 목록 `true`), 인벤의 키 매칭 끔(`match_key = false`) — 이 셋을 지킨다.
- 빌드: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1` (1~3분).
- 테스트: `E:/CDToybox/build/cdtb_tests.exe` — 마지막 줄 `N tests, 0 failures` 를 본다. 시작 시점 N = **364**. 개별 실행 필터는 없다.
- 콘솔이 CP949 라 한글 출력이 깨져 보일 수 있다. 파일 인코딩 문제가 아니다.

---

## 파일 구조

| 파일 | 책임 | 단계 |
|---|---|---|
| `src/game/item_view.{h,cpp}` (수정) | `passes` 술어 · `make_filter` 색인 변환 · `ItemFilter::match_key` | 1 |
| `src/render/filter_bar.{h,cpp}` (신규) | `FilterBar` 상태 + `draw_filter_bar` 그리기 + `to_filter` | 1 |
| `src/render/item_panel.cpp` (수정) | static 6개 → `FilterBar g_bar`, 자체 `draw_filter_bar` 삭제 | 1 |
| `src/render/inventory_panel.cpp` (수정) | static 5개 → `FilterBar g_bar`, 비교 3줄 → `passes` | 1 |
| `src/game/items.{h,cpp}` (수정) | `is_socket_gem` | 2 |
| `src/render/gem_picker.{h,cpp}` (신규) | `GemPicker` 상태 + 팝업 그리기 | 2 |
| `src/render/equip_panel.cpp` (수정) | 자체 보석 팝업 → `gem_picker` | 2 |
| `src/render/grant_panel.cpp` (수정) | 칸별 콤보 → 버튼 + `gem_picker` | 2 |
| `CMakeLists.txt` (수정) | 신규 cpp 2개를 `cdtoybox` 에 추가 | 1·2 |
| `tests/item_view_tests.cpp`, `tests/items_tests.cpp` (수정) | 테스트 | 1·2 |

---

## 1단계 — 필터바·필터 술어 (브랜치 `feat/filter-bar-shared`)

### Task 1: 작업 브랜치 + `passes()` 술어

**Files:**
- Modify: `src/game/item_view.h`
- Modify: `src/game/item_view.cpp`
- Test: `tests/item_view_tests.cpp`

**Interfaces:**
- Consumes: 기존 `cdtb::game::ItemFilter`, `filter_items`, 테스트 픽스처 `sample()`·`graded()` (같은 파일 안에 이미 있다)
- Produces: `bool cdtb::game::passes(const ItemFilter& f, std::string_view name, int grade, int category, std::uint32_t key)`; `ItemFilter::match_key` (bool, 기본 `true`)

- [ ] **Step 1: 브랜치를 만든다**

```
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox status --porcelain
```
Expected: `develop`, 그리고 두 번째는 빈 출력. 비어 있지 않으면 **멈추고 보고한다**(다른 세션의 작업이다).

```
git -C E:/CDToybox checkout -b feat/filter-bar-shared
git -C E:/CDToybox branch --show-current
```
Expected: `feat/filter-bar-shared`

- [ ] **Step 2: 실패하는 테스트를 쓴다**

`tests/item_view_tests.cpp` **맨 끝**(마지막 `}` 뒤)에 붙인다. `graded()` 는 이 파일 안에 이미 정의돼 있다.

```cpp

// ------------------------------------------------------- 술어 (passes)

TEST(passes_agrees_with_filter_items) {
    // filter_items 가 passes 를 부르도록 바뀌었다. 같은 입력에 같은
    // 판정을 내야 한다 - 이름·키·이름없음·등급·분류를 전부 돈다.
    const auto named = sample();
    const auto tiers = graded();

    ItemFilter fs[6];
    fs[1].query = "화살";
    fs[2].query = "9500";
    fs[3].hide_unnamed = true;
    fs[4].grade = 0;
    fs[5].category = 56;

    for (const auto* all : {&named, &tiers}) {
        for (const auto& f : fs) {
            const auto out = cdtb::game::filter_items(*all, f);
            std::size_t n = 0;
            for (const auto& e : *all) {
                bool in = false;
                for (const auto* o : out) {
                    if (o == &e) in = true;
                }
                const bool p = cdtb::game::passes(f, e.name, e.grade,
                                                  e.category, e.key);
                CHECK_EQ(p, in);
                if (p) ++n;
            }
            CHECK_EQ(n, out.size());
        }
    }
}

TEST(passes_ignores_key_when_match_key_is_off) {
    // 인벤토리는 이름만 본다. 숫자를 쳐도 키가 걸리면 안 된다.
    ItemFilter f;
    f.query = "9500";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
    f.match_key = true;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
}

TEST(passes_with_key_off_still_matches_name) {
    ItemFilter f;
    f.query = "도끼";
    f.match_key = false;
    CHECK(cdtb::game::passes(f, "벌목용 도끼", 0, 0, 950002u));
}

TEST(passes_with_key_off_drops_unnamed_even_if_key_matches) {
    // 이름 없는 것은 키로만 찾을 수 있는데, 키를 안 보면 못 찾는다.
    ItemFilter f;
    f.query = "200997";
    f.match_key = false;
    CHECK(!cdtb::game::passes(f, "", 0, 0, 200997u));
}
```

- [ ] **Step 3: 빌드해서 실패를 확인한다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: 실패. 오류에 `'passes': is not a member of 'cdtb::game'` 와 `'match_key': is not a member` 가 보인다.

- [ ] **Step 4: 헤더에 `match_key` 와 `passes` 를 더한다**

`src/game/item_view.h` 의 표준 include 를 이렇게 바꾼다. **그 아래의 `#include "game/items.h"` 는 그대로 둔다** — `ItemCatalogEntry` 가 거기서 온다.

```cpp
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
```

`struct ItemFilter` 를 이것으로 교체한다:

```cpp
struct ItemFilter {
    std::string query;          // 이름 또는 키에 걸린다
    bool hide_unnamed = false;  // 현지화 표에 없는 것을 감춘다
    int grade = -1;             // -1 = 전체, 0 = 등급 없음, 1..5 = T1..T5
    int category = -1;          // -1 = 전체
    // query 를 키 문자열에도 거는가. 아이템 목록은 건다("이름 또는 키로
    // 검색"). 인벤토리는 이름만 본다 - 이 차이를 숨기면 인벤에서 숫자를
    // 쳤을 때 동작이 조용히 바뀐다.
    bool match_key = true;
};
```

`enum class ItemSort` 선언 **바로 앞**에 넣는다:

```cpp
// 한 항목이 필터를 통과하는가. filter_items 가 이것을 부르고, 인벤토리
// 창은 자기 행에 직접 건다 - 거르는 규칙이 두 곳에 있으면 갈라진다.
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key);

```

- [ ] **Step 5: 구현한다 — `matches` 를 `passes` 로 흡수한다**

`src/game/item_view.cpp` 에서 익명 네임스페이스 안의 `bool matches(...)` 함수(주석 "이름과 키 둘 다에 건다" 포함, 7줄)를 **삭제**한다. `compare_by` 는 그대로 둔다.

`filter_items` 정의 **바로 앞**(익명 네임스페이스가 닫힌 뒤)에 넣는다:

```cpp
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key) {
    if (f.hide_unnamed && name.empty()) return false;
    if (f.grade >= 0 && grade != f.grade) return false;
    if (f.category >= 0 && category != f.category) return false;
    if (f.query.empty()) return true;
    if (!name.empty() && name.find(f.query) != std::string_view::npos) {
        return true;
    }
    // 이름과 키 둘 다에 건다. 지급 대상을 키로만 아는 경우가 있다.
    if (!f.match_key) return false;
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%u", key);
    return std::string_view(digits).find(f.query) != std::string_view::npos;
}

```

`filter_items` 의 루프 본문을 이것으로 교체한다:

```cpp
    for (const auto& e : all) {
        if (!passes(filter, e.name, e.grade, e.category, e.key)) continue;
        out.push_back(&e);
    }
```

- [ ] **Step 6: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: 마지막 줄 `368 tests, 0 failures`. 기존 `filter_items_*` 19개가 그대로 통과하는 것이 동작 불변의 증거다.

- [ ] **Step 7: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/filter-bar-shared`. 아니면 멈춘다.

```
git -C E:/CDToybox add src/game/item_view.h src/game/item_view.cpp tests/item_view_tests.cpp
git -C E:/CDToybox commit -m "리팩토링: 거르는 규칙을 passes 술어로 뺀다 - 인벤토리도 같은 것을 쓰게" -m "filter_items 의 안쪽만 바뀐다. 기존 테스트 19개 그대로 통과. match_key 로 아이템 목록(키로도 찾음)과 인벤(이름만)의 차이를 드러낸다." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 2: `make_filter()` — Combo 색인 → 필터

**Files:**
- Modify: `src/game/item_view.h`
- Modify: `src/game/item_view.cpp`
- Test: `tests/item_view_tests.cpp`

**Interfaces:**
- Consumes: Task 1 의 `ItemFilter`
- Produces: `ItemFilter cdtb::game::make_filter(std::string_view query, int grade_idx, int category_idx, bool hide_unnamed, const std::vector<std::uint8_t>& categories)`

- [ ] **Step 1: 실패하는 테스트를 쓴다**

`tests/item_view_tests.cpp` 맨 끝에 붙인다:

```cpp

// ------------------------------------------------- Combo 색인 -> 필터

TEST(make_filter_index_zero_means_all) {
    const std::vector<std::uint8_t> cats = {56, 22};
    const auto f = cdtb::game::make_filter("", 0, 0, false, cats);
    CHECK_EQ(f.grade, -1);
    CHECK_EQ(f.category, -1);
    CHECK(f.query.empty());
    CHECK(!f.hide_unnamed);
    CHECK(f.match_key);
}

TEST(make_filter_grade_index_is_one_past_the_grade) {
    // Combo 는 0 이 "전체" 라 등급이 한 칸 밀려 있다. 1 이 등급 0(없음).
    const std::vector<std::uint8_t> cats;
    CHECK_EQ(cdtb::game::make_filter("", 1, 0, false, cats).grade, 0);
    CHECK_EQ(cdtb::game::make_filter("", 6, 0, false, cats).grade, 5);
}

TEST(make_filter_category_index_looks_up_the_table) {
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, cats).category, 56);
    CHECK_EQ(cdtb::game::make_filter("", 0, 2, false, cats).category, 22);
}

TEST(make_filter_category_index_past_the_table_means_all) {
    // 카탈로그가 새 판으로 갈리면 Combo 색인이 표 길이를 넘을 수 있다.
    // 그때 배열 밖을 읽지 말고 "전체" 로 떨어져야 한다.
    const std::vector<std::uint8_t> cats = {56, 22};
    CHECK_EQ(cdtb::game::make_filter("", 0, 3, false, cats).category, -1);
    const std::vector<std::uint8_t> none;
    CHECK_EQ(cdtb::game::make_filter("", 0, 1, false, none).category, -1);
}

TEST(make_filter_copies_query_and_hide_unnamed) {
    const std::vector<std::uint8_t> cats;
    const auto f = cdtb::game::make_filter("화살", 0, 0, true, cats);
    CHECK_EQ(f.query, std::string("화살"));
    CHECK(f.hide_unnamed);
}
```

- [ ] **Step 2: 빌드해서 실패를 확인한다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: 실패. `'make_filter': is not a member of 'cdtb::game'`.

- [ ] **Step 3: 선언을 더한다**

`src/game/item_view.h` 에서 `passes` 선언 **바로 뒤**에 넣는다:

```cpp
// Combo 색인을 필터로 옮긴다. 색인 0 은 "전체". 등급은 색인-1 이고,
// 분류는 `categories[색인-1]` 이다(render 의 build_category_labels 가
// 만든 표). 색인이 표를 넘으면 전체로 떨어진다 - 카탈로그가 새 판으로
// 갈리면 그럴 수 있다. 아이템 목록과 인벤토리에 글자 그대로 복제돼
// 있던 것을 여기 한 곳으로 모았다.
ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories);

```

- [ ] **Step 4: 구현한다**

`src/game/item_view.cpp` 에서 `passes` 정의 **바로 뒤**에 넣는다:

```cpp
ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories) {
    ItemFilter f;
    f.query.assign(query.data(), query.size());
    f.hide_unnamed = hide_unnamed;
    f.grade = (grade_idx <= 0) ? -1 : grade_idx - 1;
    const bool cat_ok = category_idx > 0 &&
                        category_idx <= static_cast<int>(categories.size());
    f.category = cat_ok
                     ? categories[static_cast<std::size_t>(category_idx - 1)]
                     : -1;
    return f;
}

```

- [ ] **Step 5: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `373 tests, 0 failures`

- [ ] **Step 6: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/filter-bar-shared`

```
git -C E:/CDToybox add src/game/item_view.h src/game/item_view.cpp tests/item_view_tests.cpp
git -C E:/CDToybox commit -m "리팩토링: Combo 색인 -> 필터 변환을 make_filter 한 곳으로" -m "아이템 목록과 인벤토리에 글자 그대로 복제돼 있던 off-by-one 이다. 표를 넘는 색인이 전체로 떨어지는 것까지 테스트로 덮는다." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 3: `filter_bar` 위젯 + 아이템 목록 적용

**Files:**
- Create: `src/render/filter_bar.h`
- Create: `src/render/filter_bar.cpp`
- Modify: `CMakeLists.txt` (`cdtoybox` 타깃, `src/render/item_style.cpp` 줄 뒤)
- Modify: `src/render/item_panel.cpp`

**Interfaces:**
- Consumes: Task 1·2 의 `passes`·`make_filter`; 기존 `render/item_style.h` 의 `text_width`, `flow_same_line`, `kGradeLabels`, `build_category_labels`
- Produces: `struct cdtb::render::FilterBar`, `struct FilterBarOpts { const char* id; const char* hint; bool show_hide_unnamed; }`, `bool draw_filter_bar(FilterBar*, const FilterBarOpts&)`, `game::ItemFilter to_filter(const FilterBar&)`

이 태스크는 ImGui 코드라 단위 테스트가 없다. 검증은 빌드 + 기존 테스트 + (Task 5 에서) 화면이다.

- [ ] **Step 1: 헤더를 만든다**

`src/render/filter_bar.h`:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/item_view.h"

namespace cdtb::render {

// 검색 · 지우기 · (이름 없는 것 감추기) · 등급 · 분류 한 줄의 상태.
// 아이템 목록과 인벤토리가 각자 static 으로 들고 있던 것을 합쳤다.
// 부르는 창이 하나씩 소유한다.
struct FilterBar {
    // 128 이다. 아이템 목록이 128, 인벤이 64 를 쓰고 있었다. 64 로
    // 맞추면 아이템 목록에서 긴 검색어가 잘린다 - 넓은 쪽을 쓴다.
    char query[128] = "";
    int grade_idx = 0;        // 0 = 전체, 1 = 등급 없음, 2..6 = T1..T5
    int category_idx = 0;     // 0 = 전체, 그 뒤는 categories 의 색인-1
    // 기본값이 창마다 다르다. 아이템 목록은 켜져 있고 인벤은 이 칸이
    // 아예 없었다. 부르는 쪽이 초기화한다.
    bool hide_unnamed = false;
    std::vector<std::uint8_t> categories;   // 표에 실제로 있는 분류 값
    std::string category_labels;            // Combo 용 널 구분 문자열
};

struct FilterBarOpts {
    const char* id = "filter";            // ImGui ID 범위. 창마다 다르게
    const char* hint = "이름으로 검색";    // 검색창 힌트
    bool show_hide_unnamed = false;       // 아이템 목록만 켠다
};

// 한 줄을 그린다. 값이 바뀌었으면 true - 무엇을 할지는 부르는 쪽 일이다
// (아이템 목록은 뷰를 다시 만들고 쪽을 0 으로, 인벤은 매 프레임 거르니
// 아무것도 안 한다). 분류 Combo 는 category_labels 가 비어 있으면 그리지
// 않는다 - 인벤은 읽은 뒤에야 분류가 생긴다.
bool draw_filter_bar(FilterBar* s, const FilterBarOpts& o);

// 상태 -> 필터. game::make_filter 로 위임한다. match_key 는 기본(true)
// 이다 - 인벤은 받은 뒤 false 로 끈다.
game::ItemFilter to_filter(const FilterBar& s);

}  // namespace cdtb::render
```

- [ ] **Step 2: 구현을 만든다**

`src/render/filter_bar.cpp`:

```cpp
#include "render/filter_bar.h"

#include <imgui.h>

#include "render/item_style.h"

namespace cdtb::render {

bool draw_filter_bar(FilterBar* s, const FilterBarOpts& o) {
    bool changed = false;
    const ImGuiStyle& st = ImGui::GetStyle();
    const float clear_w = text_width("지우기") + st.FramePadding.x * 2.0f;
    const float check_w = text_width("이름 없는 것 감추기") +
                          ImGui::GetFrameHeight() + st.ItemInnerSpacing.x;
    const float grade_w = text_width("등급") + st.ItemInnerSpacing.x + 120.0f;
    const float cat_w = text_width("분류") + st.ItemInnerSpacing.x + 230.0f;

    ImGui::PushID(o.id);

    // 검색창은 남은 폭을 쓰되 상한을 둔다. 상한이 없으면 창을 넓혔을
    // 때 검색창만 늘어나 오른쪽 항목이 전부 밀려 잘린다.
    float query_w = ImGui::GetContentRegionAvail().x - clear_w -
                    st.ItemSpacing.x;
    if (query_w > 420.0f) query_w = 420.0f;
    if (query_w < 140.0f) query_w = 140.0f;
    ImGui::SetNextItemWidth(query_w);
    if (ImGui::InputTextWithHint("##query", o.hint, s->query,
                                 sizeof(s->query))) {
        changed = true;
    }

    flow_same_line(clear_w);
    if (ImGui::Button("지우기")) {
        s->query[0] = '\0';
        changed = true;
    }

    if (o.show_hide_unnamed) {
        flow_same_line(check_w);
        if (ImGui::Checkbox("이름 없는 것 감추기", &s->hide_unnamed)) {
            changed = true;
        }
    }

    // 라벨을 위젯 **앞**에 둔다. ImGui 기본은 뒤에 붙는데, 그러면
    // "전체 ▼ 등급" 처럼 읽혀 무엇을 고르는 칸인지 헷갈린다.
    flow_same_line(grade_w);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("등급");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    // 목록이 길다. 잘리지 않도록 펼침 높이를 넉넉히 준다.
    if (ImGui::Combo("##grade", &s->grade_idx, kGradeLabels, 12)) {
        changed = true;
    }

    // 분류는 표를 읽은 뒤에야 만들어진다.
    if (!s->category_labels.empty()) {
        flow_same_line(cat_w);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("분류");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(230.0f);
        if (ImGui::Combo("##category", &s->category_idx,
                         s->category_labels.c_str(), 20)) {
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

game::ItemFilter to_filter(const FilterBar& s) {
    return game::make_filter(s.query, s.grade_idx, s.category_idx,
                             s.hide_unnamed, s.categories);
}

}  // namespace cdtb::render
```

- [ ] **Step 3: CMake 에 넣는다**

`CMakeLists.txt` 의 `add_library(cdtoybox SHARED` 목록에서 `    src/render/item_style.cpp` 줄 **바로 다음**에 한 줄 추가:

```
    src/render/filter_bar.cpp
```

`cdtb_tests`·`cdtb_probe` 목록에는 넣지 않는다.

- [ ] **Step 4: `item_panel.cpp` 를 바꾼다 — include**

`#include "game/item_view.h"` 줄 다음에 추가:

```cpp
#include "render/filter_bar.h"
```

- [ ] **Step 5: `item_panel.cpp` — static 을 `FilterBar` 로**

이 블록(파일 25~39줄, `char g_query[128] = "";` 부터 `bool g_dirty = true;` 까지)을:

```cpp
char g_query[128] = "";
// 기본으로 켜 둔다. 이름이 안 풀린 72개는 대개 개발용이라 목록에
// 있어도 쓸모가 없다. 필요하면 체크를 풀면 된다.
bool g_hide_unnamed = true;
int g_per_page_idx = 1;                      // 아래 표의 첨자
int g_grade_idx = 0;                         // 0=전체, 1=없음, 2..6=T1..T5
int g_category_idx = 0;                      // 0=전체, 그 뒤는 g_categories
game::ItemSort g_sort = game::ItemSort::Key;
bool g_ascending = true;
std::size_t g_page = 0;

std::vector<const game::ItemCatalogEntry*> g_view;
std::vector<std::uint8_t> g_categories;      // 표에 실제로 있는 분류 값
std::string g_category_labels;               // Combo 용 널 구분 문자열
bool g_dirty = true;
```

이것으로 교체한다:

```cpp
// 검색·등급·분류 줄. 인벤토리 창과 같은 위젯(render/filter_bar)을 쓴다.
// '이름 없는 것 감추기' 는 기본으로 켜 둔다. 이름이 안 풀린 72개는 대개
// 개발용이라 목록에 있어도 쓸모가 없다. 필요하면 체크를 풀면 된다.
FilterBar g_bar{.hide_unnamed = true};
int g_per_page_idx = 1;                      // 아래 표의 첨자
game::ItemSort g_sort = game::ItemSort::Key;
bool g_ascending = true;
std::size_t g_page = 0;

std::vector<const game::ItemCatalogEntry*> g_view;
bool g_dirty = true;
```

- [ ] **Step 6: `item_panel.cpp` — `rebuild_categories` 와 `rebuild`**

`rebuild_categories` 를 이것으로 교체:

```cpp
void rebuild_categories() {
    build_category_labels(&g_bar.categories, &g_bar.category_labels);
}
```

`rebuild()` 안에서 이 8줄을:

```cpp
    game::ItemFilter f;
    f.query = g_query;
    f.hide_unnamed = g_hide_unnamed;
    f.grade = (g_grade_idx == 0) ? -1 : g_grade_idx - 1;
    f.category = (g_category_idx == 0 ||
                  g_category_idx > static_cast<int>(g_categories.size()))
                     ? -1
                     : g_categories[g_category_idx - 1];
    g_view = game::filter_items(all, f);
```

이 한 줄로 교체:

```cpp
    g_view = game::filter_items(all, to_filter(g_bar));
```

- [ ] **Step 7: `item_panel.cpp` — 자체 `draw_filter_bar` 삭제, 호출부 교체**

익명 네임스페이스 안의 `void draw_filter_bar() {` 부터 그 함수의 닫는 `}` 까지(원본 82~140줄, 59줄) **통째로 삭제**한다. 바로 앞의 `labeled_w` 는 건드리지 않는다.

`draw_item_panel` 안의 `    draw_filter_bar();` 한 줄을 이것으로 교체:

```cpp
    {
        FilterBarOpts o;
        o.id = "items";
        o.hint = "이름 또는 키로 검색";
        o.show_hide_unnamed = true;
        if (draw_filter_bar(&g_bar, o)) {
            g_dirty = true;
            g_page = 0;
        }
    }
```

- [ ] **Step 8: 남은 참조가 없는지 확인한다**

Run: `grep -n "g_query\|g_hide_unnamed\|g_grade_idx\|g_category_idx\|g_categories\|g_category_labels" E:/CDToybox/src/render/item_panel.cpp`
Expected: 출력 없음.

- [ ] **Step 9: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`. 경고 중 `filter_bar` 관련이 없어야 한다.

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `373 tests, 0 failures` (render 는 테스트에 안 들어가므로 수가 안 변한다)

- [ ] **Step 10: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/filter-bar-shared`

```
git -C E:/CDToybox add CMakeLists.txt src/render/filter_bar.h src/render/filter_bar.cpp src/render/item_panel.cpp
git -C E:/CDToybox commit -m "리팩토링: 필터바를 위젯으로 빼고 아이템 목록이 쓴다" -m "폭 계산·라벨 앞배치·지우기 버튼이 render/filter_bar 한 곳으로. 검색 버퍼 128, hide_unnamed 기본 켬 - 화면은 그대로다." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 4: 인벤토리 창 적용

**Files:**
- Modify: `src/render/inventory_panel.cpp`

**Interfaces:**
- Consumes: Task 3 의 `FilterBar`·`FilterBarOpts`·`draw_filter_bar`·`to_filter`; Task 1 의 `passes`·`match_key`
- Produces: 없음 (호출부)

- [ ] **Step 1: include**

`#include "game/items.h"` 줄 다음에 추가:

```cpp
#include "game/item_view.h"
```

`#include "render/item_style.h"` 줄 다음에 추가:

```cpp
#include "render/filter_bar.h"
```

- [ ] **Step 2: static 을 `FilterBar` 로**

이 7줄(원본 64~70줄)을:

```cpp
// 걸러 내기는 아이템 목록과 같은 모양이다 - 검색 · 등급 · 분류.
// 헬퍼는 item_style 에 함께 둔다. 한쪽만 고치면 두 창이 달라진다.
char g_query[64]{};
int g_grade_idx = 0;                 // 0 = 전체
int g_category_idx = 0;              // 0 = 전체
std::vector<std::uint8_t> g_categories;
std::string g_category_labels;
```

이것으로 교체:

```cpp
// 걸러 내기는 아이템 목록과 같은 모양이다 - 검색 · 등급 · 분류. 같은
// 위젯(render/filter_bar)을 쓴다. '이름 없는 것 감추기' 는 이 창에 없다.
FilterBar g_bar;
```

- [ ] **Step 3: 분류 표 만들기**

`refresh` 안의 `    build_category_labels(&g_categories, &g_category_labels);` 를:

```cpp
    build_category_labels(&g_bar.categories, &g_bar.category_labels);
```

- [ ] **Step 4: 자체 `draw_filter_bar` 삭제**

주석 3줄("아이템 목록과 같은 줄 구성이다…")부터 `void draw_filter_bar() {` 와 그 함수의 닫는 `}` 까지(원본 252~293줄) **통째로 삭제**한다. 바로 뒤의 `}  // namespace` 는 남긴다.

- [ ] **Step 5: 호출부와 필터 계산**

`draw_inventory_panel` 안의 이 9줄(원본 579~587줄)을:

```cpp
    draw_filter_bar();

    const int want_grade = (g_grade_idx == 0) ? -1 : g_grade_idx - 1;
    const int want_cat =
        (g_category_idx == 0 ||
         g_category_idx > static_cast<int>(g_categories.size()))
            ? -1
            : static_cast<int>(g_categories[static_cast<std::size_t>(
                  g_category_idx - 1)]);
```

이것으로 교체:

```cpp
    {
        FilterBarOpts o;
        o.id = "inv";
        o.hint = "이름으로 검색";
        draw_filter_bar(&g_bar, o);   // 매 프레임 거르므로 반환값은 안 쓴다
    }
    // 이 창은 이름만 본다. 키 문자열까지 걸면 숫자를 쳤을 때 동작이
    // 바뀐다 - 켤지는 힌트 문구와 함께 정한다(스펙 3.2).
    game::ItemFilter filter = to_filter(g_bar);
    filter.match_key = false;
```

- [ ] **Step 6: 그리는 루프의 비교 3줄**

루프 안의 이 6줄(원본 622~627줄)을:

```cpp
            if (want_grade >= 0 && r.grade != want_grade) continue;
            if (want_cat >= 0 && r.category != want_cat) continue;
            if (g_query[0] != 0 &&
                r.name.find(g_query) == std::string::npos) {
                continue;
            }
```

이것으로 교체:

```cpp
            if (!game::passes(filter, r.name, r.grade, r.category, r.key)) {
                continue;
            }
```

- [ ] **Step 7: 남은 참조가 없는지 확인한다**

Run: `grep -n "g_query\|g_grade_idx\|g_category_idx\|g_categories\|g_category_labels\|want_grade\|want_cat" E:/CDToybox/src/render/inventory_panel.cpp`
Expected: 출력 없음.

- [ ] **Step 8: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `373 tests, 0 failures`

- [ ] **Step 9: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/filter-bar-shared`

```
git -C E:/CDToybox add src/render/inventory_panel.cpp
git -C E:/CDToybox commit -m "리팩토링: 인벤토리 창이 필터바 위젯과 passes 술어를 쓴다" -m "손코딩 비교 3줄이 테스트된 술어 한 줄로. match_key 를 꺼서 이름만 보던 동작을 그대로 지킨다. 그리는 루프 안에서 거르는 방식은 안 바꾼다." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 5: 1단계 머지 · 배포 · 화면 확인 (사람 게이트)

**Files:** 없음 (git · 빌드 · 배포)

- [ ] **Step 1: 브랜치와 작업트리를 확인한다**

```
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox status --porcelain
```
Expected: `feat/filter-bar-shared`, 빈 출력. 다른 세션의 변경이 보이면 멈추고 보고한다.

- [ ] **Step 2: develop 으로 머지한다**

```
git -C E:/CDToybox checkout develop
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox merge --no-ff feat/filter-bar-shared -m "머지: 필터바·필터 술어 공통화 (2026-09-10)" -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
git -C E:/CDToybox log --oneline -6
```
Expected: 두 번째 줄 `develop`; 마지막에 머지 커밋 아래 Task 1~4 커밋 4개가 보인다.

- [ ] **Step 3: 머지 결과를 빌드하고 테스트한다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `373 tests, 0 failures`

- [ ] **Step 4: 배포한다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1`
Expected: `deployed -> E:\SteamLibrary\steamapps\common\Crimson Desert\bin64\xinput1_4.dll`. 게임이 실행 중이면 거부한다 — 그러면 사용자에게 게임을 끄고 다시 돌려 달라고 한다.

- [ ] **Step 5: 멈추고 사람에게 넘긴다 — 여기서 Task 6 으로 넘어가지 않는다**

사용자에게 이렇게 보고한다:

> 1단계 배포 완료. 게임에서 **아이템 목록** 과 **인벤토리** 두 창을 열어 봐 주세요. 볼 것은 "이전과 똑같은가" 하나입니다 — 검색·지우기·등급·분류(아이템 목록은 '이름 없는 것 감추기' 까지)가 전과 같이 거르는지, 인벤토리에서 숫자를 쳤을 때 **아무것도 안 걸리는지**(전과 같아야 합니다).

사용자가 "달라진 게 없다" 고 확인해야 2단계로 간다. 달라진 것이 있으면 무엇이 어떻게 달라졌는지 받아 적고 Task 1~4 중 어디가 원인인지 짚은 뒤 고친다.

---

## 2단계 — 보석 선택기 (브랜치 `feat/gem-picker-shared`)

### Task 6: 작업 브랜치 + `is_socket_gem()`

**Files:**
- Modify: `src/game/items.h`
- Modify: `src/game/items.cpp`
- Test: `tests/items_tests.cpp`

**Interfaces:**
- Consumes: 기존 `cdtb::game::ItemCatalogEntry`, `kSocketGemCategory` (= 74, `items.h`)
- Produces: `bool cdtb::game::is_socket_gem(const ItemCatalogEntry& e)`

- [ ] **Step 1: 브랜치를 만든다**

```
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox status --porcelain
```
Expected: `develop`, 빈 출력.

```
git -C E:/CDToybox checkout -b feat/gem-picker-shared
git -C E:/CDToybox branch --show-current
```
Expected: `feat/gem-picker-shared`

- [ ] **Step 2: 실패하는 테스트를 쓴다**

`tests/items_tests.cpp` 맨 끝에 붙인다:

```cpp

// ------------------------------------------------------ 보석 거르기

TEST(is_socket_gem_needs_category_74_and_a_name) {
    // 장비 창과 지급 창의 보석 고르기가 같은 조건으로 거른다.
    cdtb::game::ItemCatalogEntry e;
    e.category = cdtb::game::kSocketGemCategory;
    e.name = "바람 가르기";
    CHECK(cdtb::game::is_socket_gem(e));
    // 이름이 안 풀린 것은 고를 수 없다 - 목록에 빈 줄이 뜬다.
    e.name.clear();
    CHECK(!cdtb::game::is_socket_gem(e));
    // 분류가 다르면 이름이 있어도 아니다.
    e.name = "한손검";
    e.category = 56;
    CHECK(!cdtb::game::is_socket_gem(e));
}
```

- [ ] **Step 3: 빌드해서 실패를 확인한다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: 실패. `'is_socket_gem': is not a member of 'cdtb::game'`.

- [ ] **Step 4: 선언과 구현**

`src/game/items.h` 에서 `inline constexpr std::uint8_t kSocketGemCategory = 74;` 줄 **바로 뒤**에 넣는다:

```cpp

// 소켓에 박을 수 있는 강화 보석인가 - 분류 74 이고 이름이 풀린 것.
// 보석 고르기(장비 창·지급 창)가 같은 조건으로 거른다.
bool is_socket_gem(const ItemCatalogEntry& e);
```

`src/game/items.cpp` 의 마지막 줄 `}  // namespace cdtb::game` **바로 앞**에 넣는다:

```cpp
bool is_socket_gem(const ItemCatalogEntry& e) {
    return e.category == kSocketGemCategory && !e.name.empty();
}

```

- [ ] **Step 5: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `374 tests, 0 failures`

- [ ] **Step 6: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/gem-picker-shared`

```
git -C E:/CDToybox add src/game/items.h src/game/items.cpp tests/items_tests.cpp
git -C E:/CDToybox commit -m "추가: is_socket_gem - 보석 고르기 두 곳이 같은 조건으로 거른다" -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 7: `gem_picker` 위젯 + 장비 창 적용

**Files:**
- Create: `src/render/gem_picker.h`
- Create: `src/render/gem_picker.cpp`
- Modify: `CMakeLists.txt` (`cdtoybox` 타깃, `src/render/filter_bar.cpp` 줄 뒤)
- Modify: `src/render/equip_panel.cpp`

**Interfaces:**
- Consumes: Task 6 의 `is_socket_gem`; 기존 `game::items_ready()`, `game::item_catalog()`, `game::eq_write_socket`, `game::equip_refresh_pieces`
- Produces: `struct cdtb::render::GemPicker { bool open_requested; char search[64]; }`, `struct GemChoice { const game::ItemCatalogEntry* entry; std::size_t index; }`, `struct GemPickerOpts { const char* title; bool allow_empty; std::uint32_t selected_key; }`, `void gem_picker_open(GemPicker*)`, `bool gem_picker_draw(GemPicker*, const GemPickerOpts&, GemChoice*)`

스펙 4.2 는 상태를 "static 하나" 로 적었는데, 두 창이 동시에 열려 있을 때 열림 요청을 전역 하나에 두면 다른 창의 그리기가 먼저 돌아 그쪽에서 팝업이 뜬다. 그래서 상태 구조체를 부르는 창이 하나씩 갖는다. 나머지는 스펙 그대로다.

- [ ] **Step 1: 헤더를 만든다**

`src/render/gem_picker.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "game/items.h"

namespace cdtb::render {

// 보석 고르기 팝업. 장비 소켓 창과 지급 창이 같은 것을 쓴다.
//
// 상태는 부르는 창이 하나씩 가진다. 두 창이 동시에 열려 있을 수 있는데,
// 열림 요청을 전역 하나에 두면 다른 창의 그리기가 먼저 돌아 그쪽에서
// 팝업이 뜬다.
struct GemPicker {
    bool open_requested = false;
    char search[64] = "";
};

// 고른 것. 두 창이 원하는 값이 다르다 - 장비 창은 카탈로그 순번을 쓰고
// (eq_write_socket), 지급 창은 키를 쓴다(g_socket_keys). 같은 카탈로그를
// 보므로 둘을 함께 준다. allow_empty 로 "(빈 칸으로 열기)" 를 고르면
// entry 가 nullptr 이다.
struct GemChoice {
    const game::ItemCatalogEntry* entry = nullptr;
    std::size_t index = 0;
};

struct GemPickerOpts {
    const char* title = "소켓에 박을 강화 보석(분류 74)";
    bool allow_empty = false;         // "(빈 칸으로 열기)" 행 - 지급 창만
    std::uint32_t selected_key = 0;   // 현재 값. 목록에서 강조한다
};

// 다음 gem_picker_draw 에서 팝업을 연다. 버튼이 PushID 안에 있어도
// 된다 - 여는 것은 draw 가 자기 ID 범위에서 한다.
void gem_picker_open(GemPicker* p);

// 팝업을 그린다. 창의 최상위 ID 범위에서 매 프레임 부른다. 골랐으면
// true 를 내고 팝업을 닫는다.
bool gem_picker_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out);

}  // namespace cdtb::render
```

- [ ] **Step 2: 구현을 만든다**

`src/render/gem_picker.cpp`:

```cpp
#include "render/gem_picker.h"

#include <imgui.h>

#include <string>

namespace cdtb::render {
namespace {

constexpr const char* kPopupId = "보석 고르기##gem_picker";

}  // namespace

void gem_picker_open(GemPicker* p) {
    p->open_requested = true;
    p->search[0] = '\0';
}

bool gem_picker_draw(GemPicker* p, const GemPickerOpts& o, GemChoice* out) {
    if (p->open_requested) {
        ImGui::OpenPopup(kPopupId);
        p->open_requested = false;
    }
    if (!ImGui::BeginPopup(kPopupId)) return false;

    bool chosen = false;
    ImGui::TextUnformatted(o.title);
    // 열리자마자 타자를 칠 수 있게 검색창에 포커스를 준다.
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputTextWithHint("##search", "이름으로 찾기", p->search,
                             sizeof(p->search));

    ImGui::BeginChild("list", ImVec2(320.0f, 280.0f));
    if (o.allow_empty) {
        if (ImGui::Selectable("(빈 칸으로 열기)", o.selected_key == 0)) {
            out->entry = nullptr;
            out->index = 0;
            chosen = true;
        }
        ImGui::Separator();
    }
    if (!chosen) {
        if (!game::items_ready()) {
            ImGui::TextDisabled("아이템 표를 아직 못 읽었습니다");
        } else {
            const auto& cat = game::item_catalog();
            int shown = 0;
            for (std::size_t i = 0; i < cat.size() && !chosen; ++i) {
                const auto& e = cat[i];
                if (!game::is_socket_gem(e)) continue;
                if (p->search[0] != '\0' &&
                    e.name.find(p->search) == std::string::npos) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(e.name.c_str(),
                                      e.key == o.selected_key)) {
                    out->entry = &e;
                    out->index = i;
                    chosen = true;
                }
                ImGui::PopID();
                // 190종이라 다 그려도 되지만, 표가 커지면 무거워진다.
                if (++shown >= 400) break;
            }
            if (shown == 0) ImGui::TextDisabled("맞는 보석이 없습니다");
        }
    }
    ImGui::EndChild();

    if (chosen) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return chosen;
}

}  // namespace cdtb::render
```

- [ ] **Step 3: CMake 에 넣는다**

`CMakeLists.txt` 의 `add_library(cdtoybox SHARED` 목록에서 `    src/render/filter_bar.cpp` 줄 **바로 다음**에 추가:

```
    src/render/gem_picker.cpp
```

- [ ] **Step 4: `equip_panel.cpp` — include 와 static**

`#include "mem/reader.h"` 줄 다음에 추가:

```cpp
#include "render/gem_picker.h"
```

이 4줄(원본 19~22줄)을:

```cpp
std::uint64_t g_gem_inst = 0;   // 보석을 박을 대상 아이템 인스턴스
int g_gem_k = -1;               // 그 아이템의 소켓 칸
bool g_open_gem = false;
char g_gem_search[64]{};
```

이것으로 교체:

```cpp
std::uint64_t g_gem_inst = 0;   // 보석을 박을 대상 아이템 인스턴스
int g_gem_k = -1;               // 그 아이템의 소켓 칸
GemPicker g_gem_picker;         // 보석 고르기 팝업 (지급 창과 같은 위젯)
```

- [ ] **Step 5: `equip_panel.cpp` — `draw_gem_popup` 교체**

`void draw_gem_popup(const mem::Reader& reader) {` 부터 그 함수의 닫는 `}` 까지(원본 41~82줄)를 이것으로 교체:

```cpp
void draw_gem_popup(const mem::Reader& reader) {
    GemPickerOpts o;   // 제목 기본값, 빈 칸 없음, 강조 없음
    GemChoice c;
    if (!gem_picker_draw(&g_gem_picker, o, &c) || c.entry == nullptr) return;
    // 소켓에 박는 값은 그 보석의 순번(= 카탈로그 인덱스).
    const int w = game::eq_write_socket(reader, g_gem_inst, g_gem_k,
                                        static_cast<std::uint16_t>(c.index));
    std::snprintf(g_msg, sizeof(g_msg),
                  w > 0 ? "소켓 %d에 '%s' 박음 (%d realm). RE-EQUIP"
                          " 하면 보입니다."
                        : "쓰기 실패 (잠긴 소켓이거나 대상 없음).",
                  g_gem_k, c.entry->name.c_str(), w);
    game::equip_refresh_pieces(reader);
}
```

- [ ] **Step 6: `equip_panel.cpp` — 여는 쪽**

`채우기` 버튼 블록(원본 293~298줄)을:

```cpp
                    if (ImGui::SmallButton("채우기")) {
                        g_gem_inst = w.instance;
                        g_gem_k = k;
                        g_gem_search[0] = 0;
                        g_open_gem = true;
                    }
```

이것으로 교체:

```cpp
                    if (ImGui::SmallButton("채우기")) {
                        g_gem_inst = w.instance;
                        g_gem_k = k;
                        gem_picker_open(&g_gem_picker);
                    }
```

- [ ] **Step 7: 남은 참조가 없는지 확인한다**

Run: `grep -n "g_gem_search\|g_open_gem" E:/CDToybox/src/render/equip_panel.cpp`
Expected: 출력 없음.

- [ ] **Step 8: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `374 tests, 0 failures`

- [ ] **Step 9: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/gem-picker-shared`

```
git -C E:/CDToybox add CMakeLists.txt src/render/gem_picker.h src/render/gem_picker.cpp src/render/equip_panel.cpp
git -C E:/CDToybox commit -m "리팩토링: 보석 고르기를 위젯으로 빼고 장비 창이 쓴다" -m "팝업·검색·목록이 render/gem_picker 한 곳으로. 장비 창은 순번을, 지급 창은 키를 쓰므로 둘을 함께 돌려준다. 상태는 창마다 하나 - 두 창이 같이 열려 있을 때 팝업이 엉뚱한 창에 뜨지 않게." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 8: 지급 창 적용 — 칸별 콤보를 버튼 + 팝업으로

**Files:**
- Modify: `src/render/grant_panel.cpp`

**Interfaces:**
- Consumes: Task 7 의 `GemPicker`·`GemChoice`·`GemPickerOpts`·`gem_picker_open`·`gem_picker_draw`; 기존 `g_socket_keys[game::kGiveMaxSockets]`, `entry_of`
- Produces: 없음 (호출부)

여기가 **화면이 바뀌는 자리**다. 칸마다 있던 드롭다운이 같은 폭(230)의 버튼이 되고, 누르면 팝업이 뜬다.

- [ ] **Step 1: include 와 static**

`#include "render/icon_atlas.h"` 줄 다음에 추가:

```cpp
#include "render/gem_picker.h"
```

`char g_gem_search[64]{};   // 보석 고르기 안의 이름 찾기` 한 줄을 이것으로 교체:

```cpp
GemPicker g_gem_picker;     // 보석 고르기 팝업 (장비 창과 같은 위젯)
int g_gem_slot = -1;        // 팝업이 고른 보석을 넣을 칸
```

- [ ] **Step 2: 칸 루프의 콤보를 버튼으로**

`draw_sockets` 안, `ImGui::SetNextItemWidth(230.0f);` 부터 `ImGui::EndCombo();` 다음의 닫는 `}` 까지(원본 138~173줄, `if (ImGui::BeginCombo("##gem", label)) {` 블록 전체)를 이것으로 교체:

```cpp
        // 누르면 팝업이 뜬다. 팝업은 칸 루프 밖(아래)에서 그린다 - 여기는
        // PushID(k) 안이라 여기서 열면 밖의 BeginPopup 이 못 찾는다.
        if (ImGui::Button(label, ImVec2(230.0f, 0.0f))) {
            g_gem_slot = k;
            gem_picker_open(&g_gem_picker);
        }
```

바로 뒤의 `ImGui::SameLine();` 과 `비우기` 버튼은 그대로 둔다.

- [ ] **Step 3: 루프 밖에서 팝업을 그린다**

`for (int k = 0; k < g_socket_open; ++k) { ... }` 루프의 닫는 `}` **바로 다음**, `ImGui::TextDisabled("보석을 안 고르면 빈 칸이 열린 채로 나옵니다.");` **앞**에 넣는다:

```cpp

    {
        GemPickerOpts o;
        o.allow_empty = true;
        o.selected_key =
            (g_gem_slot >= 0 && g_gem_slot < game::kGiveMaxSockets)
                ? g_socket_keys[g_gem_slot]
                : 0;
        GemChoice c;
        if (gem_picker_draw(&g_gem_picker, o, &c) && g_gem_slot >= 0 &&
            g_gem_slot < game::kGiveMaxSockets) {
            g_socket_keys[g_gem_slot] = (c.entry != nullptr) ? c.entry->key : 0;
        }
    }
```

- [ ] **Step 4: 남은 참조가 없는지 확인한다**

Run: `grep -n "g_gem_search\|BeginCombo\|EndCombo" E:/CDToybox/src/render/grant_panel.cpp`
Expected: 출력 없음. (다른 곳에 `BeginCombo` 가 있으면 그것은 보석과 무관하니 남긴다 — 이 파일에서 `##gem` 콤보만 사라지면 된다.)

- [ ] **Step 5: 빌드하고 테스트를 돌린다**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `374 tests, 0 failures`

- [ ] **Step 6: 커밋**

```
git -C E:/CDToybox branch --show-current
```
Expected: `feat/gem-picker-shared`

```
git -C E:/CDToybox add src/render/grant_panel.cpp
git -C E:/CDToybox commit -m "변경: 지급 창 소켓 칸의 콤보를 버튼 + 보석 고르기 팝업으로" -m "장비 창과 같은 팝업이다. 현재 값은 버튼 라벨에 그대로 보이고, 열리면 검색창에 포커스가 간다. 화면이 바뀌는 유일한 자리 - 게임에서 확인한다." -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

---

### Task 9: 2단계 머지 · 배포 · 화면 확인 · 현황 갱신

**Files:**
- Modify: `docs/STATUS.md` (5장 저장소 블록의 `src/render/` 와 `tests/` 두 줄)

- [ ] **Step 1: 브랜치와 작업트리를 확인한다**

```
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox status --porcelain
```
Expected: `feat/gem-picker-shared`, 빈 출력.

- [ ] **Step 2: 현황 문서를 맞춘다**

`docs/STATUS.md` 5장의 이 줄을:

```
src/render/   D3D12 훅 · 오버레이 · 진단 · 아이콘 아틀라스 · 등급 색
```

이것으로:

```
src/render/   D3D12 훅 · 오버레이 · 진단 · 아이콘 아틀라스 · 등급 색 ·
              공통 위젯(filter_bar 필터바 · gem_picker 보석 고르기)
```

그리고 이 줄을:

```
tests/        364개 (전부 통과 - `build\cdtb_tests.exe` 실측 2026-09-10)
```

이것으로:

```
tests/        374개 (전부 통과 - `build\cdtb_tests.exe` 실측 2026-09-10)
```

```
git -C E:/CDToybox add docs/STATUS.md
git -C E:/CDToybox commit -m "문서: 현황에 공통 위젯과 테스트 수를 반영한다" -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
```

- [ ] **Step 3: develop 으로 머지한다**

```
git -C E:/CDToybox checkout develop
git -C E:/CDToybox branch --show-current
git -C E:/CDToybox merge --no-ff feat/gem-picker-shared -m "머지: 보석 고르기 통일 (2026-09-10)" -m "Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
git -C E:/CDToybox log --oneline -6
```
Expected: `develop`; 머지 커밋 아래 Task 6~9 커밋 4개.

- [ ] **Step 4: 빌드 · 테스트 · 배포**

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/build.ps1`
Expected: `build ok`

Run: `E:/CDToybox/build/cdtb_tests.exe`
Expected: `374 tests, 0 failures`

Run: `powershell -NoProfile -ExecutionPolicy Bypass -File E:/CDToybox/scripts/deploy.ps1`
Expected: `deployed -> ...xinput1_4.dll`. 게임이 켜져 있으면 거부한다.

- [ ] **Step 5: 멈추고 사람에게 넘긴다**

사용자에게 이렇게 보고한다:

> 2단계 배포 완료. 이번엔 화면이 바뀌었으니 제대로 봐 주세요. 셋입니다:
> 1. **지급 창** — 소켓 절에서 칸의 버튼을 누르면 팝업이 뜨고, 보석을 고르면 버튼 라벨이 그 이름으로 바뀌고, 지급하면 그 보석이 실리는가. "(빈 칸으로 열기)" 를 고르면 빈 칸으로 열리는가.
> 2. **장비 창** — 빈 소켓의 `채우기` 로 보석을 박는 것이 전과 같이 되는가.
> 3. **둘 다** — 팝업이 열리면 바로 타자를 칠 수 있고, 검색이 목록을 거르는가.

문제가 있으면 무엇이 어떻게인지 받아 적는다. 2단계만 되돌리려면 `git -C E:/CDToybox revert -m 1 <머지 커밋>` 으로 머지를 되돌리면 된다 — 1단계는 그대로 남는다.

---

## 자체 검토 기록

- **스펙 3.1~3.5 (1단계):** `match_key`·`passes`·`make_filter` → Task 1·2. `FilterBar`·`FilterBarOpts`·`draw_filter_bar`·`to_filter` → Task 3. 두 호출부 → Task 3·4. CMake `cdtoybox` 만 → Task 3 Step 3. 테스트 목록 다섯 항목 → Task 1 Step 2 (passes 넷), Task 2 Step 1 (make_filter 다섯). 검색 버퍼 128·`hide_unnamed` 초기값·`match_key=false` → Task 3 Step 5, Task 4 Step 5.
- **스펙 4.1~4.4 (2단계):** `GemChoice`·`GemPickerOpts` → Task 7 Step 1. 순번/키 둘 다 → `GemChoice::index`·`entry->key`. `allow_empty`·`selected_key`·자동 포커스·400 상한 → Task 7 Step 2. `is_socket_gem` → Task 6. 두 호출부 → Task 7·8. 스펙과 다른 점 하나(상태를 창마다) 는 Task 7 머리에 이유를 적었다.
- **스펙 5·6 (검증·브랜치):** 빌드→테스트→배포→화면 순서와 사람 게이트 → Task 5·9. 브랜치 이름과 커밋 전 확인 → 모든 커밋 스텝.
- **타입 일관성:** `passes(const ItemFilter&, std::string_view, int, int, std::uint32_t)` 를 Task 1 정의·Task 4 호출에서 같게 썼다. `make_filter` 다섯 인자 순서가 Task 2 선언·구현·Task 3 `to_filter` 에서 같다. `gem_picker_open(GemPicker*)`·`gem_picker_draw(GemPicker*, const GemPickerOpts&, GemChoice*)` 가 Task 7 헤더·구현·equip 호출·Task 8 grant 호출에서 같다.
- **테스트 수:** 364 → 368 (Task 1) → 373 (Task 2) → 374 (Task 6). Task 9 가 STATUS.md 를 374 로 맞춘다.
