# 드래곤 "호출할 수 없는 위치" — 배제한 것과 다음 한 걸음 (2026-09-15)

A.T.A.G. 는 휠 소환·탑승·파트 장착까지 **전부 된다**. 같은 처리를 받은 블랙스타만
`호출할 수 없는 위치입니다` 로 막힌다. 이 문서는 **무엇이 범인이 아닌지**를
실측으로 못박아, 다음 세션이 같은 땅을 다시 파지 않게 한다.

## 0. 한 줄

**장소 관련 정적 데이터는 다 소진했다.** 거부는 요청을 조립하기도 전에 클라이언트
안에서 끝나므로, 다음은 정적 표가 아니라 **조건 객체**를 봐야 한다.

## 1. 확정된 사실

### 1-1. 게이트 `0x2ACA250` 은 진짜 경로 위에 있다 (앞선 기록 정정)

`summon-wheel-real-path` 메모와 앞선 스펙이 "게이트는 무효 경로(2962 서버 메시지
에서만 불린다)" 라고 닫아 두었는데 **틀렸다.** 호출자를 디스어셈블하면 계약이
그대로 보인다:

```
0x2ADBB96  movzx r9d, bx        ; r9 = 동반자 카테고리 (우리 로그의 "키=0x5")
0x2ADBB9A  mov   r8, rdi
0x2ADBB9D  lea   rdx, [rbp+0xc70]
0x2ADBBA4  call  0x2ACA250      ; 게이트
0x2ADBBA9  mov   eax, [rax]     ; 반환 포인터의 첫 dword = **오류 코드**
0x2ADBBAB  test  eax, eax
0x2ADBBAD  je    0x2ADBBE7      ; 0 이면 계속 = 성공
0x2ADBBAF  mov   [rsi], eax     ; 아니면 오류 코드를 내보내고 빠져나감
```

A.T.A.G. 소환이 `키=0x5 결과=0` 으로 지나갔고 **1초 뒤 월드에 나왔다**(실측
19:51:28→29). `결과=0` 은 "안 됐다" 가 아니라 "오류 없음" 이었다.

### 1-2. 드래곤은 그 게이트에 닿지도 않는다

드래곤을 누른 시각 전후로 **우리 훅이 하나도 안 찍힌다** - 동반자 캡처 11경로 ·
게이트 `0x2ACA250` · 스폰 `0x2A22DE0` · 알림 `0xFB7F060`(msg 0x3F5) 전부 조용하다.
즉 거부는 **요청 조립 이전, 클라이언트 자체 판정**이다. 토스트도 그 판정이 낸다.

## 2. 범인이 아닌 것 (전부 실측 배제)

| 후보 | 어떻게 배제했나 |
|---|---|
| `CharacterInfo._callMercenarySpawnVoxelType`(+0x448, 개수 +0x450) | 1→0 으로 쓰고 **유지 확인**(`+0x450 = 0`). 그래도 막힌다. 오프셋도 `fields.py` 로 재확인 |
| `VehicleInfo._checkDistanceToGround`(+0x8C) | 30→0. 그래도 막힌다 |
| `VehicleInfo._escapeRoadGroupType`(+0x70) | 드래곤 2 → 와이번/ATAG 와 같은 5. 그래도 막힌다 |
| `VehicleInfo._maxAllowableHeight`(+0x9C) | 드래곤·와이번 **둘 다 1350**. 차이가 아니다 |
| `VehicleInfo._callVehicleVoxelType`(+0xA4 묶음) | 드래곤·와이번 **둘 다 0**. 차이가 아니다 |
| `CharacterInfo._callVehicleGimmickInfo`(+0x442) | 드래곤·ATAG·와이번 **전부 0xFFFF**(없음) |
| 동반자 카테고리 `_mercenaryInfo`(+0xBE) | 5(특수 탑승물)로 바꿔도 막힌다. 같은 값의 A.T.A.G. 는 된다 |
| CharacterInfo 가 서버·클라 두 벌 | `types CharacterInfo` - **`CharacterInfoManager` 는 싱글턴**이다 |
| 명부 레코드의 무언가 | 1024B 를 90초·15분 간격, 아이템 사용 전후로 떠서 **전부 무변화** |

## 3. 다음 한 걸음 — 조건 객체

실행 파일 문자열에서 나온 이름들이 방향을 준다:

```
AICondition_CheckCallVehicleSpawnVoxelType      ← 복셀 검사가 **조건 클래스**다
ConditionData_CheckMercenaryCallCooltime
ConditionData_GetFactionResearchProgress
eErrNoCallVehicleInvalidPosition                eErrNoCallVehicleInvalidPosition_sequencerEvade
eErrNoInvalidCallMercenarySpawnPositionType     eErrNoCallVehicleMercenaryMovableNavigation
eErrNoCallVehicleInvalidAir/Ground/Water/Altitude/LimitRide/QuickSlot
UI_MercenaryQuickSlot_NonCallable_AccompanyNotAllowed
```

즉 복셀 검사는 `CharacterInfo` 를 직접 읽는 코드가 아니라 **`AICondition_*` 객체**로
되어 있다. 우리가 `CharacterInfo` 쪽 개수를 0 으로 만들어도, 조건 객체가 자기
설정값을 들고 있으면 그대로 돈다 - 관측 결과와 정확히 맞는다.

이 모드는 이미 **`ConditionInfoManager`(RVA `0x06C2F260`)** 를 읽는다
(`reserveslot.h` - 예약 슬롯의 원소마다 `{u16 SpecialName, u16 ConditionInfoKey}`).
`gamedata/conditioninfo` 표도 꺼내 놨다(1.8MB).

**권하는 순서**

1. RTTI 로 `AICondition_CheckCallVehicleSpawnVoxelType` 인스턴스를 찾아 배치를 본다
   (probe `objects` / `instances`). 설정된 복셀 타입이 보이면 그게 답이다.
2. 그 클래스의 vtable 평가 함수에 훅을 걸어 **드래곤을 누를 때 불리는지**, 인자와
   결과가 무엇인지 찍는다. 지금 우리 훅은 이 층을 아예 안 보고 있다.
3. 안 불리면 `eErrNoInvalidCallMercenarySpawnPositionType` 등 오류 이름값을 찾아
   그 상수를 쓰는 코드로 거슬러 올라간다.

## 4. 곁들여 확정된 것

- A.T.A.G. 재소환 쿨다운은 **60분**(`_callMercenaryCoolTime` 3600초), 탑승 제한 10분.
  드래곤도 같다.
- **"소환 쿨다운 풀기" 는 이미 돌기 시작한 대기에 안 듣는다.** 정적 값을 1초로
  바꿔 둔 상태에서도 42분이 그대로 돌았다. 마감 시각을 부를 때 따로 박아 둔다.
  그 자리는 명부 레코드가 아니다(§2 마지막 줄).
- 게임 자신의 감소 아이템이 넷 있다 - `1002632`/`1003773`(A.T.A.G. I·II) ·
  `1002631`/`1003772`(드래곤 I·II). 하나당 5~10분. 오버레이에 지급 버튼을 뒀다.
- 우리 `useitem`(2976 구동)으로는 **대기 시간이 안 줄었다**(48분→42분, 경과 시간과
  같다). 사용은 가방에서 해야 한다.
