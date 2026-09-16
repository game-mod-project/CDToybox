# 드래곤·A.T.A.G. — 게임 데이터·커뮤니티 조사 (2026-09-16)

정본 `2026-09-16-story-vehicle-wheel.md` 가 **"클라이언트 안에서는 더 좁힐
것이 없다"** 로 멈춘 자리에서, **게임 밖**(게임 데이터 파일 · 커뮤니티 도구 ·
공개 모드)을 뒤져 새 단서를 찾은 기록.

정본을 **대체하지 않는다.** 정본은 그대로 두고 이 문서는 **새로 얻은 것**만
싣는다. 라이브 검증은 아직 안 했다 — 어디까지가 실측이고 어디부터가 가설인지
절마다 밝힌다.

---

## 0-0. **뚫렸다 — 호출 경로가 처음으로 끝까지 갔다** (2026-09-16 18:04, 실측)

`Knowledge_CallDragon`(행 5025)을 레벨 1 로 쓰자 **드래곤 호출 모션이 나갔고,
요청과 관문이 전부 통과했다.**

```
18:03:47  지식 5025 번을 레벨 1 로 - realm 2개
18:04:27.600  등록조회(0x2096C30): 카테고리=2 -> 찾음 (종행 7008 · 번호 1000483)
18:04:27.613  동반자 캡처 [부르기/프레임] ID 2895 본문 29바이트   ← **처음**
18:04:27.616  소환게이트(0x2ACA250): 키=0x2 오류코드=0 (성공)      ← **처음**
```

**왜 결정적인가.** 정본이 남긴 마지막 벽이 *"7시 경로는 호출 모션이 아예 없고,
요청(`TrocTrFrameEventCallMercenaryReq` 2895)은 모션의 프레임 이벤트가 보내므로
요청 자체가 안 나간다"* 였다. 그 요청이 나갔다. 그리고
`소환게이트 키=0x2 오류코드=0` 은 **A.T.A.G. 가 월드에 나오기 직전 통과했던
바로 그 관문**이다(그때는 키=0x5, `2026-09-15-dragon-call-position.md` §1-1).

요청 본문에 카테고리 2 와 float3 좌표 `(-9891.1, 596.0, -5890.2)` 가 실려 있다 —
조립이 정상이다.

**사용자 관측(화면):** 전에는 7시 칸에 마우스를 올리면 이름 "블랙스타" 만 떴다.
지금은 **스탯·상태까지 뜨고 호출 동작과 소환 과정이 진행된다.**

```
사슬:  Knowledge_CallDragon 레벨 1
         -> Skill_CallDragon 이 붙는다
             -> 호출 모션이 나간다
                 -> 모션의 프레임 이벤트가 요청 2895 를 보낸다
                     -> 소환게이트 통과
```

### 0-0-1. 남은 것 — **스탯이 비어 있다**

소환이 완결되지 않는다. 휠 툴팁의 값이 정상이 아니다:

```
생명 1/0   ·   기력 (빈칸)   ·   공격력 0   ·   방어력 0   ·   젖음 상태
```

`최대 생명 0` 이면 스폰될 수 없다. **저장 → 종료 → 재시작으로도 안 바뀐다**
(실측) — 즉 로드 때 채워지는 것이 아니라 **데이터 자체가 없다.**

명부 레코드를 A.T.A.G.(되는 것)와 맞대 봤으나(probe `clandiff 1000483 1000602`)
차이가 대부분 **좌표 뭉치**라 스탯 자리가 드러나지 않았다. 드래곤 쪽에만
`+0x30`~`+0x50` · `+0xC4` · `+0xF4` 대역에 float 값이 차 있고 A.T.A.G. 는 0 이다.

**다음에 볼 곳** (아직 아무것도 확인 안 함):

| 후보 | 왜 |
|---|---|
| `TrocTrLoadMercenaryStatReq` | 이름 그대로 "용병 스탯 불러오기 요청". 1순위 |
| `TrocTrMercenaryDataListAck` · `TrocTrRefreshMercenaryDataAck` | 스탯을 실어 오는 응답 후보 |
| `GetListMercenaryFetchResultSet` | DB 조회 결과 집합 |
| `MercenarySaveData` | 세이브 쪽. 플레이 중엔 인스턴스가 0개라 파일로 봐야 한다 |

이것이 *"정규 습득이 레벨 표 말고 무엇을 더 채웠는지 모른다"* 고 적어 둔 그
자리일 가능성이 크다.

---

## 0. 새로 얻은 것 셋

| | 무엇 | 상태 |
|---|---|---|
| **A** | **호출 모션은 스킬이고, 그 스킬은 지식이 준다.** `Knowledge_CallDragon`(행 5025) → `Skill_CallDragon`(키 1506, "용 호출") | **게임 데이터에서 실측** · 게임 검증 **안 함** |
| **B** | 세이브 파일 층이 열려 있다(커뮤니티가 복호법 공개) | 외부 사실 · 우리가 열어 보지는 않았다 |
| **C** | 커뮤니티 세이브 에디터가 **"드래곤 해금" 을 판다** | 외부 주장 · 검증 안 함 |

