# 탈것·특수탈것·반려동물 — 목록과 오버레이 획득 경로 리서치 (2026-09-08)

주제: **오버레이에서 원하는 탈것·특수탈것·반려동물을 골라 얻는 것.**
기존 문서 전부를 다시 읽고, 게임을 켜지 않고 되는 정적 분석으로 새 길을
찾았다.

이 문서는 조사 기록이다. **구현·배포는 하지 않았다.** 게임이 꺼져 있어
라이브 검증은 한 건도 못 했으므로, 아래 "새로 연 것"은 전부 **정적 근거**
이고 "신규 후보"는 **가설**이다. 화면에서 보기 전까지는 가설이라는 것이
이 프로젝트의 반복된 교훈이다(`2026-09-08-companion-register-session.md` §6).

---

## 1. 구성 확인 (실측 2026-09-08)

| 항목 | 상태 |
|---|---|
| 저장소 | `E:\CDToybox`, 브랜치 `develop`, HEAD `91547d8` |
| 작업 트리 | 깨끗함 (추적 안 되는 참고 zip·CT 표만 있음) |
| 빌드 산출물 | `build\xinput1_4.dll` (2026-09-08 20:45) |
| 배포본 | 게임 `bin64\xinput1_4.dll` — **md5 동일** (`96c3ee39…`) 재배포 불필요 |
| 게임 | **실행 중이 아님.** probe 실측 불가 |
| 게임 exe | `CrimsonDesert.exe` 2026-09-04 13:56, 379MB |
| 정적 분석 | `tools/rtti/{find_class,fields,disasm}.py` 동작 확인 (Python 3.14.3) |

### 곁가지 결함 — 로그가 낙사 경고로 덮인다

`bin64\CDToybox.log` 728줄 중 **289줄(40%)** 이 같은 줄이다.

```
WARN  낙사: 사이트 시그니처가 유일하지 않다(0) - 설치 안 함
```

2.4초마다 재시도하며 매번 WARN 을 쓴다. 낙사 훅은 이 빌드에서 참고 AOB 가
0히트라 어차피 설치되지 않는다. **한 번 실패하면 그만두고 한 줄만 남겨야
한다.** 지금은 실제 조사 로그가 이 줄에 묻힌다. 고칠 자리는 `nofall` 설치를
재시도하는 루프다.

---

## 2. 목록 — 무엇을 얻을 수 있나

원본: `2026-09-05-catchable-companions.md` (라이브 메모리에서 추출,
캐릭터 표 7250행 × 용병 표 21행 교차). 사용자가 물은 세 갈래로 다시 접었다.

| 게임 안 이름 | 엔진 타입 | 캐릭터 수 | 그중 야생(포획 대상) |
|---|---|---|---|
| 탈것 — 말 | `Vehicle_Horse` (행 1) | 219 | **88** |
| 특수 탑승물 | `Vehicle_Special` (행 5) | 63 | **47** |
| 탈것 — 짐승 | `Vehicle` (행 6) | 69 | **35** |
| 반려동물 | `Pet` (행 9) | 230 | **229** |
| 가축 | `Domestic` (행 11) | 33 | 33 |
| 마차 | `Wagon` (행 8) | 75 | 0 |
| 물고기 | `Fish` (행 12) | 48 | 41 |
| 곤충 | `Insect` (행 13) | 76 | 0 |
| 드래곤 | `Vehicle_Dragon` | 1 | 0 |
| ATAG | `Vehicle_WarMachine(_Raptor)` | 3 | 0 |

- 판별식은 `CharacterInfo._mercenaryInfo`(+0xBE) ≠ 0xFFFF **그리고** 내부
  이름에 `_Wild`. `_isCatchable`(+0x148) 은 거의 모든 행이 1 이라 쓸모없다.
- `_Domestic`(마구간판)·`_Saddle`·`_Bagpack`·`Riding_*` 는 종이 아니라
  **상태 변형**이다. 목록에서 접어야 한다.
