# 스토리 탈것(드래곤·ATAG) 복제본 등록·소환 — 설계 (2026-09-11)

브레인스토밍 결과. 사용자 결정: 접근 **A(명부 항목 복제 + 종 교체)**,
편집 범위 **종 + 이름 + 능력치**, 생성 방식 **오버레이 한 버튼 자동**,
UI 는 **로스터 창의 새 탭**. 이 문서는 그중 첫 스펙(0단계 스파이크 +
1단계 복제 버튼)만 다룬다. 이름·능력치 편집은 §8 의 후속 스펙이다.

## 1. 목표 · 비목표

**목표.** 스토리로만 얻는 두 탈것(드래곤 `Vehicle_Dragon`, ATAG
`Vehicle_WarMachine`·`Vehicle_WarMachine_Raptor`)의 **복제본**을 원본과
별개의 명부 항목으로 만들고, 게임 안에서 실제로 소환·탑승·저장되게
한다. 원본 스토리 개체는 어느 단계에서도 읽지도 쓰지도 않는다.

**비목표.**
- 캐릭터 표(정적 표) 행 복제. 종에 묶인 기본 능력치(속도·체력 기본값)는
  캐릭터 표에 살아 원본과 공유된다. 이것을 따로 주려면 접근 B 가
  필요하고, 이 스펙은 다루지 않는다.
- 이름·레벨·경험치 편집(후속 스펙 §8).
- 명부에 레코드를 직접 끼워 넣는 것. 검증된 길이 없다.
- 소환 치트(2988)·`GameData_SummonCharacter` 경유. 둘 다 이미 폐기됐다
  (`2026-09-08-companion-register-session.md` §1·§4).

## 2. 근거 사실 (저장소가 이미 아는 것)

| 사실 | 출처 |
|---|---|
| 드래곤은 캐릭터 표에 1행, ATAG 는 WarMachine 2행 + Raptor 1행. 포획 대상 없음 | `2026-09-05-catchable-companions.md` |
| 명부 레코드는 `+0x20` 에 캐릭터 표 행(종)만 든다. 탭·타입은 그 종의 `CharacterInfo+0xBE` 에서 파생 | `2026-09-10-companion-species-swap-session.md` §5 |
| 종 교체(`apply_species`)는 클라·서버 양쪽 제자리 쓰기로 소환·탑승·리로드까지 검증됨. 타입을 넘는 교체(펫→특수탑승물)도 됨 | 같은 문서 §1 |
| **드래곤·ATAG 행으로의 교체는 미검증** | `2026-09-08-companion-catalog-and-acquire-research.md` §6.10 |
| 새 명부 항목을 만드는 검증된 길: 부적 지급→사용, 근처 획득(2338) | `companion.h` |
| 부적 사용은 2454(`request_hire_from_inventory(A, B)`)로 구동할 수 있다. A 컨테이너 종류, B 슬롯 | `companion.h`, `2026-09-07-companion-add-session.md` §3 |
| 등록 검사 함수(`request_hire_species`)가 타입별 한도를 대조한다. 명부는 안 바꾼다 | `grant.h` |
| 소환 널 가드(`spawnguard.cpp`)가 없으면 종을 바꾼 개체 소환에서 죽는다 | 같은 세션 문서 §2 |
| 명부 주소는 휘발성 - 쓰기 직전에 번호로 다시 찾는다 | `clan.h` |
| 세이브 데이터 `MercenarySaveData` 에 `staticstringA`(이름)·`ExperienceLevelSaveData` 가 있다 | 연구 문서 §4.3 |

## 3. 0단계 — 스파이크 (새 코드 없음, 버릴 세이브)

**질문.** 드래곤·ATAG 종으로 바꾼 **두 번째** 개체를 게임이 목록에
보이고 소환·탑승시켜 주는가.

**절차.** 기존 오버레이만 쓴다.

1. 버릴 세이브를 만든다. 크래시 로그 `bin64/CDToybox.crash.txt` 를 비운다.
2. 동반자 아이템 탭에서 부적(혹멧돼지 1003847)을 지급받아 인벤에서
   쓴다. 새 항목(혹멧돼지)이 내 동반자 탭에 뜬다.
3. 그 항목의 [바꾸기] 에서 "같은 타입만" 을 풀고, 아래 네 행을
   차례로 적용한다. 행 번호는 캐릭터 탭에서 내부 이름으로 찾는다.
   - `Vehicle_Dragon` 타입행의 1행
   - `Vehicle_WarMachine` 타입행의 2행
   - `Vehicle_WarMachine_Raptor` 타입행의 1행
4. 행마다 기록한다.

