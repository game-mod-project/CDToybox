# 드래곤·A.T.A.G. — 장소 제한과 강제 하차 (2026-09-16)

드래곤 소환이 풀린 뒤(`2026-09-16-dragon-external-research.md`) 남은 두 가지를
조사한 기록: **마을 등에서 못 부르는 조건**과 **타고 있다가 강제로 내려지는
조건**, 그리고 그것을 푸는 길.

**전부 게임 데이터에서 평문으로 나왔다.** 아직 게임에서 풀어 본 것은 없다 —
어디까지가 읽은 것이고 어디부터가 제안인지 절마다 밝힌다.

---

## 0. 한 줄

조건이 **`gamedata/failmessageinfo`** 에 있고, 그 항목이 `ConditionInfo` 키를
가리키며, 그 조건식이 **사람이 읽는 문장**이다. 드래곤은 조건 **다섯**,
A.T.A.G. 는 **넷**이 전부다.

---

## 1. 조건 전문 (실측, 게임 데이터)

`failmessageinfo` 11행을 전부 풀었다. 항목 배치는 §4.

### 1-1. 드래곤

| 언제 | 조건식 | 조건키 |
|---|---|---|
| **못 부른다** | `!CheckVehicleAllowableHeight()` | 1011129 |
| **못 부른다** | `IsInTown() && !IsAboveRoad(Bird,20)` | 1011130 |
| **강제 하차** | `!CheckVehicleAllowableHeight()` | 1011129 |
| **강제 하차** | `IsInTown() && !IsAboveRoad(Bird,20)` | 1011130 |
| **강제 하차** | `IsEnterToFieldLoadingComplete() && !IsVehicleAllowedInEnteredRegion(Vehicle_Dragon)` | 1011133 |

부르는 것과 내려지는 것이 **앞의 둘을 공유**한다. 하차에만 세 번째가 더 붙는다.

곁가지(동반자 쪽 · 탑승):

```
CallVehicleDragon_Mercenary   CheckVoxelType(Ground) && !CheckActionAttribute(Fly)
                              NavigationLoaded() && !IsVisibleToTarget()
BoardVehicleDragon_Owner      CheckDistanceHorizontalToTarget()>50
BoardVehicleDragon_Mercenary  NavigationLoaded() && !IsVisibleToTarget()
```

### 1-2. A.T.A.G. (Mechanic)

| 언제 | 조건식 | 조건키 |
|---|---|---|
| **못 부른다** | `IsInTown()` | 1001739 |
| **못 부른다** | `CheckDistanceHorizontalToTarget()<30` | 1011135 |
| **강제 하차** | `IsInTown()` | 1001739 |
| **강제 하차** | `IsEnterToFieldLoadingComplete() && !IsVehicleAllowedInEnteredRegion(Vehicle_WarMachine)` | 1011136 |

**A.T.A.G. 의 마을 조건은 맨몸 `IsInTown()`** 이다 — 드래곤처럼 "길 위 20 이상"
예외가 없다. 그래서 마을에서 더 빡빡하다.

### 1-3. 와이번 (대조군)

```
CallVehicleWyvern_Owner   CheckNone()          <- 부르는 데 조건이 없다
KeepRidingWyvern_Owner    !CheckVehicleAllowableHeight()
                          ... !IsVehicleAllowedInEnteredRegion(Vehicle_Dragon)
```

와이번은 **아무 데서나 불린다.** 하차 조건만 드래곤과 공유한다(같은
`Vehicle_Dragon` 태그를 쓴다 - 둘 다 비행 계열이라 한 묶음으로 본다).

---

## 2. 그래서 관문은 셋이다

| 관문 | 무엇이 정하나 | 어디에 있나 |
|---|---|---|
| **고도** `CheckVehicleAllowableHeight()` | `VehicleInfo._maxAllowableHeight`(+0x9C) · `RegionInfo._overriedMaxHeight`(+0x6C) | 정적 표 |
| **마을** `IsInTown()` | `RegionInfo._isTown`(+0x75) — **확정**(§5-1-1 나) | 정적 표 |
| **지역 허용** `IsVehicleAllowedInEnteredRegion(X)` | ~~`_forbiddenMercenaryKeyList`(+0x80)~~ — **아니었다**(§5-1-1 다). 다시 찾는 중 | ? |