- **드래곤·ATAG·마차·곤충은 포획 대상이 아예 없다.** 스토리 부여·제작·구매다.
  "아무 종이나"를 목표로 잡을 때 여기가 천장이다.
- 전설마 3종(White 31377 / Black 31378 / RedHare 31379)은 목록에 있으나
  **2338 이 거부한다**(코드 `0x97AE29C9`, 실측 2026-09-06).

오버레이는 이 목록을 이미 보여 준다 — `roster_panel.cpp` 의 **동반자** 탭
(용병 타입 콤보로 접기) 과 **캐릭터** 탭. 표시명은 현지화 필드 `0x30`.

---

## 3. 획득 경로 — 지금 되는 것과 안 되는 것

문서 여덟 편(`2026-09-04-vehicle-pet-review` … `2026-09-08-companion-register-session`)
의 결론을 하나로 접는다.

| 경로 | 종 선택 | 상태 |
|---|---|---|
| **2338** `HireMercenaryToTarget` | **자유** | ✅ **되는 유일한 길.** 단 그 개체가 월드에 있어야 한다 |
| 2454 `HireMercenaryFromInventory` | 아이템이 결정 | ⚠️ 부적 6종이 한계. 인자가 (컨테이너, 슬롯) 이라 종을 못 고른다 |
| 2386 `CatchBySummon` | 부화체만 | ⚠️ 이름과 달리 야생 포획이 아니다. 알→둥지→5분 절차 전용 |
| 2988 `SpawnCharacterCheat` | 자유 | ⛔ **금지.** 몇 번은 돌지만 반복하면 게임이 멈춘다(2026-09-08 실측) |
| 용병 치트 3종 | — | ⛔ 이 빌드에서 몸통이 비어 있다 |

그래서 **남은 문제는 하나로 좁혀져 있다: 원하는 종을 월드에 띄우는 것.**
띄우기만 하면 2338 이 등록한다. 그리고 띄우는 유일한 알려진 수단(2988)이
금지선 너머에 있다. 이것이 이 세션 이전의 벽이다.

### 오버레이가 지금 제공하는 것

`roster_panel.cpp` 탭 6개:

- **동반자** — 동반자 타입 캐릭터 목록(타입별 필터)
- **근처** — 살아 있는 액터 목록 + 줄마다 **획득** 버튼(2338 구동) · **거두기**(2386)
- **탈것** / **용병 타입** / **캐릭터** — 정적 표 뷰어
- **동반자 아이템** — 부적 6종·알 4종 등 13개 지급 버튼(줄마다 실측/미확인 표시)

**없는 것:** 내가 가진 동반자 목록(소유 명부) 뷰. `2026-09-05-vehicle-pet-add-review`
§2.5 가 D안으로 제안했으나 "레코드 구조 미해독"으로 멈춰 있다.

---

## 4. 오늘 새로 연 것 (정적 근거)

### 4.1 2338 은 액터가 가리키는 "캐릭터 행 번호"를 그대로 쓴다 — 전 구간 확인

고용 작업 `0x2ADE280` 을 처음부터 끝까지 읽었다. 종을 정하는 값이
**액터 사슬에서만** 나온다.