**A 가 정본의 마지막 벽과 정확히 맞물린다.** 정본이 남긴 단 하나의 미해결은
*"7시 경로에 호출 모션이 아예 없다"* 였다. 그 모션이 **스킬**이고 스킬을
**지식**이 준다면, 없는 이유가 설명된다 — 그리고 우리는 이미 **지식을 쓰는
기능**을 갖고 있다(§1.19, 저장 생존 확인).

그리고 **부수 소득 하나** — 우리 문서의 "번호" 가 무엇인지 확정했다(§1).

---

## 1. 먼저: 우리 "번호" 의 정체 (실측 확정)

CDToybox 문서와 코드가 쓰는 지식·조건 **"번호" 는 게임 데이터 표의
`_key` 가 아니라 `0`-기준 **행 번호**다.** 두 표에서 교차 확인했다.

| 표 | 우리가 쓰는 번호 | 데이터 `_key` | 이름 |
|---|---|---|---|
| knowledgeinfo | **4883** | 1000802 | `Knowledge_MpFire` |
| knowledgeinfo | 4884 | 1000803 | `Knowledge_MpIce` |
| knowledgeinfo | 4885 | 1000804 | `Knowledge_MpLightning` |
| knowledgeinfo | 4886 | 1000805 | `Knowledge_MpWind` |
| conditioninfo | **9198** | 1009850 | `CheckEquipSlotName(Bracelet) && CheckKnowledge(Knowledge_MpFire)` |
| conditioninfo | 9199~9201 | 1009851~1009853 | 냉기·벼락·바람 |

STATUS.md §1.20 이 적은 네 번호·네 조건키와 **정확히 일치**한다. 즉 우리가
읽던 번호 체계가 맞고, 여기서 처음으로 **데이터 키와의 대응**이 붙었다.

**왜 중요한가.** 커뮤니티 도구와 공개 팩은 전부 `_key` 를 쓴다. 그것을 우리
API(행 번호)에 그대로 넣으면 **엉뚱한 항목**을 누른다 — 원소 휠에서 캐릭터
접두사 번호를 눌러 여러 번 헛돌았던 것과 같은 종류의 함정이다
(memory `element-wheel-reserve-slot`).

재현:

```bash
# 0008 그룹에서 표를 꺼낸다 (PAZ 도구는 lz4 · cryptography 필요)
python paz_unpack.py "<게임>/0008/0.pamt" --paz-dir "<게임>/0008" -o out --filter "*knowledge*"
# 헤더 = u16 개수 + {u32 키, u32 본문오프셋} × 개수   ← 배열 순서가 곧 행 번호
# 본문 레코드 = u32 _key · u32 이름길이 · 이름 · …
```

knowledgeinfo 는 **6,713행**, conditioninfo 는 **10,785행**(런타임
`ConditionInfoManager` 개수와 같다).

---

## 2. 단서 A — `Knowledge_CallDragon` (행 5025)

### 2-1. 무엇을 찾았나 (실측)

`gamedata/knowledgeinfo` 를 우리 빌드(2850)에서 직접 파싱했다:

| 행(0기준) | `_key` | 이름 | 현지화 |
|---|---|---|---|
| 5024 | 1000174 | `Knowledge_CallVehicle` | Summon Mount |
| **5025** | **1000175** | **`Knowledge_CallDragon`** | **Call Dragon** |
| 60 | 1000560 | `Knowledge_Unique_Varnia_Dragon` | Blackstar |

`conditioninfo` 에 **`CheckKnowledge(Knowledge_CallVehicle)`** 를 쓰는 조건이
실재한다. `Knowledge_CallDragon` 을 쓰는 조건식은 **없다**(0건).

### 2-1-1. **지식 → 스킬 사슬을 데이터에서 끝까지 이었다** (실측)

`KnowledgeInfo` 에는 `_learnApplySkillInfo` 가 있다(우리 빌드 **+0x104**,
`fields.py` 로 확인). 두 지식 레코드를 파싱하니 그 자리가 그대로 나왔다:

| 지식 | 행 | 키 | 가리키는 스킬 | 스킬 키 |
|---|---|---|---|---|
| `Knowledge_CallVehicle` | 5024 | 1000174 | **`Skill_CallVehicle`** | **1505** |
| `Knowledge_CallDragon` | 5025 | 1000175 | **`Skill_CallDragon`** | **1506** |

`skill` 표(2,061행)에서 두 스킬의 본문 문자열까지 확인했다:

| 스킬 | 행 | 이름 | 설명(게임 원문) | 키 바인딩 |
|---|---|---|---|---|
| `Skill_CallVehicle` | 1077 | 탈승물 호출 | "탈승물을 호출…" | `{Key:Key_CallVehicle}` |
| `Skill_CallDragon` | 1078 | **용 호출** | **"용을 호출한다."** | `{Key:Key_CallVehicle}` |

