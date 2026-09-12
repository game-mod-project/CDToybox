# 죽은 코드 정리 — 조사와 결과 (2026-09-10)

워크스트림 B. 참조 수 스캔으로 후보를 뽑고 A·C·D 를 지웠다. `develop@052e275`,
브랜치 `chore/dead-code`(커밋 셋: `884ed08` C · `47fc1b0` A · `93a8720` D). 27파일,
**+33 / −574.** 테스트 387/0 그대로, 화면 불변, 로그만 조용해졌다.

## 1. 어떻게 찾았나

`src/`·`tools/`·`tests/` 의 정의(컬럼 0 에서 시작하는 함수·`g_*`·`k*` 전역) 1,339개를 뽑아
정의·선언·주석 밖의 단어 경계 참조를 셌다. 참조 0 이 48개(거짓 양성 1: `kPerPage`).
컴파일러는 도움이 안 된다 — 이 프로젝트는 기본 경고 수준이라 C4505 가 안 나온다.

**스캔의 한계(실측).** 훅(디투어)이 채우는 전역 상태를 누가 읽는지는 스캔이 못 따라간다.
"조사용" 이라 이름 붙은 훅 다섯 중 둘이 생산 배관이었다:
- 캡처 하위시스템(`companion_capture_install`, 11 디투어): `det_hire_response` → `g_acks` →
  clan.cpp `tick_hire_cleanup → resolve_spawn_flag`(**게임 메모리 쓰기**, "획득 직후 소환
  먹통 수정"의 유일한 생산자). `det_catch_summon` → `g_last_catch` → `request_catch`.
- `det_spawn_work` → `g_last_spawn` → `last_spawn_work()` → roster_panel 이 소환 거부 문구 표시.

구현자가 안전 규칙("디투어 본문이 읽기·로그·자기 상태 저장만이면 지우고, 아니면 멈춤")
대로 멈춰서 잡았다. 둘 다 남겼고 companion.h 에 생산 배관이라는 주석을 달았다.

## 2. 지운 것

**C — 조사가 끝난 계측 훅 셋** (매 실행 게임 함수에 디투어를 걸고 로그만 남기던 것):
부적 등록(hire_inv) 추적 · 인벤 레코드 `+0x08` 표 프로브(09-04 에 해결된 질문) ·
바닥 떨구기 추적과 그것만 쓰던 `g_spawn`·`spawn_resolve`·`SpawnFn`·`call_spawn_guarded`.

**A — 참조 0 심볼** (연쇄 포함):
- 빈 스텁 `draw_scan_panel`; 아무도 안 부르는 해제 `remove_hooks`·`unload_original`·
  `freecam_uninstall`·`actor_hook_remove`·`tick_hook_remove`(DLL 은 언로드가 안 된다)
- 소비자 없는 상태 조회·접근자 `companion_capture_installed/count`·`companion_hire_trace_installed`·
  `companion_spawn_trace_installed`·`spawnguard_installed`·`last_hire_target`(+`HireTargetCapture`·
  `g_last_hire`)·`last_hire_ack`·`last_hire_species`·`seen_actors`
- grant 잔재 `note_task`·`g_seen_class`·`g_seen_server`; `socket_unlocked`; `companion_group_name`
- **equip 의 MGRCHAIN 경로** `resolve_equip_globals`·`equip_player_actor`·`equip_component`·
  `EquipGlobals` — 한 번도 안 탔다. 실제 탐색은 힙 스캔(`collect_equip_tables` +
  `find_equip_table` + `pick_player_table`). equip.h 헤더와 STATUS §1.12 가 이 경로를
  설명하고 있어 같이 고쳤다(D).
- 상수 `kLocFieldName`·`kRecSocketCap`·`kCur`, camera.h 의 실측 오프셋 11개(값은 STATUS §2.4 로
  옮김). `kSocketLocked` 는 지우는 대신 equip.cpp 의 리터럴 `0xFF` 두 곳을 상수로 바꿨다.

**D — 낡은 말**: equip.h 헤더, STATUS §1.12, stash.h("입고는 아직 못 한다" → 된다),
items.h("분류 이름은 아직 못 붙였다" → `category_name`), grant_panel 의 연마 "아직 못 봤다".

## 3. 일부러 남긴 것 (B — 삭제는 제품 결정)

| 것 | 왜 |
|---|---|
| `freecam.cpp` 467줄 (`kDeferred`) | STATUS §3 "보류" — 다음에 볼 곳(렌더 뷰 객체)이 남아 있다 |
| pump/taskrun 계측 훅 6심볼 (grant.cpp) | camera.cpp:217 "코드는 남겨 두되 설치하지 않는다" |
| `request_endurance` (내구도 치트 2736) | STATUS F5 "치트 자체는 남겨 두었다" |
| `cdtoybox_cmd.txt` 명령 파일 하위시스템 | STATUS 가 운영 도구로 씀(`sessions`, 게이트 회수). 테스트 6개 |
| `find_spawn_ground_rva`·`kSpawnGroundPattern`·`find_one` | 프로덕션 호출자는 없지만 `tests/grant_tests.cpp` 가 쓴다 — 다음 스캔이 다시 집을 것 |
| watchpoint | analysis·dllmain 이 씀, 테스트 14개 |

## 4. 이월 (검토자 Minor 6 + 구현자 우려)

1. `grant.cpp:836` 의 고아 주석("캐릭터 소환 크래시 시점의 호출 스택…") — 이미 없어진 것을 설명
2. `g_last_hs`/`g_hs_mutex` — `last_hire_species` 가 사라져 쓰기 전용이 됨(락을 잡고 아무도 안 읽음)
3. `find_spawn_ground_rva` 에 "테스트 전용" 표시 없음
4. `equip.h:77` `read_player_worn` 주석 "가장 큰 테이블 = 플레이어" — 실제는 정신력 풀 게이트
5. `equip.cpp` 가 상수 하나 때문에 `inventory.h` 전체를 끌어옴 — `kSocketLocked` 를 items.h 로 옮기면 닫힘
6. STATUS §1.12 "한 번 고른 것을 유지한다" — 실제 `prefer` 는 동률만 깬다

전부 주석·문서·한 줄짜리라 한 커밋으로 묶을 수 있다. → 같은 날 `68de258` 로 처리했다
(검토자 Minor 4 - 빈 줄 둘·inventory.h 주석·중복 include - 도 뒤이어 정리).

## 5. 되풀이하지 말 것

- **이름과 접근자 참조 수로 훅을 죽었다고 판정하지 말 것.** 디투어가 저장하는 상태 → 그 상태를
  읽는 함수 → 그 함수의 소비자(UI 표시·게임 메모리 쓰기)까지 사슬을 끝까지 따라간다.
- 삭제 브리프에는 안전 규칙(읽기·로그만이면 지우고 아니면 멈춤)과 연쇄 규칙(테스트가 쓰는
  순수 함수는 남김)을 반드시 넣는다. 이번엔 그 두 규칙이 회귀 하나와 테스트 파손 하나를 막았다.