```
0x2ADE3AB   rax = [대상액터 + 0x68]
0x2ADE3AF   rcx = [rax + 0x20]
0x2ADE3B7   cx  = word [rcx + 0x30]      ← 캐릭터 표 행 번호
0x2ADE3C9   call 0x383200                 행 → CharacterInfo 레코드
0x2ADE3CE   bx  = word [레코드 + 0x6A]     (탈것 전용 부속 키로 보임)
0x2ADE3E3   rcx = 레코드 + 0xBE            _mercenaryInfo
0x2ADE3EF   == 0xFFFF 면 동반자 아님 → 건너뜀
0x2ADE3F4   call 0x382E40                 용병 표 조회
0x2ADE3F9   [+0x20] == 2 (Vehicle) 이면 부속 등록 루프
   …
0x2ADE46F   ax  = word [[액터+0x68]+0x20 + 0x30]   ← 같은 행 번호를 다시
0x2ADE473   0x58바이트 레코드의 +0x00 에 넣고 배열에 append
   …
0x2ADE634   bx  = word [[액터+0x68]+0x20 + 0x30]   ← 또 같은 자리
0x2ADE665   r8d = bx
0x2ADE673   call 0x2097BC0                 ← 자격 검사(행 번호를 받는 그 함수)
0x2ADE785   call 0x26B4FD0                 ← 실제 등록 1단계(레코드 배열 전달)
0x2ADE825   call 0x26B5F80                 ← 2단계
```

즉 **행 번호가 세 곳에서 읽히고 셋 다 같은 사슬**이다. 액터의 겉모습·
스탯·이름이 아니라 이 u16 하나가 "무엇으로 등록되는가"를 정한다.
이 사슬은 `actors.{h,cpp}` 가 이미 읽고 있는 그 자리다.

### 4.2 거부 코드 두 개의 자리

문서가 "이름 미상"으로 남겨 둔 두 코드의 출처를 찾았다. 둘 다 런타임
초기화 전역이다.

| 전역 RVA | 언제 |
|---|---|
| `0x6BB7AE8` | 핸들로 액터를 못 찾았을 때 (`0x2ADE33C`) |
| `0x6BB8A18` | 액터 종류 바이트 `[[액터+0x88]+1]` 이 **4·5·6 이 아닐 때**, 또는 `[[액터+0x68]+0x118]+0x18` 이 0 이 아닐 때 (`0x2ADE622`) |

전설마·스토리 동료가 거부된 `0x97AE29C9` 는 자격 검사(`0x2097BC0`) 쪽이고,
부엉이의 `0xD65F8D70` 은 이 두 전역 중 하나일 가능성이 높다. **게임을 켜면
전역 두 곳을 읽어 1분 만에 확정된다.**

### 4.3 엔진에 리플렉션 시스템이 있다 — 세이브 데이터의 이름·오프셋을 얻는 길

이것이 오늘의 가장 큰 소득이다. RTTI 에 이런 클래스들이 그대로 있다.

```
ReflectMetaObjectBind<MercenarySaveData>        싱글턴 전역 0x6C53B50 (게터 RVA 0x18BF230)
ReflectMetaObjectBind<MercenaryClanSaveData>    전역 0x6C53A10 (게터 0x18BF2F0)
ReflectMetaObjectBind<MercenaryClanElementSaveData>
```

속성 바인드가 클래스별로 전부 이름을 달고 있다. `MercenarySaveData` 것만:

```
CharacterKey                ← 종을 정하는 값
CharacterAppearanceIndexKey
MercenaryNo                 ← 부르기(2894)가 쓰는 번호
staticstringA               ← 이름 문자열
FactionKey · FactionNodeKey · FieldInfoKey · EquipSlotNameKey · ItemNo
float3                      ← 좌표
Vector<ItemSaveData>        ← 장비
CustomizationSaveData · ExperienceLevelSaveData
u8 · u16 · u32 · u64 · float · bool 여럿
```

그리고 명부 전체 구조가 드러난다.

```
MercenaryClanSaveData
 ├ Vector<MercenarySaveData>                    사람 용병
 ├ Vector<MercenaryClanElementSaveData>         └ 그 안에 다시 Vector<MercenarySaveData>
 ├ Vector<MercenaryClanHyosiElementSaveData>
 ├ Vector<CallMercenaryCoolTimeSaveData>        ← 소환 쿨타임이 여기 산다
 ├ Vector<CallMercenarySpawnDurationSaveData>
 └ CharacterKey · u64 · u32 · u8
```

