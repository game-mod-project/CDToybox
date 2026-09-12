# 패널 공통화 설계 — 필터바·필터 술어·보석 선택기 (2026-09-10)

오버레이 패널 8개가 같은 일을 각자 하고 있다. 이 문서는 **무엇이 진짜
중복이고 무엇이 아닌지를 측정으로 가른 뒤**, 겹치는 것만 걷어내는 설계다.

리팩토링이므로 **화면이 달라지면 안 된다** — 딱 한 곳, 보석 고르기만
의도적으로 바꾼다. 그래서 두 단계로 나눈다.

| 단계 | 무엇 | 화면 | 검증 |
|---|---|---|---|
| 1 | 필터바 위젯 + 필터 술어 | **불변** | 테스트 + "달라진 게 없는가" 한 번 |
| 2 | 보석 선택기 통일 | **바뀜** | 게임에서 제대로 확인 |

---

## 1. 측정된 중복

파일을 세어 확인한 것만 적는다.

| | 어디 | 무엇이 겹치나 |
|---|---|---|
| C1 | `item_panel` ↔ `inventory_panel` | 필터바 상태(아이템 6종 · 인벤 5종)와 그리기(폭 상한 420/140, 라벨 앞배치, 지우기 버튼). 주석까지 거의 같다 |
| C2 | `item_view`(테스트 19개) ↔ `inventory_panel` 손코딩 루프 | 등급·분류·이름 비교. 인벤 쪽은 테스트가 없다 |
| C3 | `item_panel:63` ↔ `inventory_panel:581` | 콤보 인덱스를 필터 값으로 옮기는 off-by-one (`grade_idx-1`, 범위 밖 검사, `categories[idx-1]`). **글자 그대로 복제** |
| C4 | `equip_panel:49` ↔ `grant_panel:142` | 보석 목록 거르기(분류 74 + 이름 있음 + 부분일치). static 버퍼 이름까지 `g_gem_search` 로 같다 |

작성자가 이미 알고 있었다. `inventory_panel.cpp:64` 에 이렇게 적혀 있다.

```
// 걸러 내기는 아이템 목록과 같은 모양이다 - 검색 · 등급 · 분류.
// 헬퍼는 item_style 에 함께 둔다. 한쪽만 고치면 두 창이 달라진다.
```

`grant_panel.cpp:131` 에도 있다 — "목록에서 고르게 한다(장비 소켓 창의
보석 고르기와 같은 방식)". 같은 것을 의도했는데 구현이 갈렸다.

## 2. 공통화하지 **않는** 것

억지로 합칠 자리를 미리 잘라 둔다.

- **정렬.** 두 패널의 정렬 기준이 완전히 다르다. `item_panel` 은
  `ItemSort` 4종으로 미리 만든 뷰를 정렬하고, `inventory_panel` 은
  8열(이름·분류·개수·담금질·내구도·연마)로 `g_rows` 를 `stable_sort`
  한다. 공통 분모가 없다.
- **`roster_panel` 의 검색 2종**(종 고르기 `:459`, 로스터 `:1042`).
  대상 타입이 `RosterEntry` 이고 조건이 도메인 특화다(`listable()`,
  `is_listed_companion_row`, `merc_row` 일치).
- **`stash_panel:298`.** 검색이 아니라 새 세트 **이름 입력** 이다.
- **인벤토리의 뷰 만들기.** 인벤은 지금 *그리는 루프 안에서* 거른다.
  미리 뷰를 만드는 방식으로 바꾸면 정렬 대상이 바뀌어 화면 동작이
  달라진다. 1단계의 전제와 충돌하므로 하지 않는다.

## 3. 1단계 — 필터바와 필터 술어

### 3.1 `src/game/item_view.{h,cpp}` (확장)

ImGui 를 모르는 계층이라 테스트가 붙는다. 여기에 셋을 더한다.

```cpp
struct ItemFilter {
    std::string query;
    bool hide_unnamed = false;
    int grade = -1;
    int category = -1;
    bool match_key = true;      // 새로 추가
};

// 한 항목이 필터를 통과하는가. filter_items 가 이것을 부른다.
bool passes(const ItemFilter& f, std::string_view name, int grade,
            int category, std::uint32_t key);

// 콤보 인덱스를 필터로 옮긴다(C3 이 여기 한 곳으로).
ItemFilter make_filter(std::string_view query, int grade_idx,
                       int category_idx, bool hide_unnamed,
                       const std::vector<std::uint8_t>& categories);
```

`filter_items` 는 안쪽만 `passes` 를 부르도록 바꾼다. **공개 동작은
그대로다** — 기존 테스트 19개가 그것을 지킨다.

### 3.2 `match_key` 가 필요한 이유 (동작 보존)