| 항목 | 확인 방법 |
|---|---|
| 게임 탈것 목록·드래곤 호출 UI 에 두 번째 개체가 보이는가 | 게임 UI |
| 게임 자체 UI 로 소환되는가 | 화면 + `CDToybox.log` 의 "소환 작업 -> 코드" + 크래시 로그 |
| 탑승·이동이 되는가 | 화면 |
| 저장·리로드 뒤 남는가 | 리로드 후 내 동반자 탭 |
| 원본 스토리 개체가 그대로 동작하는가 | 원본 호출·탑승 |

5. 덤으로 명령 파일 또는 probe 로 `request_hire_species(행)` 을 찍어
   등록 검사 코드를 본다. 0 이 아니면 타입별 한도가 두 번째 개체를
   거부한다는 뜻이고, 종 교체가 그 검사를 우회하는지가 4 의 결과다.

**판정.** 소환·탑승이 **한 행이라도** 되면 1단계로 간다. 되는 행만
콤보에 "검증됨" 으로 올리고 나머지는 "미검증" 으로 표기한다. 전부 안
되면 접근 A 를 폐기하고 접근 B(캐릭터 표 행 복제)를 새로 설계한다.

**기록.** 결과는 이 문서 §9 에 표로 남긴다.

## 4. 1단계 — 복제 파이프라인

### 4.1 모듈

`src/game/clone.{h,cpp}`. ImGui 를 모른다. 보관함 큐(`stash.cpp` 의
큐 상태 기계 `stash_queue.h`)와 같은 꼴로, **단조 시계**(`GetTickCount64`)를 쓴다 -
ImGui 시계는 창이 숨으면 멈춘다.

### 4.2 상태

```
Idle
 → Giving        request_give(session, 부적키, 1)
 → WaitInventory 인벤 레코드에서 부적을 찾아 컨테이너 종류 A·슬롯 B 를 얻는다
 → Hiring        request_hire_from_inventory(session, A, B)
 → WaitRoster    명부에 시작 전 스냅샷에 없던 번호가 나타날 때까지 기다린다
                 (2107 응답 pending_hire_acks 가 먼저 오면 그 번호를 쓴다)
 → Swapping      apply_species(reader, 새 번호, 대상 행, &msg)
 → Verify        명부를 다시 읽어 새 번호의 row == 대상 행
 → Done | Failed(사유 문자열)
```

- 시작 조건: 소환 널 가드 설치됨, `companion_pick_session() != 0`,
  `hire_from_inventory_ready()`, `clan_ready()`, 파이프라인이 Idle.
  하나라도 아니면 시작을 거부하고 이유를 돌려준다.
- 시작 때 명부 번호 집합을 스냅샷한다. 새 번호는 **차집합**으로 잡는다.
  2454 가 2107 을 내는지 미확인이라 응답에만 기대지 않는다.
- 새 번호가 둘 이상 나타나면(사용자가 동시에 고용) 종이 부적 종과 같은
  것을 고른다. 그래도 둘 이상이면 Failed("새 항목이 둘 이상") 로 멈춘다 -
  엉뚱한 개체의 종을 바꾸지 않는다.
- 쓰기는 `apply_species` 만 부른다. 주소는 그 함수가 매번 다시 해석한다.

### 4.3 데이터

```cpp
struct CloneTarget {           // 콤보 항목
    std::uint16_t char_row;    // 캐릭터 표 행
    std::string internal;      // 내부 이름
    std::string label;         // 표시명
    std::uint16_t merc_row;    // 타입행 (드래곤·전투기계·랩터)
    bool verified;             // §3 에서 소환·탑승이 확인된 행인가
};
struct CloneProgress {
    CloneState state; unsigned long long since_ms; int attempt;
    std::uint64_t new_no; std::string message;
};
```

대상 목록은 `roster` 의 캐릭터 표에서 `merc_row` 가 `Vehicle_Dragon`·
`Vehicle_WarMachine`·`Vehicle_WarMachine_Raptor` 인 행을 고른다(내부
이름으로 타입행을 푼다 - 행 번호를 박지 않는다). 검증 여부는
`clone.cpp` 의 작은 표에 내부 이름으로 적는다(§3 결과).

부적은 `roster_panel.cpp` 의 부적 표 6종 중 하나. 기본 1003847.

### 4.4 API

```cpp
bool clone_can_start(std::string* why);
bool clone_start(std::uint16_t target_row, std::uint32_t amulet_key, std::string* why);
void clone_tick(const mem::Reader& reader);   // 본창 틱에서 매 프레임
CloneProgress clone_progress();
void clone_cancel();                          // 진행 중이면 Failed("취소") 로
```

`clone_tick` 은 `overlay.cpp` 가 `stash_tick()` 을 부르는 자리에서 함께 부른다. 로스터 창을 닫아도
진행된다(보관함 큐와 같은 규칙).

## 5. UI — 로스터 창 새 탭

`RosterTab::Clone`, 탭 이름 **"탈것 복제"**. 탭 순서는 "내 동반자"
바로 뒤. 내용:

