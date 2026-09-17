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
| **마을** `IsInTown()` | `RegionInfo._isTown`(+0x75) | 정적 표 |
| **지역 허용** `IsVehicleAllowedInEnteredRegion(X)` | `RegionInfo._forbiddenMercenaryKeyList`(+0x80) 로 보인다 — **미확인** | 정적 표 |

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

#### (나) `IsInTown()` 은 **정적 표를 안 읽는다 — 플레이어의 살아있는 칸을 읽는다**

`AICondition_IsInTown`(vtable RVA 0x57A9508)의 판정 함수(slot 1, RVA 0x2232130)
끝이 전부다:

```asm
0x0223218D  mov rax, [rsp+0x28]        ; 대상 액터
0x02232192  mov rcx, [rax + 0x68]      ; 액터 컴포넌트 홀더 (우리가 늘 쓰는 그 칸)
0x02232196  mov rax, [rcx + 0xB0]      ; +0xB0 컴포넌트
0x0223219D  cmp dword ptr [rax + 0x358], 0
0x022321AE  je  -> 결과 1              ; 0 이면 "마을 아님"
0x022321B4      -> 결과 0              ; 0 이 아니면 "마을"
```

즉 **`IsInTown()` = `dword [[[액터+0x68]+0xB0]+0x358] != 0`**. `RegionInfo._isTown`
을 그 자리에서 읽는 것이 아니라, **지금 들어와 있는 마을 구역 수**로 보이는
정수 한 칸을 읽는다(0 이면 아님). `_isTown` 은 그 칸을 **올리는 쪽**의 재료다.

레버가 둘로 갈린다:

| 레버 | 어디 | 범위 | 언제 듣나 |
|---|---|---|---|
| **살아있는 칸** | `[[액터+0x68]+0xB0]+0x358` → 0 | 그 순간 · 그 액터만 | **즉시** |
| 정적 표 | `RegionInfo._isTown`(+0x75) → 0 | 그 구역 전부 | 다음 구역 진입부터 |

이미 마을 **안에 서 있는 상태**에서 부르려면 정적 표만으로는 안 된다 — 칸은
이미 올라가 있다. **살아있는 칸이 1순위**다.

#### (다) 아직 모르는 것

- `[액터+0x68]+0xB0` 이 **어느 컴포넌트**인지 (런타임에 `whatis` 로 이름표를 본다).
- `+0x358` 이 정말 "겹친 마을 구역 수" 인지, 아니면 구역 키/비트마스크인지.
- `IsVehicleAllowedInEnteredRegion` 의 판정 함수 — `ConditionData_` 만 있고
  `AICondition_` 이 없다(468 : 435, 33개가 짝이 없고 이것이 그중 하나다).
  붉은 구역 쪽 레버는 여전히 `RegionInfo._forbiddenMercenaryKeyList`(+0x80) 다.

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
| **1** | **마을 판정** | **`[[액터+0x68]+0xB0]+0x358`(i32) → 0** | **`IsInTown()` 이 그 자리에서 거짓. 마을 안에 서 있어도 듣는다**(§5-1-1) |
| 2 | 지역 허용 | `RegionInfo._forbiddenMercenaryKeyList`(+0x80) 개수 → 0 | `IsVehicleAllowedInEnteredRegion` 참 — 붉은 구역 풀림 |
| 3 | 마을 관문(표) | `RegionInfo._isTown`(+0x75) → 0 | 다음 구역 진입부터 마을 칸이 안 오름 |
| 4 | 고도 관문 | `VehicleInfo._maxAllowableHeight`(+0x9C) · `RegionInfo._overriedMaxHeight`(+0x6C) | `CheckVehicleAllowableHeight()` 통과 (화면상 안 걸린다 · §5-0-1) |
| 5 | 시간 제한 | `CharacterInfo._callMercenarySpawnDuration`(600초) | 10분 강제 하차 없어짐 |
| 6 | 재소환 대기 | `CharacterInfo._callMercenaryCoolTime`(3600초) | 60분 쿨다운 없어짐 |

`RegionInfo` 에 닿는 길은 이제 있다 — **매니저 전역 `모듈+0x6C2E2F0`**,
`+0x08` 개수 / `+0x58` 배열(§5-1-1). 정적 표 쓰기는 **세이브에 안 남고** 실행마다
다시 걸어야 한다 — `vehicle_place_gated` 계열에서 이미 하는 방식과 같다.

> ⚠️ **1·3 은 범위가 넓다.** `IsInTown()` 은 현상금·상점·NPC 일과 등 **다른
> 계통이 같이 쓴다**(조건식에서 확인: `WantedLevel()>=1 && WantedState(Normal)
> && IsInTown()` 등). 그래서 **1 은 늘 켜 두는 것이 아니라 사용자가 켜고 끄는
> 토글**로 둔다 — 부를 때만 내리고 곧바로 되돌린다. 3 은 표를 고치는 것이라
> 세션 내내 남으므로 더 위험하다.
>
> ⚠️ **`VehicleInfo._canCallSafeZone` 은 우리 빌드에 없다**(커뮤니티 §5-4 가
> 쓰는 칸. 리플렉션 이름 목록에 안 나온다). 1순위에서 내렸다.

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

§5-1-1 로 **닿는 길이 둘 다 열렸다.** 남은 것은 런타임에서 재는 일이다.
순서는 **싼 것부터**다.

1. **`[[액터+0x68]+0xB0]` 이 무슨 컴포넌트인지 이름표를 본다**(`whatis`).
   같은 자리 `+0x358` 을 **마을 안 / 밖에서 각각 읽어** 정말 마을에서만 0 이
   아닌지 확인한다. 이 한 번으로 §5-1-1 (나)가 참인지 거짓인지 끝난다.
2. **매니저 전역을 확인한다** — `모듈+0x6C2E2F0` 을 떠서 `+0x08` 개수가 표
   행수(1007)나 최대 키(45006) 중 어느 쪽인지 본다. 그래야 배열을 **행으로**
   도는지 **키로** 도는지 정해진다.
3. `RegionInfo` 를 전부 훑어 **`_isTown=1` 인 구역 목록**과
   **`_forbiddenMercenaryKeyList` 가 비지 않은 구역 목록**을 낸다. 붉은 구역이
   그 목록과 맞는지가 §5-0-1 스크린샷으로 대조된다.
4. 그 다음에야 쓰기다. **1번 레버(살아있는 칸)를 토글로** 먼저 시험한다 —
   되돌리기가 제일 쉽고 세이브에 안 남는다.
5. 시간 제한 둘(`_callMercenarySpawnDuration` · `_callMercenaryCoolTime`)은
   지역과 무관하고 외부 선례가 확실하니 언제 해도 된다.

> `VehicleInfo._canCallSafeZone` 은 **우리 빌드에 없다**. 커뮤니티가 쓰는 칸이라
> 1순위로 뒀었지만 리플렉션 이름 목록에 안 나온다 — 후보에서 뺐다.

---

## 7. 관련

- `2026-09-16-dragon-external-research.md` — 드래곤 소환 해결 전문
- `2026-09-16-story-vehicle-wheel.md` — 정본(관문 지도)
- `../../STATUS.md` §1.21
