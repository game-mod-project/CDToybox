# A.T.A.G. 파트 업그레이드 — 시스템 조사 (2026-09-15)

A.T.A.G. 가 휠에서 소환·탑승까지 되게 된 뒤(`2026-09-15-vehicle-wheel-data.md` 계열),
그 **파트 업그레이드**를 오버레이로 다룰 수 있는지 조사했다. 게임 데이터 파일 ·
실행 파일 · 웹 셋을 봤다. **아직 라이브 검증 전인 부분을 절마다 표시했다.**

## 0. 한 줄 결론

**A.T.A.G. 파트는 별도 시스템이 아니라 평범한 "장비" 다.** `equiptypeinfo` 에 `Robot*`
장비 타입 열이 있고, `iteminfo` 에 그 타입을 쓰는 아이템이 **정확히 16개** 있다.
해금은 진영 연구소(`FactionResearch`)가 하지만, 그건 **아이템을 얻는 경로**일 뿐이다.

## 1. 두 층으로 되어 있다

### 1-1. 해금 — 진영 연구 (FactionResearch)

웹 가이드가 말하는 "Gorthak Ironworks 에서 Machine Parts Research 를 후원하면 ATAG
Boots Mk. II·III 와 Thrusterpack Mk. II·III 가 열린다" 는 것이 이 층이다.
실행 파일에 계통이 통째로 있다:

```
표(PABGB 스키마)  FactionResearchData          _researchName · _researchDesc
                                               _researchTotalProgress · _costItemList
                                               _progressDataList · _conditionOptionList
                                               _connectFactionNodeInfo · _dropSetInfo
                  FactionResearchProgressData  _onFailMaxValue · _condition
                  UpgradeActiveConditionData   _activeCondiiton(원문 오타)
                                               _deactiveReasonLocalString
서버 메시지        TrocTrResearchStartReq/Ack · TrocTrResearchCompleteReq/Ack
                  TrocTrUpdateFactionResearchProgressAck
조건              ConditionData_GetFactionResearchProgress
UI                UIGamePlayControlRoot_FactionResearch
                  UIGamePlayControl_Common_FactionResearchListItem
                  UIGamePlayControlFactionResearchTooltip · UIFactionResearchStateData
상점 UI 문자열     UI_NpcStore_ChangeCharacter_WarMachine
```

연구 항목 이름은 `_researchName` 이 **현지화 문자열 참조**(reader_8B)라 `faction`
표 본문에 ASCII 로 안 들어 있다. 이름을 뽑으려면 `localstringinfo` 나 실행 중
현지화 표를 봐야 한다 — 우리 모드는 이미 현지화를 읽으므로(표시명 7250개)
게임을 켜면 바로 읽힌다.

### 1-2. 장착 — `Robot*` 장비 타입

`gamedata/equiptypeinfo` 117종 중 A.T.A.G. 것이 **열 개**다. 옆에 말·펫·드래곤
타입이 같은 방식으로 있다(`HorseArmor`·`HorseSaddle`·`PetHelm`·`DragonArmor`·
`SpecialVehicleArmor`) — 즉 **탈것 장비는 전부 같은 뼈대**를 쓴다.

| 장비 타입 | 키(해시) | 웹 가이드의 이름 |
|---|---|---|
| `RobotBody` | `0x74EE4A67` | 본체 |
| `RobotFoot` | `0x12E0EB73` | ATAG Boots |
| `RobotBackPack` | `0xDA1D5D1A` | ATAG Thrusterpack |
| `RobotFist` | `0x2EE1B6AB` | Iron Fists |
| `RobotGatling` | `0x3F90399B` | machinegun |
| `RobotCannon` | `0xCA340DA8` | cannon |
| `RobotLaser` | `0xCDC67976` | laser |
| `RobotFlameThrower` | `0xC8E6257B` | flamethrower |
| `RobotTongs` | `0xBDE9F580` | 집게(수리) |
| `RobotWelding` | `0x0F97A49E` | 용접(수리) |

## 2. 파트 아이템 16개 — 전수

`gamedata/iteminfo` 6813개에서 `WarRobot` 로 시작하는 것이 16개이고, **열여섯 전부**
자기 레코드 안에 위 장비 타입 해시를 들고 있다(16/16 확인). `_01/_02/_03` 이
Mk I/II/III 다.

| 아이템 키 | 내부 이름 | 슬롯 |
|---|---|---|
| 1000261 | `WarRobot_Body_01` | RobotBody |
| 1001094 | `WarRobot_Body_02` | RobotBody |
| 1000758 | `WarRobot_Body_03` | RobotBody |
| 1001429 | `WarRobot_Foot_01` | RobotFoot |
| 1001096 | `WarRobot_Foot_02` | RobotFoot |
| 1000855 | `WarRobot_Foot_03` | RobotFoot |
| 1000475 | `WarRobot_Backpack_01` | RobotBackPack |
| 1001095 | `WarRobot_Backpack_02` | RobotBackPack |
| 1001068 | `WarRobot_Backpack_03` | RobotBackPack |
| 1002744 | `WarRobot_Fist_01` | RobotFist |
| 1002745 | `WarRobot_Gatling_01` | RobotGatling |
| 1002743 | `WarRobot_Cannon_01` | RobotCannon |
| 1002503 | `WarRobot_Laser_01` | RobotLaser |
| 1002501 | `WarRobot_FlameThrower_01` | RobotFlameThrower |
| 1001923 | `WarRobot_RepairTool_01_L` | RobotTongs |
| 1002099 | `WarRobot_RepairTool_01_R` | RobotWelding |