`RegionInfo` 필드 오프셋은 `tools/rtti/fields.py` 로 뽑은 **실측**이다:

```
_overriedMaxHeight  +0x6C     _regionType            +0x70
_limitVehicleRun    +0x74     _isTown                +0x75
_isWild             +0x76     _isNonePlayZone        +0x78
_forbiddenMercenaryKeyList +0x80
_isAccompanyAllowed +0x91     _tagList               +0xA8
```

> ⚠️ **`_forbiddenMercenaryKeyList` 가 `IsVehicleAllowedInEnteredRegion` 의
> 근거라는 것은 아직 가정이다.** 이름이 맞아 보일 뿐 확인하지 않았다.
> `_limitVehicleRun` 일 수도 있다. 쓰기 전에 §5-1 로 가른다.

곁가지로, 드래곤·A.T.A.G. 의 시간 제한은 이미 알려져 있다(정본):
`CharacterInfo._callMercenarySpawnDuration` **600초(10분)** ·
`_callMercenaryCoolTime` **3600초(60분)**. 넥서스 최대 인기 모드
"Unlimited Dragon Flying"(11.6만 다운)이 패치하는 것이 이 **두 값**이다.

그리고 "오래 타기" 버프가 데이터에 있다:

```
skill 행 227  키 11634  Active_RideDragon_Long
skill 행 228  키 11635  Active_RideATAG_Long
조건 1001654  CheckRidingVehicleType(Dragon) && CheckBuffTag(RideDragon_Long)
조건 1001655  (CheckRidingVehicleType(WarMachine) || ...) && CheckBuffTag(RideATAG_Long)
```

이 둘이 시간 제한을 늘리는 쪽으로 보이지만 **어디서 쓰이는지 안 쫓았다.**

---

## 3. 실행 파일의 오류 이름 (조건과 대조용)

```
eErrNoCallVehicleInvalidPosition       eErrNoCallVehicleInvalidAltitude
eErrNoCallVehicleInvalidAir/Ground/Water
eErrNoCallVehicleInvalidLimitRide      eErrNoCallVehicleInvalidQuickSlot
eErrNoCallVehicleMercenaryRegion       eErrNoCallVehicleMercenaryIndoor
eErrNoCallVehicleMercenaryOnRoof       eErrNoCallVehicleMercenaryRideLimit
eErrNoCallVehicleMercenaryMovableNavigation
eErrNoCallVehicleBlockedSpawnPositionByObstacle
eErrNoCallVehicleCoolTimeExist
```

조건 클래스도 이름으로 있다 — `ConditionData_IsVehicleAllowedInEnteredRegion` ·
`ConditionData_IsInTown` · `ConditionData_IsInRegionType` ·
`AICondition_CheckIsInDoor` · `AICondition_IsInTown`.

---

## 4. `failmessageinfo` 배치 (실측으로 풀었다)

한때 "이진 배치 미해독" 으로 남겨 뒀던 자리다. 11행짜리 작은 표다.

```
레코드: u32 _key · u32 이름길이 · 이름 · u8 _isBlocked · u32 항목수 · 항목 × n
항목(33바이트 고정):
  +0x00 u32  **조건 키**  -> ConditionInfo._key
  +0x04 3바이트 ?         (2f f0 02 로 전부 같다)
  +0x07 u16 ?             (0 · 4 · 8 - 항목 번호로 보인다)
  +0x09 u32  부모 키      (그 레코드 자신의 _key)
  +0x0D u32 길이 + 문자열 **메시지 id**(16자리 숫자 문자열)
```

검증: 조건 키가 전부 `conditioninfo` 에서 풀렸고, 문장이 화면에서 보는 증상과
정확히 맞는다(마을에서 못 부른다 · 고도 · 지역).

---

## 5. 푸는 길 — 후보와 값 (**아직 아무것도 시험 안 했다**)

### 5-0. **화면 문구가 원인을 알려 준다** (실측, 가장 싼 계측)

