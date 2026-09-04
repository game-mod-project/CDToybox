# 탈것·반려동물·용병 관리 기능 검토 (2026-09-04)

게임 실행 파일(2.00.01)의 RTTI·치트 표만 보고 판단했다. 실측 전이다.

## 무엇이 있나

- **데이터 표(아이템처럼 읽힌다):** `VehicleInfoManager`,
  `MercenaryInfoManager`, `CharacterInfoManager` 가 전부
  `StaticInfoManager2` 다 - `ItemInfoManager` 를 읽는 코드가 그대로
  일반화된다. 탈것·용병·캐릭터 카탈로그를 이름(현지화)까지 낼 수 있다.
- **플레이어 소유 컴포넌트(인벤토리처럼 읽힌다):**
  `ClientMercenaryActorComponent`, `ClientVehicleActorComponent`,
  `ClientMercenaryClanActorComponent`. RTTI 로 찾아 내용을 걷으면
  "지금 가진 용병/탈것"을 낼 수 있다 - 인벤토리 컴포넌트와 같은 패턴.
- **반려동물은 독립 체계가 아니다.** `Pet` 관련은
  `TrocTrUnSetMainPetAndUnSpawnReq`, `ConditionData_IsPetLooting`,
  `UIGamePlayControlRootPet` 뿐이다. 반려동물은 소환/용병 체계에
  얹힌 것으로 보인다("메인 펫" = 소환된 동반자).
- **네트워크 요청 메시지 1085개 중 탈것·용병·소환 관련 104개.**
  `RideOnVehicleReq`, `HireMercenaryFromInventoryReq`,
  `FireMercenaryReq`, `ChangeMercenaryNameReq`, `MercenaryEquipItemReq`,
  `MoveItemInventoryToVehicleReq` 등 실제 관리 동작이 전부 여기 있다.

## 무엇이 되고 무엇이 안 되나

### Tier 1 — 읽기 전용 관리 뷰 (지금 가능, 위험 낮음)

탈것/용병/캐릭터 카탈로그와 **내가 가진 것**을 이름·스탯까지 보여주는
패널. 아이템 목록·인벤토리 패널과 **완전히 같은 인프라**(RTTI 탐색 +
안전 읽기 + 현지화)를 쓴다. 프레임마다 게임 메모리를 읽지 않고 배경
분석 루프에 얹으면 된다. 새 위험이 없다.

### Tier 2 — 캐릭터/탈것/반려동물 소환 (가능, 중간)

`SpawnCharacterCheatReq`(ID 2510)는 **살아 있는 처리기**(0x278B860 ->
0x2381330)가 있고, 열려 있는 치트 문을 지난다. 페이로드는
`캐릭터키(4) · ?(4) · 위치(12) · 플래그(1)`. 탈것·반려동물·NPC 는 전부
캐릭터(`CharacterInfo`)이므로, 특정 캐릭터를 월드에 소환하는 것은
아이템 바닥 스폰과 **같은 직접 호출 경로**로 가능하다. 실행 지점도
같다(액터 조회 자리). STATUS 에 이미 "SpawnCharacter 인자 배치"가
남은 일로 적혀 있다 - 그것을 마치면 된다.

### Tier 3 — 실제 관리(고용·해고·이름·장비·탑승) — 치트 경로로 불가, 큰 작업

- **용병 치트 3종은 껍데기다.** `RequestHireMercenaryForCheatReq`,
  `RequestDischargeMercenaryForCheatReq`,
  `ChangeHiredMercenaryWithSummonForCheatReq` 모두 처리기가 스텁
  (`0x12255F0` [마지막 호출], 실제 작업 없음)이다. 아이템 지급처럼
  직접 부를 처리기가 없다.
- 진짜 동작은 100+개의 **일반 요청 메시지**(`*Req`)에 있는데, 이것들은
  치트 처리기처럼 직접 못 부른다 - 게임의 **메시지 전송 경로**(요청
  객체를 만들어 큐에 넣고 보내기)를 태워야 한다. 우리는 지금까지
  치트 처리기를 **직접 호출**만 했지, 정상 요청을 **보낸** 적이 없다.
  그 전송 인프라를 새로 파는 것은 별도의 큰 과제이고, 형식이 틀리면
  상태를 망가뜨릴 위험이 있다.

## 권고 순서

1. **Tier 1 부터.** 탈것·용병·반려동물 뷰어. 기존 패널 구조에 그대로
   맞고 위험이 없다. 소유물 컴포넌트 읽기가 인벤토리와 같은 패턴이라
   빠르다.
2. **Tier 2.** SpawnCharacter 인자 배치를 실측으로 확정하고, 탈것/반려/
   NPC 소환을 붙인다. 검증된 치트 호출 경로를 재사용한다.
3. **Tier 3 는 별도 조사로 승격.** 메시지 전송 경로를 먼저
   리버스해야 한다. 되면 고용·장비·이름·탑승까지 열리지만, 지금
   당장의 추가가 아니다.

## 주의

- 지급과 같은 실행 지점(액터 조회 자리)을 쓰므로, Tier 2 는 오늘
  드러난 실행 지점 성질(게임 로직 스레드 활동에 묶임)을 그대로
  물려받는다.
- 소환은 아이템 바닥 스폰처럼 **위치**가 필요하다. 지금은 카메라 초점
  좌표라 3인칭에서 캐릭터와 떨어진 곳에 생긴다 - 바닥을 보고 소환하는
  식으로 완화한다.