**뜻:** 동반자 하나하나가 `CharacterKey` 를 들고 저장된다. 지금까지
"레코드 구조 미해독"이라 멈춰 있던 소유 명부(D안)가 열린다. 속성 이름은
정적으로 뽑았고, **오프셋은 게임을 켜면 메타 객체를 걸어 읽을 수 있다** —
속성 객체 하나가 약 0x110바이트이고 타입 이름 문자열(`"CharacterKey"` 등)을
생성자에서 받는다(`0x18E3100` 확인).

### 4.4 `GameData_SummonCharacter` 에도 `CharacterKey` 가 있다

`2026-09-08-companion-register-session.md` §4 는 C안(아이템 사용 소환)이
**`SummonCharacterTrackNode` 의 살아있는 인스턴스가 0개**라 막혔다고 적었다.
그 옆에 못 본 것이 있었다.

```
GameData_SummonCharacter            ← 반사 등록됨. CharacterKey + CharacterGroupKey
GimmickEventHandlerData_SummonCharacter
GameData_TimelineEvent_SummonCharacter
TrocTrGimmickExecuteSummonCharacterFrameEventReq   ← 정상 게임플레이 메시지
```

`GameData_*` 는 이 엔진에서 **적재된 에셋 객체**다. 트랙 노드와 달리 상시
메모리에 있을 가능성이 있다. 있으면 종 → 키 대응을 읽을 수 있고, 나아가
게임 자신의 소환 경로(치트가 아닌 것)를 탈 수 있다.

### 4.5 아이템 사용 타입 26종 — 미조사 후보가 남아 있다

```
ItemUseData_ConvertCharacter      ← 캐릭터를 다른 것으로 바꾼다?  미조사
ItemUseData_FeedToTarget          ← 먹이 주기(친밀도)             미조사
ItemUseData_SummonCharacterWithCatch   (조사 중, 막힘)
ItemUseData_SummonGimmickWithCatch     ← 둥지 계열로 보임         미조사
ItemUseData_RegisterReserveSlot · SealToEquip · UseSealed · RandomBox …
```

`ConvertCharacter` 는 이름만으로 판단하면 안 되지만(교훈: 이름을 믿지
말 것), **살아있는 인스턴스 수와 필드를 세는 데 5분이면 된다.**

---

## 5. 신규 방법 후보

전부 **가설**이다. 우선순위는 (되면 목표가 끝남) × (실측 비용이 쌈) × (위험이 낮음).

### N-1. 소유 명부 제자리 종 교체 ★ 최우선

**아이디어.** 무해하고 확실히 되는 종(까마귀 30029)을 2338 로 등록한 뒤,
그 `MercenarySaveData` 레코드의 `CharacterKey` 를 원하는 종으로 **제자리
한 번 쓰기**로 바꾼다.

**왜 유망한가.**
- 이 프로젝트에서 **제자리 단일 쓰기가 리로드를 넘는 것이 이미 실측**돼
  있다(인벤 소켓·스탯). 명부도 세이브 데이터 클래스이므로 같은 성질일
  개연성이 크다.
- 소환 치트를 전혀 건드리지 않는다. 월드에 개체를 띄울 필요가 없다.
- 등록 자체는 게임의 정상 경로로 이미 끝난 상태다 — 우리는 이름표만 바꾼다.

**필요한 실측(게임 켜면 순서대로, 한 시간 안).**
1. `instcount MercenarySaveData` — 살아있는 인스턴스 수가 내 동반자 수와 맞나.
2. 맞으면 **알고 있는 값으로 오프셋을 역산한다** — 등록해 둔 개체의 캐릭터
   키를 이미 아니까, 인스턴스를 덤프해 그 값이 있는 자리가 `CharacterKey` 다.
   (리플렉션 메타를 걷는 것보다 이쪽이 훨씬 싸다.)
3. 버릴 세이브에서 값 하나를 바꾸고 → 목록·소환·리로드 확인.