`failmessageinfo` 항목마다 **조건별로 다른 메시지 id** 가 붙어 있다. 즉 거부
문구만 읽으면 셋 중 어느 조건이 섰는지 **메모리를 안 걷고** 알 수 있다.

| 상황 | 메시지 id | 조건 |
|---|---|---|
| 드래곤 못 부름 | 4294975885935344 | `!CheckVehicleAllowableHeight()` |
| 드래곤 못 부름 | 4294975886197488 | `IsInTown() && !IsAboveRoad(Bird,20)` |
| 드래곤 강제하차 | 4294967296000752 | `!CheckVehicleAllowableHeight()` |
| 드래곤 강제하차 | 4294967296262896 | `IsInTown() && !IsAboveRoad(Bird,20)` |
| 드래곤 강제하차 | 4294967296525040 | `!IsVehicleAllowedInEnteredRegion(Vehicle_Dragon)` |
| A.T.A.G. 못 부름 | 4294971590968048 | `IsInTown()` |
| A.T.A.G. 못 부름 | 4294971591230192 | `CheckDistanceHorizontalToTarget()<30` |
| A.T.A.G. 강제하차 | 4294988770837232 | `IsInTown()` |
| A.T.A.G. 강제하차 | 4294988771099376 | `!IsVehicleAllowedInEnteredRegion(Vehicle_WarMachine)` |

id 는 `(레코드 _key << 32) | 순번` 이다(예: 1000002<<32 + 752). 현지화 키이므로
게임 안에서 문구로 풀린다.

**그래서 계측은 이것으로 한다** — 막히는 자리에서 문구를 읽고 위 표와 맞댄다.
`RegionInfoManager` 를 걷는 것보다 훨씬 싸다(§5-1 은 그 시도의 기록으로 남긴다).

### 5-0-1. 화면으로 확정된 것 (사용자 스크린샷)

| 문구 | 조건 |
|---|---|
| "마을에서는 해당 탑승물을 **호출**할 수 없습니다" | `IsInTown()` |
| "마을에서는 … 탑승할 수 없어 **자동으로 하차**합니다" | `IsInTown()` |
| "**A.T.A.G. 진입 불가 지역**입니다" | `IsVehicleAllowedInEnteredRegion(Vehicle_WarMachine)` |
| "**블랙스타 진입 불가 지역**입니다" | `IsVehicleAllowedInEnteredRegion(Vehicle_Dragon)` |

**둘 다 확인됐다** — 마을(소환·하차 공통)과 진입 불가 지역(지도의 붉은 구역)이
서로 다른 조건이다. **고도 조건은 안 걸린다** — 걸렸다면 문구가 달랐다.

지도가 붉은 구역을 **그린다**는 것은 클라이언트가 그 지역 집합을 알고 있다는
뜻이다. 아직 안 쫓은 단서다.

### 5-1. `RegionInfoManager` 를 걸으려다 멈춘 자리 (기록 · **§5-1-1 에서 풀렸다**)

> 이 절은 **헛걸음의 기록**이다. 결론은 아래 §5-1-1 을 보라.

어느 조건이 실제로 걸리는지부터 본다. 지금은 **셋 다 가능성**이고, 마을에서
막히는 것이 `IsInTown()` 인지 `IsVehicleAllowedInEnteredRegion` 인지 모른다.

- RTTI 로 `RegionInfoManager` 인스턴스는 **찾았다**(vtable `0x14549B360`,
  인스턴스 2개). 그런데 **배치가 다른 매니저와 다르다** — 클래스가
  `StaticInfoManager2<RegionKey, RegionInfo, RegionInfoManager, u16>` 템플릿이고,
  `KnowledgeInfoManager` 의 `+0x08 개수 / +0x58 배열` 이 여기서는 안 맞는다
  (`+0x08` 이 48680, 후보 포인터 셋을 떠 봤지만 `RegionInfo*` 배열이 아니었다).
  **여기서 멈췄다** - §5-0 이 훨씬 싸다.
