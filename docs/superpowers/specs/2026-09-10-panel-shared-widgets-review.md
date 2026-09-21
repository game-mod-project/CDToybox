# 패널 공통화 — 검토 기록 (2026-09-10)

> **정리 (2026-09-21) — ✅ 완료.** Critical/Important 없이 머지됐다
> (`plans/2026-09-10-panel-shared-widgets.md`). §4 화면 확인 목록과 취향 질문의 결과는
> 이후 문서에 기록이 없다.

대상: `626b919`(계획 직전 develop) → `706f353`(2단계 브랜치 끝). 설계는
`2026-09-10-panel-shared-widgets-design.md`, 계획은
`../plans/2026-09-10-panel-shared-widgets.md`.

구현은 워크트리 `E:/CDToybox-sdd` 에서 태스크마다 새 서브에이전트가 했고,
태스크마다 별도 검토자가 봤다. 끝에 전체 브랜치 내부 검토와 외부(Codex)
검토를 같은 diff 에 대해 돌렸다. **어느 검토도 Critical/Important 를 내지
않았다.** 아래는 남은 Minor 와 그 판정이다.

## 1. 태스크별 검토 (8건, 전부 Approved)

| 태스크 | 커밋 | 검토 | 이월 Minor |
|---|---|---|---|
| 1 `passes` 술어 | 61ea7db | ✅ | 교차검증이 match_key=false 갈래를 안 돎(전용 테스트 3개가 덮음) |
| 2 `make_filter` | 8981cc0 | ✅ | 헤더 주석 시점, `assign(data,size)` 빈 string_view UB(도달 불가), 기본값만 단언하는 테스트 1개 |
| 3 filter_bar + 아이템 목록 | 0331def | ✅ | `check_w` 무조건 계산, `<string>` include 잔존, `labeled_w` 는 base 부터 죽은 코드 |
| 4 인벤토리 적용 | 22991e1 | ✅ | `to_filter` 가 매 프레임 string 생성(SSO), BeginTable 실패 시에도 filter 생성 |
| 6 `is_socket_gem` | e095b80 | ✅ | 테스트의 `56` 리터럴, 주석의 `74` 반복 |
| 7 gem_picker + 장비 창 | c9c88d6 | ✅ | `selected_key==0` 겸용, snprintf 여분 인자(옛 코드 그대로), 빈 상태 문구 2개 신설 |
| 8 지급 창 적용 | 6d8b9cb | ✅ | include 순서, 슬롯 가드 `kGiveMaxSockets`(범위 안), 라벨=ID, 헤더 접으면 팝업 닫힘(옛 콤보와 동일) |

**계획과 다르게 한 것(판정 기록):**
- `FilterBar g_bar{.hide_unnamed = true}` 는 MSVC 19.44 에서 **C2440** — `char query[128] = ""`
  기본 멤버 초기화자가 있는 집합체의 지정 초기화 결함(구현자 최소 재현, 컨트롤러 재현).
  람다 초기화로 대체. 값은 같다.
- `GemPicker` 상태는 전역 static 하나가 아니라 **창마다 하나**. 두 창이 동시에 열려 있으면
  열림 요청을 다른 창의 그리기가 먼저 소비한다.
- 테스트 기준선은 계획의 364 가 아니라 375(그날 아침 다른 세션이 grant 테스트 11개 추가).
  최종 **385**.

## 2. 전체 브랜치 내부 검토 (opus) — Ready to merge: Yes

Minor 6건, 전부 비차단:

1. 장비 팝업이 옛 지급 콤보의 동작 셋을 얻었다 — 열릴 때 검색창 포커스, 빈 상태 문구 2개,
   400 상한. 포커스 때문에 **Esc 를 두 번** 눌러야 닫힌다(첫 Esc 는 입력 비활성). → 화면
   확인 목록에 넣음. 원치 않으면 `GemPickerOpts::focus_search` 로 갈라 쓴다.
2. 인벤의 `match_key = false` 가 힌트 문구("이름으로 검색")와 떨어져 있다 → `FilterBarOpts`
   에 `match_key` 를 두면 한 곳이 된다. 후속 정리 후보.