**위험.** 종을 바꾸면 장비·외형·스탯이 어긋나 소환 시 죽을 수 있다.
반드시 버릴 세이브. 되돌리기는 게임의 **반려동물 풀어주기**.

### N-2. 액터 종 위장 후 2338

**아이디어.** 근처의 아무 액터(까마귀)의 `[[액터+0x68]+0x20]+0x30` u16 을
원하는 종의 행 번호로 잠깐 바꾸고, 2338 을 쏘고, 즉시 되돌린다.

**근거.** §4.1 이 그 u16 이 종을 정하는 **유일한** 입력임을 보여 준다.
자격 검사도 등록 레코드도 전부 그 값을 읽는다.

**왜 2순위인가.** 살아 있는 액터의 신원 필드를 건드리는 것이라 그 액터의
AI·렌더가 프레임 중간에 어긋날 수 있다. N-1 은 이미 굳은 데이터(명부)를
건드리는 것이라 더 얌전하다. 다만 **되면 종 제한이 사실상 사라진다** —
전설마·드래곤까지 시도해 볼 수 있다(자격 검사가 여전히 거부할 수는 있다).

**필요한 실측.** 쓰기 → 2338 → 즉시 복원. 로그로 검사 함수 결과 코드 확인.

### N-3. `GameData_SummonCharacter` 경유 정상 소환

**아이디어.** §4.4 의 에셋 객체를 찾아 `CharacterKey` 를 읽고(종 대응 확보),
나아가 게임의 김믹 소환 프레임 이벤트를 태워 개체를 정상 경로로 띄운다.
띄우면 2338 이 등록한다 — 즉 금지된 2988 을 **대체**한다.

**필요한 실측.** `instcount GameData_SummonCharacter` 한 줄. 0개면 이 길도
트랙 노드와 같은 운명이고, 그때는 버린다.

### N-4. 리플렉션 워커 (범용 도구, 부수 효과가 큼)

메타 객체 전역(`0x6C53B50` 등)에서 속성 목록을 걸어 **이름 → 오프셋** 표를
찍는 probe 명령. 한 번 만들면 명부뿐 아니라 앞으로 나오는 모든 세이브 데이터
구조에 쓴다. N-1 의 2단계를 "역산" 대신 "읽기"로 바꾼다.

**비용이 더 크므로 N-1 의 역산이 실패했을 때만 만든다.**

---

## 6. 권고 순서

게임을 켜고 **버릴 세이브**로:

1. 전역 두 개(`0x6BB7AE8`, `0x6BB8A18`)를 읽어 거부 코드 이름을 확정한다 (1분)
2. `instcount MercenarySaveData GameData_SummonCharacter ItemUseData_ConvertCharacter` (5분)
3. **N-1** 오프셋 역산 → 한 마리에 단일 쓰기 → 목록·소환·리로드 (1시간)
4. 되면 오버레이에 **소유 명부 탭**(D안) + 줄마다 "종 바꾸기"를 얹는다
5. 안 되면 **N-2** 로 간다. 그것도 아니면 **N-3**

**하지 말 것:** 2988 소환 치트 되살리기(`grant.h` 경고), 3022 구동,
`trace_char_path` 재현 호출.

---

## 6.5 실측 결과 (2026-09-09, 게임 실행 중) — 거부 코드를 확정했고 해석이 틀렸다

권고 순서 1번을 실행했다. 전역 둘을 읽으니 문서가 "이름 미상"으로
남겨 둔 거부 코드가 바로 나왔다 (모듈 베이스 0x140000000).

| 전역 | 값 | 문서에 있던 이름 |
|---|---|---|
| `0x6BB7AE8` | **`0xD65F8D70`** | 부엉이가 받은 코드 |
| `0x6BB8A18` | **`0x97AE29C9`** | 전설마·스토리 동료가 받은 코드 |

### 그래서 0x97AE29C9 는 "고용 불가 유형"이 아니다