- 그 뒤 더 파 본 것(2026-09-16 밤):
  - **모듈 전역은 읽힌다.** 앞 문서가 "probe 로 안 읽힌다" 고 적은 것은 **주소
    오타**였다(자릿수 하나 더). `0x146C2E2D8`(지식 매니저 전역)이 그대로 읽힌다.
  - 지식 매니저 배치를 라이브로 확인했다 — **`+0x08` 이 6713**(지식 수와 일치),
    `+0x58` 이 배열. 다른 매니저도 같은 모양이다.
  - **RTTI 로 잡은 `0x2FC87F61130` 은 진짜 매니저가 아니다** — `+0x08` 이 48680
    이고 `+0x58` 이 포인터가 아니며, **모듈 안에서 그것을 가리키는 전역이 0개**다
    (`findptr`). vtable `0x14549B360` 은 `RegionInfoManager` 가 아니라 같이 묶여
    나온 `UIEventWrap`/`StaticInfoWrapper` 쪽일 수 있다.
  - 매니저 전역 구역(`모듈+0x6C2E000`~`+0x6C30000`, 후보 포인터 170개)을 훑어도
    그 vtable 을 가진 객체가 **없다.**
  - **다음에 할 것:** `.?AVRegionInfoManager@pa@@` 의 vtable 을 RTTI 로 **정확히**
    집어(지금은 세 클래스가 한 vtable 로 묶여 나왔다) 다시 찾는다.
- 커뮤니티 `regioninfo_parser.py` 는 **우리 빌드에서 안 돌았다**(1007개 중 0개
  파싱). 예약 슬롯 파서와 같은 문제다 - 버전이 다르다. 쓰지 말 것.

### 5-1-1. **풀렸다 — 매니저 전역도, `IsInTown()` 의 실제 데이터도** (2026-09-17, 정적 실측)

§5-1 에서 막혔던 것을 **실행 파일만 보고** 풀었다. 게임을 켤 필요도 없었다.

#### (가) `RegionInfoManager` 전역 = **모듈 + 0x6C2E2F0**

찾은 길: `regioninfo` 라는 **표 이름 문자열**(RVA 0x597B990)을 참조하는 코드를
뒤졌다. 그중 RVA 0x49BBD2 가 진짜 조회 함수였다.

```asm
0x0049BB92  movzx edi, word ptr [rcx]        ; 인자 = RegionKey (u16)
0x0049BB95  mov   rbx, [rip+...]             ; -> **RVA 0x6C2E2F0 = 매니저 전역**
0x0049BB9C  cmp   edi, dword ptr [rbx + 8]   ; +0x08 = 개수
0x0049BBA5  lea   rsi, [rdi*8]
0x0049BBAD  mov   rax, qword ptr [rbx + 0x58]; +0x58 = RegionInfo* 배열
0x0049BBB1  mov   rax, qword ptr [rsi + rax] ; 배열[키]
```

**지식 매니저와 배치가 똑같다**(`+0x08` 개수 / `+0x58` 배열). 게다가 주소가
`0x6C2E2D8`(지식) 바로 **+0x18** 뒤다 — 매니저 전역들이 한 구역에 줄지어 있다.

> 앞 절이 **찾지 못한 이유**가 이제 분명하다. 런타임 RTTI 로 집었던
> vtable `0x14549B360` 은 남의 것이었다. 진짜 `RegionInfoManager` vtable 은
> **RVA 0x597BBF0 (VA 0x14597BBF0)** 이다(`find_class.py` 가 TD→COL→vtable 로
> 정확히 집어 준다). 런타임 `instances` 가 세 클래스를 한 vtable 로 묶어 낸 것을
> 그대로 믿은 것이 헛걸음의 원인이었다.

#### (나) `IsInTown()` 은 **정적 표를 먼저 읽는다** — 둘의 OR 이다

> 이 절은 2026-09-17 낮에 한 번 **틀리게 적었다가** 같은 날 고쳤다. 처음에는
> 판정 함수의 **꼬리만** 떠서 "정적 표를 안 읽는다"고 썼는데, 그 앞에 표를
> 훑는 호출이 따로 있었다. 지금 적는 것이 함수 전체를 뜬 결과다.

`AICondition_IsInTown`(vtable RVA 0x57A9508)의 판정 함수는 **slot 1, RVA
0x2232130**. 가지가 셋이다(반환은 **참=0 · 거짓=1 · 판정불가=2** 규약):

