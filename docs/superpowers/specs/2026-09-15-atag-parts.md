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

## 4. 아직 안 본 것 (게임을 켜야 한다)

1. **A.T.A.G. 가 자기 `EquipSlotActorComponent` 를 가지는가.** 로그에 "장비 캐릭터
   후보 1개" 만 떴는데 그건 A.T.A.G. 를 안 부른 상태였다. 소환한 뒤 probe
   `equipchars` / `equip` 로 테이블이 하나 더 보이는지 본다.
2. **그 테이블의 슬롯 태그가 `Robot*` 해시인가.** 위 표의 해시와 맞춰 본다.
3. **연구가 장착을 막는가.** 파트를 지급만 하고 게임 UI 에서 껴 본다.
4. **현지화된 연구 항목 이름.** 게임을 켜면 현지화 표에서 바로 읽힌다.

## 근거

- 게임 데이터: `0008` 그룹 `gamedata/equiptypeinfo` · `gamedata/iteminfo` ·
  `gamedata/faction` (PAMT 색인 + LZ4 블록, 우리 `lz4blk`. `lz4` 파이썬 모듈이 없어
  커뮤니티 unpacker 는 못 쓴다 - 직접 푼다. 꺼내는 스크립트는 세션 스크래치패드의
  `pull.py` · 읽는 것은 `sit.py`).
- 실행 파일: RTTI 이름 · 문자열 스캔(`CrimsonDesert.exe` 1.0.0.2850).
- 스키마: `github.com/NattKh/CrimsonDesertModdingTools` 의 `pabgb_complete_schema.json`
  (434표). MIT.
- 웹: thegamer / gamerant / consolepulse 가이드 — 연구소 위치와 Mk 등급 이름.