곁들여 나온 A.T.A.G. 관련 소비/퀘스트 아이템:
`1002632 Item_ETC_CooltimeReduce_ATAG` · `1003773 Item_ETC_CooltimeReduce_ATAGII`
(웹의 "instant cooldown reset" 이 이것) · `1002132 Item_KuKu_Small_ATAG`(제작 재료) ·
`1003657 Quest_Paper_KuKuATAG`.

## 3. 오버레이로 하는 방법 — 세 갈래

우리 모드에 이미 있는 것과 없는 것을 갈라 둔다.

### 갈래 A. 지급만 한다 (가장 쉽고 가장 안전)

파트 아이템을 인벤토리에 **지급**하고, 장착은 게임 UI 로 한다.

- **이미 있다.** `game/grant.cpp` 의 지급 경로 + 아이템 표(6813개)에 16종이 전부
  들어 있다. 키를 알고 있으니 목록만 만들면 된다.
- 세이브 생존도 기존 지급과 같다([[inventory-inplace-write]]).
- **위험 낮음.** 새 쓰기 경로가 없다.
- 한계: 연구가 안 끝났으면 게임이 장착을 거부할 수 있다(§4 미검증).

### 갈래 B. 착용 항목을 직접 바꾼다

`EquipSlotActorComponent` 의 착용 배열에서 항목 키(`WornPiece.key`, entry `+0x08`
하위 16비트)를 원하는 파트로 덮는다.

- **절반 있다.** `game/equip.cpp` 가 착용 배열을 찾고(`find_equip_table`),
  both-realms 로 소켓·담금질·연마·염색을 **쓴다**(`eq_write_all`, 인스턴스로 짝지음).
  같은 배관에 "항목 키" 하나를 더 다루면 된다.
- **없는 것**: 항목 키를 쓰는 writer, 그리고 A.T.A.G. 의 장비 테이블을 고르는 길
  (지금 `pick_player_table` 은 **플레이어 것**을 고른다 — 정신력 풀 + 조각 수 기준).
- 위험: 중간. 항목 키를 바꾸면 게임이 그 아이템의 부가 데이터(소켓 수·내구)와
  어긋날 수 있다. 먼저 **읽기**로 A.T.A.G. 테이블을 확인하고 나서 붙인다.

### 갈래 C. 연구를 끝난 것으로 만든다

`FactionResearch` 진행도를 직접 채운다.

- **없다.** 진행도를 들고 있는 런타임 구조를 아직 안 찾았다.
- 단서는 셋: `ConditionData_GetFactionResearchProgress`(읽는 쪽) ·
  `TrocTrResearchCompleteReq`(메시지) · `TrocTrUpdateFactionResearchProgressAck`.
- 우리 모드는 이미 **메시지 21종을 해독해 `msg <16진 와이어>` 로 보낼 수 있다.**
  `TrocTrResearchCompleteReq` 를 그 목록에 넣는 것이 가장 짧은 길이다.
- 위험: 중간~높음. 서버 메시지라 진행도가 세이브에 남는다(되돌리기가 어렵다).

**권고 순서: A → (필요하면) C → (그래도 안 되면) B.**
A 는 오늘 바로 되고, 연구 잠금이 실제로 막는지부터 A 로 확인된다.

## 4. 라이브 실측 (2026-09-15 저녁, A.T.A.G. 를 소환해 둔 상태)

### 4-1. A.T.A.G. 는 자기 장비 컴포넌트를 가진다 — 확인

`probe equip diag` 가 128개 중에서 잡았다:

```
[.?AVClientEquipSlotActorComponent@pa@@] comp=0x2480C5E3C00 pieces=3
    char=0x2480D7E2800 gauge=Y hpmax=2500000 player=0 row=6818
```

**행 6818 = A.T.A.G.**, 체력 250만, 조각 3개. 소환하기 전에는 이 컴포넌트가 없다
(그전 `equip diag` 에는 안 나왔다). 같은 스캔에 **행 6708 무역 마차**(조각 16,
체력 500만)도 있다 - 탈것들이 다 같은 방식으로 장비를 든다.

`equipchars 6818` 은 행을 받아들이지만(`선택 A.T.A.G.`) 후보 목록에는 안 뜬다.
후보는 `is_playable_character_row` 로 걸러 **플레이어블만** 남기기 때문이다
(클리프·데미안·웅카). 탈것을 다루려면 이 필터를 넓혀야 한다.