```asm
0x02232160  call 0x141764580           ; (1) 표를 훑는 IsInTown(액터)
0x02232165  test al, al
0x02232167  je   ...                   ; 0 이면 다음 가지로
0x02232169  xor  bl, bl                ; 1 이면 -> 결과 0 = **참(마을)**
...
0x0223218D  mov rax, [rsp+0x28]        ; (2) 살아있는 칸
0x02232192  mov rcx, [rax + 0x68]
0x02232196  mov rax, [rcx + 0xb0]
0x0223219D  cmp dword ptr [rax + 0x358], 0
0x022321AE  je  -> 결과 1 = 거짓        ; 0 이면 마을 아님
0x022321B4      -> 결과 0 = **참**      ; 0 이 아니면 마을
```

**(1) 표를 훑는 쪽**(RVA 0x1764580)이 본체다:

```asm
0x017645A8  mov rax, [rbx + 0x68]      ; 액터 컴포넌트 홀더
0x017645AC  mov rcx, [rax + 0x1a0]     ; **구역 상태 컴포넌트**
0x017645B3  mov rax, [rcx + 0x38]      ; 지금 들어와 있는 구역 목록
0x017645B7  mov rbx, [rax + 0x10]      ;   +0x10 시작
0x017645BB  mov eax, [rax + 0x18]      ;   +0x18 개수, 항목 stride 0xC
0x017645D3  call 0x14049bb80           ; 항목 앞 u16 -> RegionInfo*
0x017645D8  cmp byte ptr [rax + 0x75], 0   ; _isTown
0x017645DC  jne -> 1                   ; 하나라도 마을이면 참
```

그래서 정리하면

```
IsInTown(액터) = (지금 들어와 있는 구역 중 하나라도 RegionInfo._isTown != 0)
              || (dword [[액터+0x68]+0xB0]+0x358 != 0)
```

**실측으로 확인했다**(2026-09-17, 게임 PID 52684). 플레이어가 데메니스 성당에
서 있을 때:

```
살아있는 칸 [[+0x68]+0xB0]+0x358 = 0        <- 마을인데도 0 이다
지금 들어와 있는 구역 6개
  행 0    키 1      Region_Pywel                        _isTown=0
  행 333  키 6      Region_Delpheon                     _isTown=0
  행 334  키 14     Region_Demenissian_Territory        _isTown=0
  행 335  키 12000  Region_Demenissian                  _isTown=0
  행 348  키 44045  Region_Node_Dem_DemenissCathedral   _isTown=1  <- 이것
  행 624  키 10032  Region_Golden_Plains                _isTown=0
```

즉 **평소에 걸리는 것은 (1) 정적 표 쪽**이고, 살아있는 칸은 0이라 놀고 있다.
앞서 "살아있는 칸이 1순위" 라고 적었던 것은 그래서 뒤집힌다.

> **구역 목록의 u16 은 `_key` 가 아니라 표의 행 번호다.** 조회 함수
> 0x49BB80 은 그 값을 **배열 첨자로 그대로** 쓴다(`cmp edi,[rbx+8]` 로 개수와
> 비교한 뒤 `[rbx+0x58] + idx*8`). 키로 찾으려다 "표에 없음" 만 여섯 줄 본 뒤
> 알았다.

같은 모양의 함수가 하나 더 있다 — **`IsLimitVehicleRun(액터)`(RVA 0x1764050)**,
`_limitVehicleRun`(+0x74)을 같은 식으로 훑는다.

#### (다) 표 전수 조사 (실측 · 1007행)

런타임 표를 통째로 읽어 세어 봤다(`walk.py`/`live.py`, 이름까지 1007/1007 해석).

| 칸 | 참인 구역 수 | 비고 |
|---|---|---|
| `_isTown` | **172** | 앞 문서의 "159" 는 틀렸다 |
| `_limitVehicleRun` | **15** | 대도시·성당·저택 (헤르난드/칼페이드/델레시아 등) |
| `_isNonePlayZone` | 4 | `Region_NoneplayZone_*` |
| `_isBlocked` | **0** | 아무 데도 안 켜져 있다 |
| `_isAccompanyAllowed == 0` | **1** | `Region_Abyss`(행 728) |
| `_forbiddenMercenaryKeyList` 비지 않음 | **1** | `Region_Abyss`, 6개 |

