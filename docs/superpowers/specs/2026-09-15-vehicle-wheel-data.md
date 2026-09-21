# 탈것 휠 — 게임 데이터로 본 드래곤·ATAG 슬롯 (2026-09-15)

> **정리 (2026-09-21) — ❌ 처방 반증 (데이터 해독은 유효).** "메인 휠 허용 목록에
> 넣는다" 는 이미 간 길이다 — 아이콘은 올라가지만 소환은 그것으로 안 풀린다
> (`2026-09-16-dragon-external-research.md`, 정본 `2026-09-16-story-vehicle-wheel.md`
> §0-2). 인용한 커뮤니티 파서도 우리 데이터와 필드가 어긋났다. 1바이트 카테고리 읽기는
> 맞았다. 드래곤은 제 칸(7시)에서 풀렸다(`STATUS.md` §1.21).

`2026-09-15-dragon-summon-final.md` 가 "못 뚫었다" 로 닫은 뒤, **게임 데이터 파일**을
열어 다시 봤다. 앞선 조사 전부가 실행 파일과 런타임 메모리만 봤고 데이터 층은
"DMM 미설치 · pabgb 의 소환 필드 미규명" 으로 비워 둔 자리였다.

## 0. 한 줄 결론

**드래곤·ATAG 는 메인 탈것 휠의 허용 목록에서 빠져 있고 자기 전용 슬롯에만 들어간다.**
그 목록은 `gamedata/reserveslot` 표의 한 필드이고, 런타임으로는 앞선 조사가
"소유 타입 레지스트리" 라 부르며 이미 읽고 있던 바로 그 배열이다. 넣어야 할 곳이
드래곤 슬롯이 아니라 **메인 휠 쪽**이었다.

## 1. 게임 데이터 층을 열었다

`0008` 그룹(`0.pamt` + `0~2.paz`)에 정적 표가 **134개** 있다. 꺼내는 데 필요한 것은
둘뿐이다 — PAMT 색인 파싱과 **LZ4 블록 해제**(암호화는 XML 에만 걸린다).

```
gamedata/reserveslot.staticinfoheader     226 B   비압축
gamedata/reserveslot.staticinfobody     3,893 B   비압축
gamedata/characterinfo.staticinfobody   28.8 MB   LZ4
gamedata/stageinfo.staticinfobody       28.2 MB   LZ4
gamedata/conditioninfo.staticinfobody    1.8 MB   LZ4
```

표 파일 한 쌍의 형식:

```
header: u16 개수, 그다음 {u32 키, u32 본문오프셋} × 개수
body:   레코드가 오프셋 순서로 이어진다
레코드: u32 _key · u32 문자열길이 · 문자열 · u8 _isBlocked · …
```

**검증:** `characterinfo` 를 **7250/7250 항목 전부** 파싱했다(게임 안에서 세던 수와
같다). `Riding_Dragon_1`(키 1000799)의 `_callMercenarySpawnDuration` = **600초**,
`_callMercenaryCoolTime` = **3600초** — 넥서스 최대 인기 모드 "Unlimited Dragon
Flying"(11.6만 다운)이 "10분 강제 하차 / 60분 재소환 쿨다운" 이라고 적은 그 두 값이다.
파이프라인이 게임과 같은 것을 읽고 있다는 뜻이다.

커뮤니티가 PABGB 스키마(434표 · 3708필드)와 파서·PAZ 추출기를 공개해 두었다
(`github.com/NattKh/CrimsonDesertModdingTools`, MIT). 필드 이름이 전부 있어
`tools/rtti/fields.py` 가 못 뽑는 표도 읽힌다.

## 2. 탈것 휠은 슬롯이 셋이다

`gamedata/reserveslot` 28개를 전수 해독했다. 탈것 관련은 셋이다.

| 데이터 키 | 이름 | 메모(게임 원문) | 허용 카테고리 |
|---|---|---|---|
| 1000006 | `VehicleSlot` | 탈것 관련 처리 슬롯 | `0x4E` 일반 탈것, `0x51` 지상 차량 |
| 1000019 | `VehicleSlot_Mechanic` | 메카닉 탈것 관련 처리 슬롯 | `0x50` ATAG, `0x52` 기계 |
| **1000020** | **`VehicleSlot_Dragon`** | 용 탈것 관련 처리 슬롯 | **`0x4F` 드래곤 — 이것 하나** |

카테고리 ID 는 1바이트이고 뜻은 커뮤니티 파서가 이름표를 달아 두었다
(`0x4E` Normal Mounts · `0x4F` Dragon · `0x50` WarMachine/ATAG · `0x51` Land
Vehicles · `0x52` Mechanic). 그 파서의 주석이 우리가 찾던 문장을 그대로 적고 있다:

> `_enableVehicleList` on VehicleSlot entries controls which mount categories appear
> in the mount wheel. **Adding all vehicle hashes to VehicleSlot makes all mounts
> (dragon, ATAG, etc.) available from the main wheel.**

본문의 편집 자리(2850 기준, `reserveslot.staticinfobody`):

| 대상 | 오프셋 | 값 |
|---|---|---|
| `VehicleSlot` 개수 | 3519 | `02 00 00 00` |
| `VehicleSlot` 항목 | 3523~3524 | `4e 51` |
| `VehicleSlot_Dragon` 항목 | 3775 | `4f` |

크기를 안 바꾸는 최소 시험은 **3524번지 `51` → `4f`** 한 바이트다(지상 차량을 내주고
드래곤을 메인 휠에 올린다 — 헤더 재색인이 필요 없다).