3. `build_category_labels(&g_bar.categories, &g_bar.category_labels)` 를 호출부가 기억해야
   한다 → `filter_bar_rebuild_categories(FilterBar*)` 로 감싸면 빠뜨렸을 때 눈에 띈다. 후속.
4. `f.query.assign(query.data(), query.size())` → `assign(query)` 한 단어. 후속.
5. `item_panel.cpp` 의 `#include <string>` 과 `labeled_w` 죽은 코드. 후속(정리 사이클).
6. `gem_picker.cpp` 의 `if (!chosen)` 은 고른 프레임에 목록을 안 그린다 — 의도(같은 프레임
   이중 선택 방지). "고치지" 말 것.

권고: `item_view_tests` 에 두 테스트 추가(hide_unnamed+match_key+키 매칭 → 제외되어야
함; 음수 색인 → 전체). `roster_panel` 의 검색은 의미가 달라 일부러 안 합쳤다.

## 3. 외부 검토 (Codex, gpt-5.6-sol, 추론 xhigh, 읽기 전용 샌드박스)

두 번 돌렸다.

- `codex review --base review-base` (내장 리뷰): 도구 호출 23번. **"필터링·소켓 보석
  동작이 호출부마다 보존됨. 실행 가능한 정확성 결함 없음."** 지적 0건.
- `codex exec` 한국어 초점 지시(1단계 회귀 · 2단계 ID 범위/상태/칸/포인터 · 테스트 ·
  중복): 도구 호출 51번 이상, 테스트 바이너리도 직접 실행. **Critical/Important 0,
  Minor 4, "머지 가능 — 게임 내 smoke test 권고".** ID 범위·창별 상태·잘못된 칸 쓰기
  없음·`GemChoice::entry` 수명(카탈로그 옛 판이 `items.cpp:211` 에 보존됨)·중복 제거를
  확인했다.

| Codex Minor | 판정 |
|---|---|
| 인벤 검색 버퍼 64→128, "완전 불변" 과 엄밀히 충돌 | 스펙 3.3 이 명시한 승인 결정. 유지 |
| `gem_picker_open` 이 검색어를 항상 지움 — 옛 지급 콤보는 유지했음 | 스펙이 장비 팝업 방식(열 때 지움)으로 통일. UX 취향이라 **화면 확인 때 사용자에게 묻는다** |
| `passes_agrees_with_filter_items` 는 순환 검증 | 전용 테스트 3개 + 기존 19개가 실질 검증. 불변식으로 유지 |
| `labeled_w`·`g_containers` 죽은 코드 | 이 브랜치가 만든 것이 아님. 정리 사이클 |

Gemini CLI(0.55.1)는 개인 무료 티어가 종료돼(`IneligibleTierError`, Antigravity 이전
안내) API 키 없이는 돌릴 수 없었다.

## 4. 화면 확인 목록

**1단계 (배포됨, cc609db)** — "이전과 똑같은가":
- 아이템 목록: 검색·지우기·이름 없는 것 감추기(켜진 채 시작)·등급·분류. 숫자(키)로도 걸림.
- 인벤토리: 검색·지우기·등급·분류. **숫자를 치면 아무것도 안 걸림**(이름만). 표 읽기 전엔
  분류 콤보 없음.
- 둘 다: 창을 좁혔을 때 필터 줄 줄바꿈.

**2단계 (머지·배포 후)**:
- 지급 창: 소켓 칸의 **버튼**(폭 230, 라벨 가운데 정렬·▼ 없음) → 팝업 → 고르면 라벨이
  바뀌고 지급 시 실림. "(빈 칸으로 열기)" 도. 칸 5개 중 아무 칸이나 눌러도 **그 칸에만**.
- 장비 창: 빈 소켓 `채우기` → 박기 전과 같음. **Esc 두 번**에 닫히는 것이 괜찮은가.
- 둘 다: 팝업 열리면 바로 타자 가능, 검색이 목록을 거름, **두 창을 동시에 열고** 각각
  눌렀을 때 누른 창에 팝업이 뜸.
- 취향 질문: 팝업을 다시 열 때 검색어가 지워지는 것(현재) vs 남는 것(옛 지급 콤보).