**여기서 §2 의 추정 하나가 깨진다.** `IsVehicleAllowedInEnteredRegion` 의 재료가
`_forbiddenMercenaryKeyList` 라고 적었는데, 그 목록은 **어비스에만** 있다.
지도의 붉은 구역이 그 목록에서 오는 것이 **아니다.** 붉은 구역 레버는 다시
찾아야 한다.

새로 나온 칸 하나: **`_isAccompanyAllowed`(+0x91)**. 구역 진입 처리
(RVA 0x2B02205 부근)가 이것을 보고 0이면 동반자를 **강제 해산**시킨다 —
UI 문구 `UI_Alert_ForceDisbandAccompanyNotAllowedRegion` ·
`UI_MercenaryQuickSlot_NonCallable_AccompanyNotAllowed` 가 그 짝이다. 어비스
전용이라 마을 제한과는 무관하다.

#### (라) 아직 모르는 것

- `[액터+0x68]+0xB0` 의 정체. 런타임 `whatis` 는
  `ClientSequencerPlayDataActorComponent` 라고 답했는데 이름이 안 어울린다 —
  `+0x358` 이 0 인 채로만 관측돼서 의미를 못 봤다. **당장은 안 써도 된다**
  (표 쪽만 꺼도 `IsInTown()` 이 거짓이 됐다).
- `IsVehicleAllowedInEnteredRegion` 의 판정 자리. `ConditionData_` 만 있고
  `AICondition_` 이 없다(vtable RVA 0x5856378, 생성 자리 RVA 0x217F689).
  vtable 슬롯 2가 범용 디스패처라 한 겹 더 따라가야 한다.

#### (마) 쓸 만한 주소 · 함수 (실측)

| 무엇 | 자리 |
|---|---|
| `RegionInfoManager` 전역 | 모듈 `+0x6C2E2F0` (`+0x08` 개수 1007 · `+0x58` 배열) |
| `RegionInfoManager` vtable | RVA `0x597BBF0` |
| 행 번호 → `RegionInfo*` | RVA `0x49BB80` |
| `IsInTown(액터)` (표) | RVA `0x1764580` |
| `IsLimitVehicleRun(액터)` | RVA `0x1764050` |
| `AICondition_IsInTown` 판정 | RVA `0x2232130` (vtable `0x57A9508` slot 1) |
| 같은 판정의 클라 사본 | RVA `0x377430` |
| 액터의 구역 상태 | `[[액터+0x68]+0x1A0]+0x38` → `+0x10` 시작 · `+0x18` 개수 · stride 0xC |
| `RegionInfo` 레코드 | 192바이트(0xC0) · 목록 칸 = `{ptr +0, u32 개수 +8, u32 용량 +0xC}` |

필드 오프셋 전체(`tools/rtti/fields.py`, 20개 짝지음):

```
_stringKey   +0x08   _isBlocked    +0x10   _displayRegionName +0x18
_knowledgeInfo +0x38 _regionEnterknowledgeInfoList +0x40
_childRegionInfoList +0x58  _bitmapColor +0x69  _overriedMaxHeight +0x6C
_regionType  +0x70   _fogClearCondition +0x72  _limitVehicleRun +0x74
_isTown      +0x75   _isWild       +0x76   _isUIMapDisable +0x77
_isNonePlayZone +0x78  _forbiddenMercenaryKeyList +0x80
_isWorldMapRoadPathFindable +0x90  _isAccompanyAllowed +0x91
_domainFactionList +0x98  _tagList +0xA8
```

#### (라) 곁가지로 확인한 표 배치 (앞 문서 정정)

`regioninfo.staticinfoheader` 는 **행 1007개**인데 항목이 **6바이트**다:

```
헤더: u16 개수 · {u16 키, u32 본문오프셋} × 개수
```