## 3. 런타임에서 같은 것 — 앞선 조사가 이미 읽고 있었다

`2026-09-12-summon-wheel-research.md` 실측 8·9 가 게이트 `0x2ACA250` 을 따라가
"소유 타입 레지스트리" 라 부르며 읽은 것:

```
색인헤더 [0x6C2E300] (cnt 28) → 배열 [hdr+0x58] → 그룹 [배열+인덱스*8]
그룹 +0x58 = 타입행 배열, +0x60 = 개수
그룹 24 = [1, 5]   그룹 25 = [3, 4]   그룹 26 = [2]
```

이것은 **`ReserveSlotInfoManager`(개수 28)와 `ReserveSlotInfo._enableMercenaryList`**
다. 우리 빌드의 `fields.py` 가 `_enableMercenaryList +0x58` 을 그대로 준다.
데이터의 1바이트 카테고리가 로드 때 타입 행으로 옮겨진 것이고, 대응이 정확히 맞는다:

```
0x4E → 1     0x4F → 2     0x50 → 3     0x51 → 5     0x52 → 4
[0x4E,0x51] → [1,5]   [0x50,0x52] → [3,4]   [0x4F] → [2]
```

**즉 두 문서가 같은 자리를 다른 이름으로 보고 있었다.** 실측 9 는 "드래곤(2)이 이미
그룹 26 에 있으니 레지스트리 조작은 불필요" 로 닫았는데, 그룹 26 에 있다는 것이
곧 문제였다 — 26 은 전용 슬롯이고 메인 휠은 24 다.

벡터 배치는 로더(RVA 0x014865E0)가 확정해 준다 — `{ptr, +8 개수, +0xC 용량}`,
항목은 u16(게이트가 `cmp bx, [r8+rax*2]` 로 읽는다). 그래서
`_enableMercenaryList` 는 **+0x58 / +0x60 / +0x64** 다.

## 4. 곁가지로 확정된 것

- `0x2A22DE0`(휠 소환 프리미티브)은 진입에서 `ReserveSlotInfoManager` 의 이름해시 맵을
  조회하고(`r8` = 데이터 키 1000020), 뒤에서 **`0x200BDC0`(레코드 비었나)·
  `0x2014EA0`(레코드 조회)** 를 부른다 — 원소 작업이 밝힌 그 예약 슬롯 런타임 함수들이다.
  휠 소환은 플레이어의 `EquipSlotActorComponent` 예약 슬롯 레코드 위에서 돈다.
- `VehicleSlot_Dragon` 레코드에는 **조건(`_reserveSlotTargetList`)이 하나도 없다.**
  원소 슬롯처럼 `ConditionInfo` 로 잠긴 구조가 아니다.
- `StageChart_Function_HireMercenary { _changeMainMercenary, _isConfirmed,
  _hireTargetTag, _mercenaryType, _useMountingMercenary }` 가 실재한다(우리 빌드
  vtable RVA 0x586E088, 타입 번호 0x29). 스토리가 동반자를 지급하는 데이터 함수이고
  아직 안 판 길이다.

## 5. 외부 사실

- 드래곤은 **챕터 11**(Foreboding Shadow → 골든 스타 격파)로 열린다. 조기 해금의
  공개 선례는 없다.
- 커뮤니티에 JSON 데이터 모드 로더가 셋 있다(PhorgeForge JMM · DMM · CDUMM).
  데이터 모드는 `characterinfo.pabgb` 의 항목을 **이름·키·필드 단위로 set** 한다.
- 넥서스 3204 "Summon Any Character as Companion - Mount - Pet" 은 **작성자가
  해결 못 한 문제로 숨김 처리**했다. 1668 "All Mounts Unlock Start Save" 는 게임 내
  아이템으로 얻은 것들이고 드래곤이 아니다.

## 6. 우리가 넣은 것

`game::wheel_state` / `wheel_unlock`(`src/game/reserveslot.{h,cpp}`), 화면은
플레이어 치트 > "탈것 휠". 메인 휠의 `_enableMercenaryList` 에 드래곤·메카닉
슬롯의 값을 그대로 베껴 더한다(번호를 안 박는다). 용량이 남으면 제자리로 붙이고
모자라면 우리 정적 배열로 옮긴다. 개수는 맨 마지막에 올리고 되돌릴 때 맨 먼저
내린다. 정적 표라 세이브에 안 남는다.

## 7. 다음에 볼 것

1. **게임에서 확인** — 휠에 드래곤·ATAG 칸이 뜨는가, 뜨면 눌렀을 때 소환·탑승까지
   가는가. 안 되면 2로.
2. **예약 슬롯 런타임 레코드** — 작동하는 탈것 슬롯과 드래곤 슬롯의 레코드
   (`+0xC8`~`+0xEC`)를 `0x2014EA0` 로 떠서 비교한다. 원소가 `+0xEA` 한 칸으로
   풀렸듯 대응 칸이 있을 수 있다. 원소 쪽은 이 구조가 **세이브에도 남는 것**이
   확인됐다(`_specialNameKey`).
3. **데이터 모드** — §2 의 한 바이트를 로드 **전에** 적용해 본다. 앞선 조사가
   "런타임 편집이라 소환 표가 재구축 안 돼서일 수도" 라고 남긴 여지를 이것이 지운다.
4. **`StageChart_Function_HireMercenary`** — 스토리의 지급 경로를 그대로 부르는 길.