**둘이 같은 키에 물려 있다.** 즉 탈것 호출 키 하나가 두 스킬을 덮고,
드래곤 쪽은 `Skill_CallDragon` 을 가진 사람만 쓴다.

두 지식 레코드는 **구조가 같다**(이름 뒤 380바이트로 동일). 다른 곳은 넷뿐:

| 자리 | 드래곤 | 탈것 | 뜻 |
|---|---|---|---|
| +0x001 | `f0 cc 7f 41` | `73 e1 c5 ea`(없음 센티널) | UI 텍스처 해시 |
| +0x00e | `e2 05 00 00` = **1506** | `e1 05 00 00` = **1505** | **적용 스킬** |
| +0x022 | `01 01` | `00 00` | u8 깃발 둘(UI 알림 계열로 보인다 — 미확정) |
| 나머지 | 현지화 문자열 번호 | 〃 | 설명문 |

**사슬 가설(전부 실측 위에 서 있다):**

```
Knowledge_CallDragon(행 5025)을 안 배웠다
  └ Skill_CallDragon(1506)이 안 붙는다
      └ 7시 슬롯에서 호출 모션이 안 나간다          ← 정본이 관측한 그 증상
          └ 모션의 프레임 이벤트가 없다
              └ TrocTrFrameEventCallMercenaryReq(2895)가 안 나간다
                  └ 서버 realm 이 스폰할 일이 없다   ← 드래곤이 안 나온다
```

**대조군이 같은 표 안에 있다.** `Knowledge_CallVehicle` → `Skill_CallVehicle`
는 말이 잘 불리는 것으로 이미 검증돼 있다. 두 사슬이 한 글자만 다르다.

### 2-1-2. 드래곤 호출 전용 실패 메시지가 데이터에 있다 (실측)

`gamedata/failmessageinfo` 는 **11행뿐인 작은 표**인데, 절반이 드래곤·
와이번·메카닉 호출/탑승 실패 메시지다:

| 행 | 키 | 이름 |
|---|---|---|
| 0 | 1000002 | **`CallVehicleDragon_Owner`** |
| 1 | 1000003 | **`CallVehicleDragon_Mercenary`** |
| 2 | 1000000 | `KeepRidingDragon_Owner` |
| 3 | 1000009 | `KeepRidingWyvern_Owner` |
| 4 | 1000004 | `BoardVehicleDragon_Owner` |
| 5 | 1000008 | `BoardVehicleDragon_Mercenary` |
| 6 | 1000010 | `CallVehicleWyvern_Owner` |
| 7 | 1000001 | `CallVehicleMechanic_Owner` |
| 8 | 1000005 | `KeepRidingMechanic_Owner` |
| 9·10 | 1000006·1000007 | `Fishing_Fail` · `Fishing_NoSpace` |

커뮤니티 스키마는 이 표의 항목을
`FailMessageInfoData { _nakMessage, _conditionInfo }` 로 적고 있다 —
**Nak 메시지와 `ConditionInfo` 를 짝지은 것**이다. `CallVehicleDragon_Owner`
에는 그 짝이 **둘**, `CallVehicleWyvern_Owner` 에는 **하나** 들어 있다
(레코드 크기 102 vs 69바이트).

**아직 안 푼 것:** 그 두 짝의 `ConditionInfo` 키를 못 뽑았다(레코드 이진
배치 미해독). 뽑으면 **드래곤 호출이 정확히 무슨 조건을 보는지**가 평문으로
나온다 — 원소 휠을 풀었던 것과 같은 방식이다(STATUS §1.20).
**다음에 팔 자리 1순위다.**

### 2-1-3. 곁증거 — 조건 표의 비대칭

`conditioninfo` 를 이름으로 훑으면:

```
!IsHiredMercenary(Riding_WarMachine_Unique_1)      ← A.T.A.G. 전용 조건이 4건
CheckCharacterKey(Riding_WarMachine_Unique_1)
Condition_CallVehicle → CheckKnowledge(Knowledge_CallVehicle)   ← 지식 게이트 실재
```

**`Riding_Dragon_1` 을 쓰는 조건은 0건이다.** 드래곤 쪽 게이트는
`conditioninfo` 의 평문 조건식이 아니라 `failmessageinfo` 나 코드에 있다.

### 2-2. 왜 이것이 유력한가

정본이 남긴 단 하나의 벽은 **"7시 경로에 호출 모션이 없다"** 이고, 요청은
모션의 프레임 이벤트가 보낸다(`TrocTrFrameEventCallMercenaryReq` 2895).
그러니 **모션을 트리거할지 말지를 정하는 판정**이 남은 자리다.

- `VehicleInfo._riderSpawnUpperAction`·`_vehicleSpawnUpperAction` 은
  드래곤과 A.T.A.G. 가 **같은 값**이다(정본 §0-4). **모션 데이터는 있다.**
- 정적 표 넷(`MercenaryInfo`·`CharacterInfo`·`ReserveSlotInfo`·`VehicleInfo`)
  을 이름으로 맞대 봤더니 **차이가 없었다**(정본 §0-4).