`2026-09-06` 실측표는 전설마·Damian 을 "고용 불가 유형 추정"으로
적었다. 틀렸다. 그 코드가 나오는 자리(`0x2ADE622`)의 조건을 따라가서
그 필드를 직접 읽었다.

```
[[액터 + 0x68] + 0x118]  = ClientMercenaryActorComponent  (RTTI 확인)
   +0x10 u32 상태     0x7F010001 고용됨 / 0x00010001 미고용
   +0x18 u32 고용주  0 이어야 통과. 플레이어 쪽은 0xA0100001
   +0x20 u32 번호     미고용은 0xFFFFFFFF
```

| 액터 | `+0x18` | 결과 |
|---|---|---|
| 까마귀(대조) | 0 | 통과 |
| 전설마 흑마 | `0xA0100001` | 거부 |
| Damian | `0xA0100001` | 거부 |

**사용자가 흑마를 이미 소유 중이라고 확인해 줬다.** 즉 거부 이유는
종이 아니라 **임자가 있다**는 것이다. `0xD65F8D70` 도 마찬가지로
"거리·공중"이 아니라 **핸들로 액터를 못 찾았다**는 뜻이다.

### 목록에서 구분하게 넣었다

`game/actors.{h,cpp}` 에 `actor_owner_handle()` 과 `LiveActor::owned()` 를
넣고, 근처 탭에 **소유 열**을 추가했다. 임자가 있으면 획득 버튼을
막고 이유를 툴팁으로 말한다 — 누르고 나서 코드를 보는 일이 없다.

라이브 검증(probe `nearby`, 액터 248개): 소유로 잡힌 것은 7개이고
전부 실제로 내 것이거나 파티 동료였다.

```
Animal_Black_Horse_Wild_31378  전설마 흑마
Damian                          스토리 동료
NHM_Unique_Luke_661             유니크 동료
NHM_Unique_Ronald_665           유니크 동료
Animal_Baby_Wyvern_1 × 2         부화시킨 새끼 와이번
BlackWolf                       검은 늑대
```

야생 까마귀·비둘기·앵무는 전부 미소유로 남았고, 마을 NPC
(`NHM_Unique_Andrew_647`·`Marius_468`)도 미소유로 정확히 갈렸다.

### 이것이 계획을 어떻게 바꾸나

2338 의 관문이 세 개로 확정됐고 종 제한은 그 중에 없다.

1. 액터가 월드에 있고 핸들로 찾혀야 한다 (`0xD65F8D70`)
2. 액터 종류 바이트 `[[액터+0x88]+1]` 가 4·5·6 (흑마 5, Damian 4,
   까마귀 6 — **셀 다 통과한다**)
3. 고용주가 없어야 한다 (`0x97AE29C9`)
4. 그다음 자격 검사(`0x2097BC0`)가 용병단 타입별 한도를 본다

즉 **종을 가리는 관문은 없다.** 남은 문제는 여전히 1번 하나이고,
그것이 N-1·N-2 가 노리는 자리다.

---

## 7. 근거 파일

- 정적 분석 이번 회차: `tools/rtti/disasm.py` · `find_class.py` 로 수행.
  범위 디스어셈블러와 리플렉션 탐색은 임시 스크립트였다 — 다시 쓸 것 같으면
  `tools/rtti/` 로 승격할 것.
- 기존 문서: `2026-09-04-vehicle-pet-review` / `2026-09-05-vehicle-pet-add-review`
  / `2026-09-05-catchable-companions` / `2026-09-05-companion-summon-acquire-design`
  / `2026-09-05-virtual-amulet-review` / `2026-09-05-session-companion-handoff`
  / `2026-09-06-session-close` / `2026-09-07-companion-add-session`
  / `2026-09-08-companion-register-session`
- 코드: `src/game/companion.{h,cpp}` · `roster.{h,cpp}` · `actors.{h,cpp}`
  · `src/render/roster_panel.cpp`