1. 머리글: 한 줄 설명("스토리 탈것의 복제본을 새 동반자로 등록합니다.
   원본은 건드리지 않습니다.").
2. 대상 종 콤보: §4.3 목록. 항목마다 `검증됨`/`미검증` 꼬리표. 미검증
   행을 고르면 경고 줄을 띄운다.
3. 부적 콤보: 6종, 기본 혹멧돼지.
4. [복제] 버튼: 2단 버튼(공용 확인 조각). 진행 중이면 잠근다.
   비활성 툴팁은 `clone_can_start` 의 이유를 그대로 낸다
   (`AllowWhenDisabled`).
5. 진행 줄: 상태 이름 · 경과 초 · 시도 횟수. Done 이면 새 번호와 종,
   Failed 이면 사유. 결과 알림은 공용 `Notice` 로 본창에도 낸다.
6. 아래에 이 세션에서 만든 복제본 목록(번호·종·시각). 줄을 누르면
   내 동반자 탭의 그 항목으로 이동한다.

게임 메모리에 쓰는 동작이므로 UI/UX 설계의 규칙을 따른다 - 확인 한
단계, 로그 한 줄(`2026-09-10-overlay-uiux-design.md` §1).

## 6. 오류 처리

| 상황 | 처리 |
|---|---|
| 시작 조건 미충족 | 버튼 잠금 + 이유 툴팁 |
| 큐가 밀림(`request_*` 가 false) | 2초 간격 3회 재시도, 그래도 안 되면 Failed("요청이 밀렸습니다") |
| Giving·WaitInventory 10초 초과 | Failed("부적이 인벤에 안 들어왔습니다") |
| Hiring 거부 코드 `0x73353994`(컨테이너 범위)·`0x06306EB0`(빈 슬롯) | Failed 에 코드 그대로 |
| WaitRoster 15초 초과 | Failed("명부에 새 항목이 없습니다"). 부적은 인벤에 남는다 |
| Swapping 실패(`SpeciesApply != Ok`) | Failed(msg). **부적 종 동반자가 남는다** - 무해하며 게임의 풀어주기로 지운다고 안내 |
| Verify 불일치 | Failed("교체 확인 실패") + 로그에 읽은 행 |
| 세션이 사라짐 | Failed("세션 없음") |

어느 실패도 원본 스토리 개체에는 영향이 없다 - 파이프라인은 원본 번호를
알지도 못한다.

## 7. 검증

- **단위 시험** `tests/clone_tests.cpp`: 상태 기계를 가짜 시계·가짜
  콜백(지급·인벤·등록·명부·교체)으로 돌린다. 정상 경로, 각 시한, 재시도
  3회 뒤 실패, 차집합이 0개·1개·2개(같은 종 하나만)·2개(둘 다 같은 종)
  인 경우, 취소. 와이어 조립은 기존 `companion_tests.cpp` 가 이미 덮는다.
- **게임 검증** (화면 확인 없이 "된다" 고 하지 않는다): 복제 → 게임 UI
  소환 → 탑승 → 저장·리로드 → 원본 호출. 크래시 로그 0건, `CDToybox.log`
  에 단계별 줄.
- **회귀**: 내 동반자 탭의 기존 바꾸기·근처 획득이 그대로 동작.

## 8. 후속 스펙 (이 문서 밖)

- **2단계 이름 편집.** 이름 붙은 말의 명부 레코드를 덤프해 `staticstringA`
  자리를 찾는다(엔진 문자열 객체 `char*` + 길이). 쓰기는 양쪽 세계,
  길이가 늘면 우리 버퍼로 바꿔 끼우는 방식의 안전성을 먼저 잰다.
- **3단계 레벨·경험치 편집.** `ExperienceLevelSaveData` 의 레코드 안 자리를
  아는 값으로 역산한다. 종 기본 능력치는 캐릭터 표라 여기서는 못 바꾼다.

## 9. 스파이크 결과 (채울 것)

| 행 | 내부 이름 | 목록 노출 | 소환 | 탑승 | 리로드 | 원본 무사 | 검사 코드 |
|---|---|---|---|---|---|---|---|
| | `Vehicle_Dragon` … | | | | | | |
| | `Vehicle_WarMachine` … | | | | | | |
| | `Vehicle_WarMachine` … | | | | | | |
| | `Vehicle_WarMachine_Raptor` … | | | | | | |

## 10. 근거 파일

- `src/game/clan.{h,cpp}` · `companion.{h,cpp}` · `grant.h` · `roster.h`
  · `spawnguard.cpp` · `stash.cpp`(큐 상태 기계 본보기)
- `src/render/roster_panel.cpp`
- `docs/superpowers/specs/2026-09-08-companion-catalog-and-acquire-research.md`
- `docs/superpowers/specs/2026-09-10-companion-species-swap-session.md`
- `docs/superpowers/specs/2026-09-07-companion-add-session.md`
- `docs/superpowers/specs/2026-09-10-overlay-uiux-design.md`