정본이 적어 둔 `{u32 키, u32 오프셋}` 8바이트는 **표마다 다르다** — 키 폭이
그 표의 키 타입을 따라간다(`StaticInfoManager2<RegionKey,RegionInfo,...,u16>`).
커뮤니티 `regioninfo_parser.py` 가 1007개 중 0개를 뽑은 것도 이것 때문으로 보인다.

레코드 앞머리(역어셈블로 확인한 **읽는 순서**, `RegionInfo::Deserialize`
RVA 0x1485E50):

```
u16 _key · str _stringKey · u8 _isBlocked · {현지화} _displayRegionName
· u32 _knowledgeInfo · 목록 _regionEnterknowledgeInfoList · u16(+0x50)
· 목록 _childRegionInfoList · u8(+0x68) · u8 _bitmapColor
· f32 _overriedMaxHeight · u8 _regionType · ? _fogClearCondition
· u8 _limitVehicleRun · **u8 _isTown** · u8 _isWild · u8 _isUIMapDisable
· u8 _isNonePlayZone · 목록 _forbiddenMercenaryKeyList · u8 _isWorldMapRoadPathFindable ...
```

### 5-2. 손댈 자리 (제안)

정적 표 쓰기라 **세이브에 안 남고** 실행마다 다시 걸어야 한다 — 우리가 이미
`vehicle_place_gated` 계열에서 하는 방식과 같다.

| 순위 | 무엇 | 어디 | 기대 |
|---|---|---|---|
| **1** | **마을 관문(표)** | **`RegionInfo._isTown`(+0x75) → 0** (172행) | **`IsInTown()` 이 즉시 거짓. 서 있는 자리에서도 듣는다**(§5-1-1 나) |
| 2 | 마을 판정(살아있는 칸) | `[[액터+0x68]+0xB0]+0x358`(i32) → 0 | 1 의 보조. 실측에선 늘 0 이라 평소엔 안 걸린다 |
| 3 | 탈것 달리기 제한 | `RegionInfo._limitVehicleRun`(+0x74) → 0 (15행) | 대도시·성당에서 탈것이 안 묶인다 |
| 4 | 고도 관문 | `VehicleInfo._maxAllowableHeight`(+0x9C) · `RegionInfo._overriedMaxHeight`(+0x6C) | `CheckVehicleAllowableHeight()` 통과 (화면상 안 걸린다 · §5-0-1) |
| 5 | 시간 제한 | `CharacterInfo._callMercenarySpawnDuration`(600초) | 10분 강제 하차 없어짐 |
| 6 | 재소환 대기 | `CharacterInfo._callMercenaryCoolTime`(3600초) | 60분 쿨다운 없어짐 |
| — | ~~지역 허용~~ | ~~`_forbiddenMercenaryKeyList`(+0x80) → 0~~ | **빠졌다.** 그 목록은 어비스에만 있다(§5-1-1 다) |

**1 은 실제로 걸어 봤다.** 플레이어가 서 있던 `Region_Node_Dem_DemenissCastle`
한 행의 `_isTown` 을 1→0 으로 쓰자 `IsInTown()` 의 재료가 그 자리에서 사라졌다
(6개 구역 전부 `_isTown=0`, 살아있는 칸도 0). 되돌리기는 같은 자리에 1 을 쓰면
된다 — 표는 **세이브에 안 남고** 실행마다 다시 걸어야 한다(`vehicle_place_gated`
계열과 같은 방식).

> ⚠️ **1·2 는 범위가 넓다.** `IsInTown()` 은 현상금·상점·NPC 일과 등 **다른
> 계통이 같이 쓴다**(조건식에서 확인: `WantedLevel()>=1 && WantedState(Normal)
> && IsInTown()` 등). 그래서 **늘 켜 두는 것이 아니라 사용자가 켜고 끄는
> 토글**로 둔다 — 부를 때만 내리고 곧바로 되돌린다. 되돌릴 수 있게 **바꾼 행과
> 원래 값을 들고 있어야** 한다.
>
> ⚠️ **`VehicleInfo._canCallSafeZone` 은 우리 빌드에 없다**(커뮤니티 §5-4 가
> 쓰는 칸. 리플렉션 이름 목록에 안 나온다). 후보에서 내렸다.

### 5-3. 이미 우리가 푸는 것 (중복 주의)