- 남은 것은 **플레이어 상태**인데, "부를 줄 아는가" 를 담을 만한 자리가
  바로 이 지식이다.

**그래서 가설:** 드래곤 호출 모션(또는 그 앞 판정)이
`Knowledge_CallDragon` 을 본다. 지식이 없으면 모션이 안 나가고, 모션이
없으니 요청이 안 나가고, 요청이 없으니 아무 일도 안 일어난다 — 관측과
정확히 맞는다.

**이것은 가설이다.** 조건식에 안 나오므로 검사는 코드나 `.paz` UI 스크립트
층에 있을 것이고, 거기는 아직 안 봤다.

#### ⚠️ **레벨 표를 쓰는 것으로는 안 된다** (정정 2026-09-16, 레포 문서로 확인)

이 문서는 한때 §2-3 에 *"지식을 레벨 1 로 쓰고 모션을 본다"* 를 시험 절차로
적고 있었다. **틀렸다.** STATUS §1.19 의 「기능 관문을 찾았다(2026-09-14)」가
이미 답을 갖고 있었는데 읽지 않았다.

```
ServerKnowledgeActorComponent 전용 해시맵 (클라 쪽 같은 자리는 전부 0)
  +0xE8 버킷 수 · +0xF4 원소 수 · +0xF8 버킷 배열 · +0x100 값 배열
  키 = 지식 id · 값 = { u32 SkillKey, i32 레벨 }

진짜 습득 0x02AA55D0 이 이 맵에 넣는다
   (KnowledgeInfo._learnApplySkillInfo +0x104 에서 SkillKey 를 꺼낸다)
 → ServerSkillActorComponent vtable 슬롯 19 (RVA 0x02B26DE0)가 맵을 훑어
   스킬 컴포넌트에 등록한다                      ← 그때 기능이 켜진다

**레벨 표(우리 know_learn)는 이 맵을 안 건드린다.**
```

그래서 레벨만 쓰면 **화면은 "배운 것처럼" 보이고 스킬은 안 붙는다.**

#### 그러면 원소 휠은 왜 켜졌나 — 관문이 두 종류다

§1.20 에서 지식 4883~4886 을 레벨 표로 쓰자 원소 휠이 켜졌다. 위와 모순처럼
보이지만 아니다. **관문이 두 종류이고 우리 쓰기가 닿는 곳이 다르다.**

| 관문 | 무엇을 보나 | 레벨 표 쓰기로 되나 |
|---|---|---|
| **조건식** `CheckKnowledge(X)` | 레벨 표를 직접 읽는다 | **된다** (원소 넷이 이 경우) |
| **스킬 부착** `_learnApplySkillInfo` | 서버 해시맵을 읽는다 | **안 된다** (0x02AA55D0 이 필요) |

그리고 드래곤은 **두 번째 쪽**이다:

- `conditioninfo` 에 `CheckKnowledge(Knowledge_CallDragon)` 이 **0건**이다(§2-1-3)
- `Knowledge_CallDragon._learnApplySkillInfo` 가 **0xFFFF 가 아니라 1506** 이다

(`Knowledge_CallVehicle` 은 **둘 다** 갖고 있다 — `Condition_CallVehicle =
CheckKnowledge(Knowledge_CallVehicle)` 도 있고 `_learnApplySkillInfo` 도 1505다.)