### 4-2. 그런데 우리 파서로는 못 읽는다 — 배치가 다르다

```
comp=0x2480C5E3C00 → equip table arr=0x248319784D0 cnt=3 stride=0xD8
```

플레이어는 stride `0xD0` 의 평평한 레코드인데 A.T.A.G. 는 `0xD8` 이고, 배열 자체가
레코드가 아니라 **`{u32, u32, 객체포인터}` 16바이트 묶음**이다:

```
+0x00  09 00 00 00 | 01 02 00 00 | 0x24832F60598
+0x10  02 00 00 00 | 01 02 00 00 | 0x24848BF7E00
+0x20  09 00 00 00 | 01 02 00 00 | 0x247A5433BF8
```

가리키는 곳은 vtable 로 시작하는 객체다(`0x1_4546F4B0`). 그래서 `read_worn_gear`
가 내놓는 `key=1432 temper=13046 slot=9984` 류는 **전부 오독이다 - 쓰면 안 된다.**

배치를 밝히려면 파트 아이템 **순번 6099~6114**(아래)를 표식으로 쓰면 된다. 이 열여섯
개는 `iteminfo` 표 순서에서 **연속 블록**이라 리틀엔디언 `d3 17`~`e2 17` 로 바로
눈에 띈다.

| 순번 | 아이템 | 순번 | 아이템 |
|---|---|---|---|
| 6099~6101 | Body_01~03 | 6108 | Fist_01 |
| 6102~6104 | Backpack_01~03 | 6109 | Cannon_01 |
| 6105~6107 | Foot_01~03 | 6110 | Laser_01 |
| 6111 | RepairTool_L | 6112 | Gatling_01 |
| 6113 | FlameThrower_01 | 6114 | RepairTool_R |

### 4-3. 파트 16종은 지급된다 — 확인

명령 파일(`cdtoybox_cmd.txt`)의 `give` 로 **16/16 전부** 인벤토리에 들어갔다.
연구소를 거치지 않고 아이템 자체는 얻어진다.

**함정 하나**: 명령 파일은 줄마다 2.5초를 쉬는데 **지급 대기열은 그보다 오래 걸린다.**
16줄을 그대로 넣으면 **한 줄 걸러 하나씩 "지급 거부(대기열/쿨다운)"** 가 난다. 사이에
아무 뜻 없는 줄(`nop`)을 끼워 5초로 벌리면 전부 통과한다 - 빈 줄과 `#` 주석은
`continue` 로 빠져서 간격이 안 벌어진다.

### 4-4. 아직 안 본 것

1. **연구가 장착을 막는가.** 지급된 16종을 게임 UI 에서 껴 본다. 여기서 갈래가 갈린다
   (끼워지면 연구소를 통째로 건너뛴다 · 막히면 갈래 C).
2. **A.T.A.G. 장비 배열의 실제 배치.** 4-2 의 표식으로 찾는다.
3. **현지화된 연구 항목 이름.**

## 5. 쿨다운 — 곁가지로 밝혀진 것

A.T.A.G. 재소환 쿨다운은 **50분**이고, 게임의 감소 아이템은 하나당 5~10분이라
열 개 가까이 써야 한다(사용자 실측). 쿨다운 아이템 네 개:
`1002632 ATAG` · `1003773 ATAGII` · `1002631 Dragon` · `1003772 DragonII`.

기존 "탈것 소환 쿨다운 풀기"(정적 표 `_callMercenaryCoolTime` → 1초)는 **이미 돌기
시작한 타이머에는 안 듣는다**(실측: 80개를 푼 뒤에도 막혔다). 남은 시간은 다른
곳에 있다.

명부 레코드 앞 0x200 을 45초 간격으로 떠서 비교했더니 한 바이트도 안 변했는데,
**이것으로 명부를 배제할 수 없다** - 남은 시간이 카운트다운이 아니라 "언제부터
가능" 이라는 **절대 시각**이면 가만히 둬도 안 변하기 때문이다. 제대로 보려면
**소환 직전과 직후**를 떠서 비교해야 한다(그때 새 마감 시각이 찍힌다).
## 근거

- 게임 데이터: `0008` 그룹 `gamedata/equiptypeinfo` · `gamedata/iteminfo` ·
  `gamedata/faction` (PAMT 색인 + LZ4 블록, 우리 `lz4blk`. `lz4` 파이썬 모듈이 없어
  커뮤니티 unpacker 는 못 쓴다 - 직접 푼다. 꺼내는 스크립트는 세션 스크래치패드의
  `pull.py` · 읽는 것은 `sit.py`).
- 실행 파일: RTTI 이름 · 문자열 스캔(`CrimsonDesert.exe` 1.0.0.2850).
- 스키마: `github.com/NattKh/CrimsonDesertModdingTools` 의 `pabgb_complete_schema.json`
  (434표). MIT.
- 웹: thegamer / gamerant / consolepulse 가이드 — 연구소 위치와 Mk 등급 이름.