`vehicle_place_gated` 가 얹기와 함께 **이미** 셋을 풀고 있다(정본 §2-5):
`CharacterInfo._callMercenarySpawnVoxelType` · `VehicleInfo._checkDistanceToGround` ·
`_escapeRoadGroupType` · `_maxAllowableHeight` · `_callVehicleVoxelType`.

**그런데 그것들은 드래곤 거부의 원인이 아니었다**(정본 §2-5 - 다 풀어도 안 됐다).
이제 그 이유가 설명된다 — 실제 조건은 위 다섯이고, 그중 우리가 건드리던 것은
**고도 하나뿐**이었다.

### 5-4. 외부 선례

- 넥서스 **Unlimited Dragon Flying**(11.6만 다운) — 10분 하차 / 60분 쿨다운
  두 값을 푼다. 즉 **시간 제한은 데이터 모드로 풀린다는 것이 이미 증명됐다.**
- 커뮤니티 **`Mount Everywhere.json`** 을 열어 봤다(250건, 3개 파일).
  **무슨 칸을 바꾸는지 전부 나왔다:**

```
gamedata/vehicleinfo.pabgb   (31건)
    _canCallSafeZone            00 -> 01     Dragon · WarMachine · 나머지 29종
gamedata/regioninfo.pabgb    (174건)
    _isTown                     x159
    _limitVehicleRun            x15
gamedata/characterinfo.pabgb (45건)
    Riding_Dragon_1: _callMercenaryCoolTime        100E -> 0000        (3600초 -> 0)
    Riding_Dragon_1: _callMercenarySpawnDuration   58020000 -> FFFFFF7F (600초 -> INT_MAX)
    Riding_WarMachine_Unique_1: 같은 둘
```

  **`VehicleInfo._canCallSafeZone` 이 마을 소환 제한의 진짜 칸이다** — 우리가
  `RegionInfo` 쪽만 보다가 놓칠 뻔했다. `_isTown` 을 159개 지역에서 끄는 것도
  같이 하는데, §5-2 의 경고대로 그것은 범위가 넓은 조치다.

  시간 제한 값도 확인됐다: 쿨다운은 **0**, 지속은 **0x7FFFFFFF**(INT_MAX).

---

## 6. 다음 한 걸음

§5-1-1 의 1~3번은 **끝났다**(매니저 확인 · 표 전수 조사 · 판정 함수 전문 ·
`_isTown` 쓰기까지). 남은 것:

1. **게임에서 실제로 불러 본다.** `_isTown` 을 끈 상태에서 마을 한복판에서
   드래곤·A.T.A.G. 를 불러 보고, 타고 있을 때 강제 하차가 안 오는지 본다.
   이것이 마지막 확인이다 — 조건식(§1)과 판정 함수(§5-1-1)는 이미 맞췄다.
2. **붉은 구역 레버를 다시 찾는다.** `_forbiddenMercenaryKeyList` 가 아니었다
   (어비스 전용). `ConditionData_IsVehicleAllowedInEnteredRegion`
   (vtable RVA 0x5856378) 의 판정 자리를 한 겹 더 따라간다.
3. 되면 **모드에 토글로 얹는다.** 표 쓰기는 `RegionInfoManager`
   (모듈 `+0x6C2E2F0`)에서 행을 돌며 `+0x75`/`+0x74` 한 바이트씩. 켤 때 원래
   값을 들고 있다가 끌 때 되돌린다. 세이브에 안 남으니 실행마다 다시 건다.
4. 시간 제한 둘(`_callMercenarySpawnDuration` · `_callMercenaryCoolTime`)은
   지역과 무관하고 외부 선례가 확실하니 언제 해도 된다.

> `VehicleInfo._canCallSafeZone` 은 **우리 빌드에 없다**. 커뮤니티가 쓰는 칸이라
> 1순위로 뒀었지만 리플렉션 이름 목록에 안 나온다 — 후보에서 뺐다.

---

## 7. 관련

- `2026-09-16-dragon-external-research.md` — 드래곤 소환 해결 전문
- `2026-09-16-story-vehicle-wheel.md` — 정본(관문 지도)
- `../../STATUS.md` §1.21