**이것으로 §3.5 도 제대로 설명된다.** §3.5 의 *관찰*("레벨을 써도 휠이 안
켜진다")은 지금도 맞다. 틀린 것은 *원인*이고 — "휠 상태가 따로 저장돼서" 가
아니라 **레벨 표가 기능 관문 해시맵을 안 건드려서**다. 원소 건에서 번호를
잘못 골랐던 것은 그 위에 겹친 별개의 두 번째 문제였다.

#### 게임에서 쟀다 (2026-09-16 16:45~17:10, **쓰기 없음**)

읽기 전용 진단(`플레이어 치트 > 지식 > [호출 지식]`)을 넣고 돌렸다. 이름으로
찾은 번호가 **파일에서 계산한 행과 정확히 일치**했다(지식 6,713개도 같다).

| 지식 | 행 | 데이터 키 | **레벨** |
|---|---|---|---|
| `Knowledge_CallVehicle` (말 — **잘 불린다**) | 5024 | 1000174 | **1** |
| `Knowledge_CallDragon` | 5025 | 1000175 | **0** |

**작동하는 것과 안 되는 것 사이의 확인된 차이는 이 한 칸뿐이다.**

#### 서버 스킬 맵의 배치를 확정했다 — 그리고 호출 스킬은 그 맵을 안 탄다

첫 판은 값 배열을 `{u32 SkillKey, i32 레벨}` 로 **한 겹 얕게** 읽어 대조군까지
"없음" 으로 나왔다(넣어 둔 가드가 잡았다). 탐침으로 **게임을 다시 끄지 않고**
라이브에서 배치를 확정했다:

```
comp +0xE8 버킷수 2 · +0xF4 원소수 19 · +0xF8 버킷배열 · +0x100 **포인터 배열**
값[i] -> 포인터 -> +0x04 지식키 · +0x08 스킬키 · +0x0C 레벨
                  (+0x00 은 0 또는 31, +0x10·+0x18 은 시각으로 보이는 값 둘)
```

**검증:** `1000000 -> 10202 레벨 5` 가 게임 데이터의
`Knowledge_Wrestle -> Skill_Wrestle` 과 정확히 맞는다. vtable 도
`kServerCompVtableRva`(0x05A13200)와 일치해 **서버 컴포넌트가 맞다.**

> STATUS §1.19 의 *"값 = {u32 SkillKey, i32 레벨}"* 은 **한 겹 얕은 서술**이다.
> 정정 대상.

19개를 전부 읽은 결과:

| | |
|---|---|
| `Knowledge_CallVehicle` (키 1000174) | **맵에 없다** |
| `Knowledge_CallDragon` (키 1000175) | **맵에 없다** |
| 맵에 있는 19개 | 전부 전투·스킬트리 쪽(Wrestle · 15001~15003 · 13001 · 10xxx …) |

**그런데 말 호출은 잘 된다.** 그러므로 **호출 스킬은 이 맵을 안 탄다** — 맵에
있는 것은 이 건의 필요조건이 아니다.

이것이 앞 절의 정정을 다시 좁힌다. §1.19 의 "레벨 표로는 기능이 안 붙는다" 는
**스킬트리 지식에 대해서만** 참으로 보인다. 호출 지식 쪽에서는 기능이 되는
대조군이 **레벨 표에만** 있다.

> **아직 모르는 것:** 정규 습득이 레벨 표 말고 무엇을 더 채웠는지. 우리 쓰기가
> 그것까지 재현한다는 보장은 없다. "레벨만 맞추면 된다" 고 적지 않는다.

#### 그래서 바뀐 시험 경로

`0x02AA55D0(rcx = 서버 컴포넌트, dx = u16 지식키, r8d = i32 레벨, r9b = 1)`
을 부른다. 인자는 게임 자신의 호출부(RVA 0x02AA6450 / 0x02AA64F8)와 대조해
확정돼 있다 — 그 호출부 첫 줄이 `cmp word [rax+0x104], 0xFFFF`(붙을 스킬이
없으면 안 부른다)라, **`_learnApplySkillInfo` 가 판별자라는 것을 게임 코드가
그대로 확인해 준다.** 드래곤은 1506 이라 통과한다.

> **⛔ 렌더 스레드에서 부르면 죽는다.** ImGui 버튼에서 부르자 **네 번 다**
> 접근 위반이었고 맵 원소 수가 그대로여서 삽입 전에 튕겼다
> (TROUBLESHOOTING [1.13](../../TROUBLESHOOTING.md) · 근본 원인은 [1.8] TLS).
> 이 저장소의 **작업 디스패처 훅**(RVA 0x13A6750, 로그 "여기서만 실행한다")에
> 걸어 게임 스레드가 집어 가게 한다.
>
> **2026-09-14 기준 이 호출은 아직 성공을 확인하지 못했다**(STATUS §1.19).
> 즉 시험 전에 **호출 배관부터 세워야 한다** — 이것이 실제 다음 작업이다.

#### "휠은 지식과 별개다" 경고에 대해

TROUBLESHOOTING [3.5](../../TROUBLESHOOTING.md) 가 *"지식 레벨을 써도 원형
휠의 어두운 칸은 안 켜진다 — 휠 상태는 지식에서 파생되지 않고 따로 저장된다"*
고 적고 있다. 그대로 읽으면 이 가설을 막는 것처럼 보이지만, 두 가지가 다르다.

1. **§3.5 의 결론은 뒤에 뒤집혔다.** STATUS §1.20 에서 원소 휠이 켜졌고,
   안 켜졌던 이유는 "휠이 별개라서" 가 아니라 **번호를 잘못 골랐기 때문**
   이었다(접두사 붙은 가짜 넷을 눌렀다). 조건이 실제로 충족되자
   `.paz` UI 층이 **알아서 켰다.**
2. **우리가 노리는 것은 휠 칸이 아니라 스킬 부착이다.** §1.20 은 접두사 붙은
   원소 지식들이 `_learnApplySkillInfo == 0xFFFF` 라 **"붙는 스킬 자체가
   없다"** 고 적었다. `Knowledge_CallDragon` 은 그 자리가 **0xFFFF 가 아니라
   1506**(`Skill_CallDragon`)이다 — 붙을 스킬이 실재한다.

그래도 **갈릴 여지는 남는다.** 지식을 켜도 (a) 스킬이 안 붙거나, (b) 붙어도
7시 슬롯이 그 스킬을 안 부를 수 있다. 화면에서 **모션 유무**로 판정한다.

### 2-3. 시험 방법 (값이 **싸지 않다** — 배관을 먼저 세워야 한다)

> 앞 절대로 **레벨 표 쓰기로는 안 된다.** 아래는 그것을 반영한 순서다.

```
0. [읽기] **끝났다**(위). 레벨 1 대 0 이 남는 유일한 차이이고,
   서버 스킬 맵은 이 건의 관문이 아니다
1. [쓰기] know_learn 으로 Knowledge_CallDragon(행 5025)을 레벨 1 로 쓴다.
   대조군이 레벨 표에만 있고 기능이 되므로 이 경로가 맞을 만하다.
   0x02AA55D0(해시맵 등록)은 **지금은 필요 없어 보인다**
2. 게임에서 저장 → 7시 슬롯에서 드래곤을 누른다
3. **모션이 나오는가**를 본다. 로그가 아니라 화면으로 본다 -
   모션 유무는 우리 훅에 안 잡힌다(TROUBLESHOOTING 4.29 의 교훈)
4. 전후 비교는 **같은 슬롯 번호**에서. 로그 `슬롯u16=` 26(7시) / 24(메인 휠)
```

**되돌리기를 먼저 정해 두고 시작한다** — 지식 쓰기는 세이브에 남는다.
세이브 파일 백업(§3 의 경로) + 레벨 0 쓰기로 돌린다.

**판정 규칙** — 증상이 사라진 것을 고쳐진 것으로 읽지 않는다. 시험 전후로
**같은 슬롯 번호**에서 재고, 로그의 `슬롯u16=` 값을 눌러 확인한다.
26(7시)과 24(메인 휠)는 다른 경로다.

### 2-4. 곁가지 — 블랙스타 스킬 지식 11개

커뮤니티 팩 `Black Star Dragon Attack MoveSet` 에 드래곤 기술 지식이 있다.
**게임 안에서 드래곤이 무엇을 할 수 있는지**를 정하는 쪽이고 소환과는
별개지만, 이름이 드래곤의 내부 코드명을 알려 준다 — **`TriStar`**.

```
Knowledge_Skill_TriStar_Attack / _Evade / _Breath / _StaminaRegen
Knowledge_Skill_TriStar_Attack_Fire / _Ice / _Lightning  …  (11개, 키 1004189~)
```

`Knowledge_MasterDooTest_TriStar`("Master Du's Training: Urdavah")도 있다 —
드래곤을 처음 타는 챕터 9 장소다. **`TriStar` 로 다시 훑으면** 우리가
`Dragon` 으로만 찾다 놓친 것이 나올 수 있다. 아직 안 했다.

---

## 3. 단서 B — 세이브 파일 층 (우리가 한 번도 안 만진 곳)

### 3-1. 있다 (실측)

```
C:\Users\<사용자>\AppData\Local\Pearl Abyss\CD\save\<steamid>\slot<N>\save.save
```

이 기기: `59132039` 아래 slot0~slot107, `save.save` 가 1.5~1.6MB.
머리 4바이트가 `SAVE`, 그 뒤는 고엔트로피.

```
00000000  53 41 56 45 02 00 80 00  00 00 00 00 02 00 00 00   SAVE............
```

### 3-2. 커뮤니티가 열어 뒀다 (외부 사실, MIT)

`github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS` 에
`save_crypto.py` · `ben_save_decrypt.py` · `parc_*`(PARC 컨테이너 파서·
직렬화기) · C++ 판(`save_parser_cpp` · `save_writer` · `save_repair`)이
전부 있다. 크레딧에 복호(`@gek`)·압축해제(`@MrIkso`) 기여가 적혀 있다.

### 3-3. 왜 중요한가

`2026-09-16-client-server-architecture.md` §2-3 대로, **저장되는 것은 서버
realm 의 SaveData 직렬화 결과**다. 메모리를 고쳐도 안 남는 값들(가방 용량
§1.16 · 스킬 포인트 §1.17)이 여기 있다. 그리고 `MercenarySaveData` 는
**플레이 중 인스턴스가 0개**라 제자리 쓰기를 걸 대상이 아니다(memory
`engine-reflection-savedata`) — 즉 **파일 쪽에서만 닿는 것**이 있다.

### 3-4. 주의

- 세이브 편집은 **게임을 끈 상태**에서 한다. 백업은 필수.
- 우리 모드의 철학(런타임 메모리)과 다른 층이다. 손대기 전에 **무엇을 위해
  여는지** 정하고 시작한다 — 지금 후보는 "드래곤 소유·해금 상태가 파일에서
  어떻게 다른가" 를 **읽기만** 하는 것이다.
- 우리가 아직 **열어 보지 않았다.** 위는 전부 외부 정보다.

---

## 4. 단서 C — 커뮤니티가 이미 드래곤을 해금한다 (외부 주장)

### 4-1. 무엇을 말하고 있나

Nexus **SWISS Knife Save Editor**(mods/20, 작성자 RicePaddy/NattKh,
추천 1,855 · 내려받기 21만)의 설명에 이렇게 적혀 있다:

> **Mount Unlock System** — Unlock Dragon (Blackstar) — **with or without
> quest flags** · One-click unlock, no quest needed ·
> **Dev Mode: 21 experimental mounts (Elephant, Camel, Wyverns, ATAG, and more)**

정리하면:

| 주장 | 우리 조사와의 관계 |
|---|---|
| 드래곤을 **세이브 편집으로** 해금한다 | 우리는 런타임만 팠다. **다른 층이다** |
| **퀘스트 플래그와 무관하게** 된다 | "스토리 잠금이라 못 한다" 가설을 더 약하게 만든다 |
| A.T.A.G.·와이번은 **실험적**으로 분류 | 우리가 A.T.A.G. 를 뚫은 것과 앞뒤가 맞는다 |

### 4-2. 믿을 것과 안 믿을 것

**믿을 만한 것** — 그 도구가 세이브 파일을 실제로 읽고 쓴다는 것(소스 공개,
이 기기의 파일 구조와 경로가 설명과 일치).

**검증 안 된 것** — "드래곤이 해금된다" 가 *무엇을* 뜻하는지.
휠에 뜨는 것까지인지, 실제로 소환·탑승되는지, 아이콘만인지 **모른다.**
모드 설명은 광고문이고, 우리는 같은 문장에 두 번 속았다(정본 §5).
**직접 재기 전에는 근거로 쓰지 않는다.**

### 4-3. 곁가지 사실

- 드래곤의 정규 해금은 **챕터 11**(Foreboding Shadow). 챕터 9 에서 처음
  탄다. 조기 해금의 공개 선례는 **세이브 파일 배포**(Nexus 44 · 395 · 671)
  뿐이고, *실행 중 메모리로 푼* 선례는 여전히 못 찾았다.
- `Unlimited Dragon Flying`(11.6만 다운)은 **10분 강제 하차 / 60분 쿨다운**
  두 값을 푸는 데이터 모드다 — 우리가 `characterinfo` 에서 읽은
  `_callMercenarySpawnDuration` 600초 · `_callMercenaryCoolTime` 3600초와
  같은 값이다(파이프라인 교차 검증).

---

## 5. 커뮤니티 `reserveslot` 파서는 **우리 빌드와 안 맞는다** (실측)

`CrimsonDesertModdingTools/reserveslot_parser.py` 의 머리말이 이렇게 적고
있어 한때 유망해 보였다:

> `_enableVehicleList` on VehicleSlot entries controls which mount categories
> appear in the mount wheel. Adding all vehicle hashes to VehicleSlot makes
> all mounts (dragon, ATAG, etc.) available from the main wheel.

**그 파서를 우리 2850 데이터에 돌려 봤다. 필드가 어긋난다.**

| 슬롯 | 파서 출력 | 실제(우리 읽기) |
|---|---|---|
| 1000006 `VehicleSlot` | `vehicle=[20814, 0]` | `[0x4E, 0x51]` (0x514E 를 u16 로 읽은 것) |
| 1000019 `VehicleSlot_Mechanic` | `vehicle=[21072, 0]` | `[0x50, 0x52]` |
| 1000020 `VehicleSlot_Dragon` | `vehicle=[79]` | `[0x4F]` |

파서는 항목을 **u16** 으로 읽는데 2850 은 **u8** 이다. 파서 머리말도
*"27 entries as of game version 1.0.0.4"* 라고 적고 있고 우리는 **28개**다.

그리고 실행 파일의 `fields.py` 로 뽑은 우리 빌드의 `ReserveSlotInfo` 에는
**`_enableVehicleList` 가 없다**:

```
_stringKey +0x8 · _isBlocked +0x10 · _timeLimit +0x18 · _coolTime +0x20
_memo +0x38 · _reserveSlotType +0x40 · _usingType +0x41
_enableTribeList     +0x48
_enableMercenaryList +0x58      ← 우리가 쓰는 그 목록
_enableReserveSlotList +0x78 · _enableItemGroupList +0x88
_reserveSlotTargetList +0x98 · _isSelfPlayerOnly +0xAC
```

**결론:** 커뮤니티가 `_enableVehicleList` 라 부른 것이 우리 빌드의
`_enableMercenaryList`(+0x58)이고, **우리는 이미 그 목록을 만지고 있다**
(`reserveslot.cpp` 의 `wheel_unlock`). **새 레버가 아니다.**
우리 문서의 1바이트 읽기(`0x4E`/`0x4F`/`0x50`/`0x51`/`0x52`)가 맞았다.

> 기록해 두는 이유: 이 파서 머리말은 "메인 휠에 다 올릴 수 있다" 고 읽히고,
> 다음 사람이 그것을 **새 길**로 착각하기 쉽다. 아니다 — 이미 간 길이고,
> 아이콘은 올라가지만 소환은 그것으로 안 풀린다(정본 §0-2).

---

## 6. 안 본 곳 (다음 세션에)

우선순위 순.

1. **`Knowledge_CallDragon`(행 5025)을 켜고 7시 슬롯에서 모션을 본다.**
   가장 싸고, 되면 그 자리에서 끝난다. §2-3.
   먼저 **읽기만** — `Knowledge_CallVehicle`(행 5024)이 배워져 있는지 보면
   대조군이 선다(말이 불리니 켜져 있어야 정상이다).
2. **`failmessageinfo` 의 `CallVehicleDragon_Owner`·`_Mercenary` 레코드를
   푼다.** 항목이 `{_nakMessage, _conditionInfo}` 이므로, 이진 배치만 풀면
   **드래곤 호출 조건이 평문으로 나온다.** §2-1-2.
3. **받는 쪽(서버 realm) 계측.** 지금까지 훅은 전부 요청 *조립* 쪽이었다.
   `ServerMercenaryClanActorComponent` · `ServerFrameEventCallMercenaryReservedSlot`
   · `TrocTrCallVehicleMercenaryAck` 조립 자리.
   구조 근거: `2026-09-16-client-server-architecture.md` §5.
4. **세이브 파일을 읽기 전용으로 연다.** 드래곤 소유·해금이 파일에서 어떻게
   보이는지. §3.
5. **`.paz` UI 스크립트 층.** 휠 칸을 어둡게 그리는 최종 판정이 여기 있다는
   것은 원소 휠에서 이미 확인됐다(STATUS §1.20). 호출 모션을 거는 자리도
   여기일 수 있다.

**다시 파지 말 것** (정본 §0-2 · TROUBLESHOOTING §9):
등록 항목을 바꿔 부르는 길. 휠 배치는 등록 항목의 종행이 아니라 **그 종의
정적 카테고리**를 본다.

### 6-1. 이번에 인게임 확인을 못 한 이유 (기록)

게임이 돌고 있어 `scripts/overlay-check.ps1` 로 오버레이를 열고
플레이어 치트 > 지식까지는 갔다(스크린샷 확인, `지식 6713개` — **파일에서
센 6,713행과 일치**). 거기서 멈췄다:

- 1분쯤 지나자 모드가 **raw input 모드로 전환**했다(로그:
  `창 마우스 메시지가 끊겼다 … raw input 으로 버튼·휠을 넣는다`). 그 뒤
  PostMessage 클릭이 안 먹고 오버레이가 꺼졌다.
- 지식 절의 버튼 넷(`선행 조건 훑기`·`진단`·`휠 진단`·`이름 진단`)은 전부
  **읽기 전용**이지만, **특정 지식을 이름으로 찾아 레벨을 보는 길이 없다.**
  원소 절은 넷을 이름으로 찾아 보여 주는데, 같은 것이 일반 지식에는 없다.

**그래서 다음에 필요한 것:** 지식 절에 *이름으로 찾아 레벨 보기* 를 붙이거나,
`know_diagnose_names` 에 번호를 직접 넣을 입력 칸을 붙인다. 지금은
`선행 조건 훑기` 결과의 첫 항목만 찍는다.

**주의:** 지식 쓰기는 **세이브에 남는다**(§1.19). 시험은 사용자 동의를 받고,
되돌릴 방법(`knowledge_keep` 목록 비우기 · 세이브 백업)을 먼저 정하고 한다.

---

## 7. 이번에 쓴 도구

```
# PAZ 추출 (이전 세션 산출물을 재사용, 커뮤니티 MIT 도구)
paz_tools/paz_unpack.py   ChaCha20 복호 + LZ4 해제
  필요: pip install lz4 cryptography     ← 이번에 lz4 4.4.5 를 설치했다

# 실행 파일 정적 분석 (우리 것)
tools/rtti/fields.py <exe> ReserveSlotInfo     필드 이름 -> 오프셋
grep -a -o '\.?AV[A-Za-z0-9_]*@pa@@' <exe>     RTTI 클래스 이름 5,091개
```

꺼낸 표(이 세션 임시 폴더, 레포에 안 넣음):
`knowledgeinfo` · `knowledgegroupinfo` · `conditioninfo` · `reserveslot`.

---

## 8. 출처

| | |
|---|---|
| 커뮤니티 모딩 도구(MIT) | `github.com/NattKh/CrimsonDesertModdingTools` |
| 세이브 에디터 소스(MIT) | `github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS` |
| SWISS Knife Save Editor | `nexusmods.com/crimsondesert/mods/20` |
| Unlimited Dragon Flying | `nexusmods.com/crimsondesert/mods/356` |
| 드래곤 해금 챕터 | `game8.co/games/Crimson-Desert/archives/587115` |

---

## 9. 관련 문서

- `2026-09-16-story-vehicle-wheel.md` — **정본.** 결론과 관문 구조
- `2026-09-16-client-server-architecture.md` — 이 게임의 realm 구조
- `2026-09-15-vehicle-wheel-data.md` — 게임 데이터 층 최초 개척
- `../../STATUS.md` §1.19 §1.20 §1.21 · `../../TROUBLESHOOTING.md` §4.29 §8 §9