`matches()` 는 이름뿐 아니라 **키를 문자열로 바꿔서도** 찾는다.
`item_panel` 힌트가 "이름 또는 키로 검색" 인 이유다. 그런데
`inventory_panel:624` 는 이름만 본다.

술어를 그냥 공유하면 인벤에서 숫자를 쳤을 때 키가 걸린다 — **화면
동작이 변한다.** 그래서 차이를 플래그로 드러내고 1단계에서는 인벤을
`match_key = false` 로 둔다.

켜는 것은 한 줄이다. 켤지 말지는 힌트 문구("이름으로 검색")를 함께
고쳐야 하는 문제라 2단계나 UI/UX 사이클에서 결정한다.

### 3.3 `src/render/filter_bar.{h,cpp}` (신규)

```cpp
struct FilterBar {
    // 128 이다. 아이템 목록이 128, 인벤이 64 를 쓰고 있었다. 64 로
    // 맞추면 아이템 목록에서 긴 검색어가 잘린다 - 넓은 쪽을 쓴다.
    char query[128] = "";
    int grade_idx = 0;
    int category_idx = 0;
    // 기본값이 창마다 다르다. 아이템 목록은 켜져 있고(g_hide_unnamed
    // = true) 인벤은 이 칸이 아예 없었다. 부르는 쪽이 초기화한다.
    bool hide_unnamed = false;
    std::vector<std::uint8_t> categories;   // 표에 실제로 있는 분류 값
    std::string category_labels;            // Combo 용 널 구분 문자열
};

struct FilterBarOpts {
    const char* id;                  // "##query" 충돌을 막는다
    const char* hint;                // 창마다 다르다
    bool show_hide_unnamed = false;  // 아이템 목록만 쓴다
};

// 값이 바뀌었으면 true. 부르는 쪽이 다시 그릴지 정한다.
bool draw_filter_bar(FilterBar* s, const FilterBarOpts& o);

// FilterBar -> ItemFilter. make_filter 로 위임한다.
game::ItemFilter to_filter(const FilterBar& s);
```

폭 계산·라벨 앞배치·지우기 버튼이 여기 하나로 모인다. 지금 패널마다
흩어진 static 6개가 `FilterBar` 인스턴스 하나로 바뀐다.

`draw_filter_bar` 가 `bool` 을 돌려주는 이유는 두 패널이 변경에 서로
다르게 반응하기 때문이다. `item_panel` 은 `g_dirty = true; g_page = 0`
이 필요하고, `inventory_panel` 은 매 프레임 거르므로 아무것도 안 한다.
**무엇을 할지는 부르는 쪽 일이다.**

### 3.4 호출부

| 파일 | 무엇이 바뀌나 |
|---|---|
| `item_panel.cpp` | static 6개 → `FilterBar` 하나. **`hide_unnamed = true` 로 초기화**(지금 기본값). `rebuild()` 가 `to_filter()` 를 쓴다. 반환이 참이면 `g_dirty`·`g_page` 리셋 |
| `inventory_panel.cpp` | static 5개 → `FilterBar` 하나. 그리는 루프의 비교 3줄이 `passes()` 한 줄로. `match_key = false`, `show_hide_unnamed = false` |
| `CMakeLists.txt` | `src/render/filter_bar.cpp` 를 `cdtoybox` 에**만** 추가 |

**기본값 세 개를 놓치면 화면이 조용히 달라진다.** 검색 버퍼 크기(128 대
64), `hide_unnamed` 초기값(아이템 목록은 켜져 있다), 인벤의 키 매칭
여부. 셋 다 위에 못박아 두었다.

`cdtb_tests` 와 `cdtb_probe` 에는 넣지 않는다. `render/` 는 ImGui 를
끌고 오는데 두 타깃 모두 ImGui 와 링크하지 않는다.

### 3.5 테스트

`tests/item_view_tests.cpp` 에 더한다. **기존 19개는 손대지 않는다** —
그대로 통과하는 것이 `filter_items` 동작 불변의 증거다.

- `passes` 가 `filter_items` 와 같은 판정을 낸다(같은 입력, 같은 결과)
- `match_key = false` 면 키 문자열이 안 걸린다
- `match_key = true` 면 걸린다
- `make_filter` 경계: idx 0 = 전체 / idx 1 = 등급 0 / 범위를 넘는 idx =
  전체(지금 두 패널이 하는 처리)
- `make_filter` 가 빈 `categories` 를 받아도 분류를 -1 로 둔다

## 4. 2단계 — 보석 선택기

### 4.1 두 호출자가 원하는 값이 다르다

- `equip_panel` 은 카탈로그 **순번** 을 쓴다 —
  `eq_write_socket(reader, inst, k, static_cast<uint16_t>(i))`
- `grant_panel` 은 아이템 **키** 를 쓴다 — `g_socket_keys[k] = e.key`

같은 카탈로그를 보므로 둘을 함께 돌려주는 데 비용이 없다.

### 4.2 `src/render/gem_picker.{h,cpp}` (신규)

```cpp
struct GemChoice {
    const game::ItemCatalogEntry* entry = nullptr;
    std::size_t index = 0;
};

struct GemPickerOpts {
    const char* title;                // 팝업 위 설명 줄
    bool allow_empty = false;         // "(빈 칸으로 열기)" - grant 만
    std::uint32_t selected_key = 0;   // 현재 값 강조 - grant 콤보가 하던 것
};

void open_gem_picker();                                       // 이번 프레임에 연다
bool draw_gem_picker(const GemPickerOpts& o, GemChoice* out);  // 골랐으면 true
```

검색 버퍼와 열림 플래그는 `gem_picker.cpp` 안 static 하나로 충분하다.
팝업은 한 번에 하나만 열리고, `equip_panel` 이 이미 그 구조다.

**대상을 기억하는 일은 각 패널에 남긴다.** `equip_panel` 의
`g_gem_inst`/`g_gem_k`, `grant_panel` 의 슬롯 번호는 선택기의 관심사가
아니다. 선택기는 "무엇을 골랐나" 만 답한다.

### 4.3 화면 변화 (이번 단계의 확인 대상)

- **grant 창**: 칸마다 있던 드롭다운이 **버튼** 이 된다. 현재 값은 버튼
  라벨에 그대로 보이므로 정보는 줄지 않는다. 라벨 규칙은 지금 콤보가
  쓰던 것 그대로 — `(빈 칸으로 열기)` / 보석 이름 / `(표에 없는 키)`
- **equip 창**: 보이는 것은 그대로다(팝업 방식 유지)
- **양쪽 공통**: 팝업이 열릴 때 검색창에 자동 포커스가 간다. 지금은
  grant 콤보만 하던 것을 둘 다 갖는다
- `shown >= 400` 상한은 grant 에만 있었다. 보석이 190종이라 걸린 적이
  없지만 선택기에 그대로 둔다

### 4.4 테스트

선택기 자체는 ImGui 라 `cdtb_tests` 가 못 본다. 대신 두 곳에 복제된
거르는 조건을 `game/items.h` 로 뺀다.

```cpp
// 소켓에 박을 수 있는 강화 보석인가 (분류 74 + 이름이 풀린 것).
bool is_socket_gem(const ItemCatalogEntry& e);
```

`tests/items_tests.cpp` 에서 분류가 다를 때·이름이 빌 때를 덮는다.

## 5. 검증 절차

두 단계 모두 같은 순서다.

```
1. scripts\build.ps1
2. build\cdtb_tests.exe        전부 통과할 것 (지금 364개)
3. scripts\deploy.ps1          게임이 실행 중이면 거부한다
4. 게임에서 확인
```

4번에서 볼 것:

| 단계 | 볼 것 |
|---|---|
| 1 | 아이템 목록·인벤토리 두 창이 **이전과 똑같이** 보이는가. 검색·등급·분류가 전과 같이 거르는가 |
| 2 | grant 창에서 보석을 골라 지급하면 값이 실리는가. equip 창에서 보석 박기가 되는가. 두 팝업 모두 검색이 도는가 |

화면 확인은 사람이 한다. 모드는 화면을 볼 수 없다.

## 6. 브랜치와 되돌리기

- 1단계 `feat/filter-bar-shared`, 2단계 `feat/gem-picker-shared` 로
  나눈다. 2단계 화면이 별로면 그 커밋만 되돌리면 된다.
- `develop` 에서 분기하고 `develop` 으로 머지한다. `main` 은 릴리스만.
- **커밋 직전마다 `git branch --show-current` 로 확인한다.** 이 저장소는
  워크트리가 하나인데 세션이 동시에 붙는다. 2026-09-10 에 브랜치를 만든
  직후 다른 세션이 체크아웃해 커밋이 남의 브랜치로 간 일이 있었다.

## 7. 이번에 하지 않고 미루는 것

- **인벤토리의 "걸러진 개수" 표시.** 지금 인벤은 몇 개가 걸러졌는지
  모른다. 뷰를 미리 만들어야 알 수 있는데 그건 정렬 동작을 건드린다.
  UI/UX 사이클로 미룬다.
- **인벤 검색에 키 매칭 켜기.** `match_key = true` 한 줄이지만 힌트
  문구와 함께 결정할 일이다.
- **`roster_panel` 검색 2종.** 도메인 특화라 합칠 것이 아니다.
- **죽은 코드 정리**(`draw_scan_panel` 빈 스텁, `freecam` 비활성 467줄),
  **거대 모듈 분해**(`grant.cpp` 2313줄, `companion.cpp` 1241줄),
  **UI/UX 리뷰**(창 8개). 각각 별도 사이클이다.
