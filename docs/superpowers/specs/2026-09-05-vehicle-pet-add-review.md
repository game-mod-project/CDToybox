# 탈것·반려동물 "추가" 기능 검토 (2026-09-05)

> **정리 (2026-09-21) — ✅ A안 완료 (부수 결론 반증).** 부적 6종 사용 등록(A안)은
> 실측으로 끝났다. 그러나 "정상 경로는 2454 하나뿐", "아이템 없이 임의 탈것을 소유로
> 넣는 길은 없다" 는 뒤집혔다 — 근처 획득(2338)과 종 교체로 원하는 종을 얻는다
> (`STATUS.md` §1.10). `CatchBySummonReq` 는 야생 포획이 아니라 알→둥지→5분 절차
> 전용이다(`2026-09-08-companion-catalog-and-acquire-research.md`).

질문: 오버레이에서 탈것·반려동물을 플레이어 소유로 **추가**할 수 있는가.
용병 고용 UI 는 필요 없는데 탈것·반려동물에 용병이 왜 얽히는가.

실측(게임 실행 중, PID 34256)과 정적 분석을 섞어 판정했다.

## 1. 결론 먼저

- **엔진은 말·탈것·마차·반려동물·가축을 전부 `MercenaryInfo` 타입으로
  관리한다.** "용병" 은 NPC 고용이 아니라 **동반자 체계 전체의 이름**이다.
  탈것·펫을 소유물로 넣는 정상 경로는 하나뿐이다:
  `TrocTrHireMercenaryFromInventoryReq`(ID 2454) — **인벤토리 아이템을
  고용(등록)** 하는 메시지. 즉 용병 NPC 는 안 써도 되지만, "고용" 메시지는
  탈것·펫에도 그대로 쓰인다.
- 아이템 기반이라 **지금 되는 지급 기능으로 이미 절반은 된다.** 동반자
  아이템(예: `동행의 부적` 6종)을 지급하고 게임 안에서 사용하면 등록된다.
  새 RE 없이 버릴 세이브로 바로 시험 가능(§4 A안).
- 원클릭(지급→고용까지 자동)은 소켓 Phase 2 와 **같은 인프라**(역직렬화
  구동 + 인벤토리 슬롯 참조)로 가능하다. 페이로드가 u16 두 개뿐이라
  소켓보다 단순하다(§4 B안).
- **아이템 없이 임의 탈것 키(Dragon·Wyvern·Elephant 등 34종)를 소유로
  넣는 길은 없다.** 키로 고용하는 치트 3종은 스텁, 저수준 소환은 대공사
  (기존 판정 유지)(§4 C안).

## 2. 실측 근거

### 2.1 MercenaryInfo 표 = 동반자 타입 21종

`MercenaryInfoManager` 인스턴스 0x2EABCF6E1C0, 개수 +0x30 = 21,
레코드 포인터 배열 **+0x58**(roster.cpp 의 kRecordsPtr 과 같다).
레코드 `_stringKey`(+0x08)·`_mercenaryType`(+0x20)·플래그를 읽은 결과:

| # | 이름 | type | 특징 플래그 | key(+0x88) |
|---|---|---|---|---|
| 0 | Mercenary_Main | 1 | _isMainMercenaryInfo | 0 |
| 1 | **Vehicle_Horse** | 2 | _isShowHorseStatus, _useVehicleEquipSlot | 51067 |
| 2 | Vehicle_Dragon | 2 | _useVehicleEquipSlot | 0 |
| 3 | Vehicle_WarMachine | 2 | 〃 | 0 |
| 4 | Vehicle_WarMachine_Raptor | 2 | 〃 | 0 |
| 5 | Vehicle_Special | 2 | 〃 | 0 |
| 6 | Vehicle | 2 | 〃 | 0 |
| 7 | Vehicle_Ship | 2 | 〃 | 35286 |
| 8 | Wagon | 3 | | 15884 |
| 9 | **Pet** | 4 | _isMapIconPet | 11925 |
| 10 | Dokev | 4 | | 0 |
| 11 | Domestic | 5 | _canHaveBreedingTargets, 인벤 없음 | 26000 |
| 12 | Fish | 5 | 〃 | 0 |
| 13 | Insect | 5 | 〃 | 0 |
| 14–18 | Mercenary_Melee/Range/Worker/GuestWorker/Shop | 6–9,12 | | 0 |
| 19 | Observer | 10 | | 0 |
| 20 | RecoveryItem | 11 | | 0 |

(+0x88 은 `_cameraPresetHash` 와 겹치는 자리라 key 로 믿지 말 것.
읽기 스크립트: 스크래치 `merc_read.py`, ctypes ReadProcessMemory.)

**roster 패널의 "용병: 매니저를 찾지 못했습니다" 는 버그다.** 인스턴스는
있다(`probe instances .?AVMercenaryInfoManager@pa@@` → 1개). 색인 검증
쪽 문제로 보인다. 별건.

### 2.2 요청 메시지 101개 중 소유 변경 후보

exe 문자열에서 `TrocTr*(Vehicle|Pet|Mercenary|Summon)*Req` 를 걸렀다
(스크래치 `cd_msgs.txt`). 소유물을 **늘리는** 쪽은:

- `TrocTrHireMercenaryFromInventoryReq` — 인벤 아이템 → 고용. **유일한
  일반 경로.**
- `TrocTrHireMercenaryToTargetReq` — 대상(NPC) 고용. 탈것·펫 아님.
- `TrocTrCatchBySummonReq` — 포획(야생 말 길들이기 추정). 월드 문맥 필요.
- `TrocTrAddHyosiMercenaryReq` — 용도 미상.
- 치트 3종(`RequestHireMercenaryForCheatReq` 등) — 처리기 스텁(기존 확인).

`TrocTrCallSpecialVehicleByQuickSlotReq` 는 이미 가진 탈것 부르기라
"추가"가 아니다(기존 확인).

### 2.3 HireMercenaryFromInventoryReq 역직렬화 (RVA 0x2965510)

서술자 RVA 0x69401C0, vtable 0x5A06818, vtable[2]=역직렬화 0x2965510.
`probe cheat` 가 집은 처리기 0x26AFF70 은 공용 라우터(r9=0x11)라 소켓과
같은 꼴 — **역직렬화 구동으로 태운다.**

디스어셈블 요지:

```
헤더 5바이트 검사([rsi+3] u16 길이 == 패킷길이-5)
읽기 2바이트 -> A   (rbp+0x77)      ; 인벤토리 슬롯 참조 1
읽기 2바이트 -> B   (rbp+0x7f)      ; 인벤토리 슬롯 참조 2
[세션+0x88]+1 == 1, [세션+0x96] != 0  ; 세션 상태 게이트
0x2AD1FC0([세션+0x68]+0x110, &A, A, B) -> 아이템 키 (0xFFFF 거부)
0x3f5 태그 구조체 {u16 0x3f5, u8 0xff, u32 아이템키, u16 [서술자+0xC]}
상태표 조회(0x1398000/0x1398540): [rec+8]==2 면 거부, ==1 이어야 통과
세션 vtable+0xE8 -> 컨텍스트
0x26AFF70(rcx=[0x6C29C50], edx=0x3f5, r8=&태그, r9=0x11)   ; 공용 라우터
```

**본문 4바이트(u16 A, u16 B).** 소켓의 "칸:위치" 슬롯 참조와 같은
인벤토리 좌표계로 보인다(0x2AD1FC0 이 A·B 로 컨테이너를 찾아 아이템 키를
낸다). 실제 값은 **캡처로 확정**해야 한다 — 인벤에서 동반자 아이템을
사용할 때 이 메시지를 후킹하면 된다(grant.cpp 의 `hook_one_socket` 그대로).

상태표 `[rec+8]` 1/2 는 "아이템이 고용 가능 상태 / 이미 고용됨"으로 추정.
게임이 스스로 거르니 **이중 고용은 메시지 단에서 막힌다** — 안전 쪽.

### 2.4 동반자 아이템은 아이템 표에 있다 (지급 가능)

`probe items` 전체 덤프(6813, 스크래치 `items_all.txt`)에서:

| 키 | 이름 | 추정 대상 |
|---|---|---|
| 1003843 | 서릿발 백곰 동행의 부적 | 탈것 Bear(16979) |
| 1003844 | 은빛 송곳니 동행의 부적 | 탈것 Wolf(16966) |
| 1003845 | 순백의 사슴 동행의 부적 | 탈것 ReinDeer(16980) |
| 1003846 | 서릿발 알파인 아이벡스 동행의 부적 | 탈것 AlpineIbex(16993) |
| 1003847 | 바위엄니 혹멧돼지 동행의 부적 | 탈것 Boar(16962) |
| 1003921 | 피닉스 동행의 부적 | 미상 |

이름이 탈것 로스터(VehicleInfo 34종)와 1:1 로 맞는다 — **특수 탈것은
아이템으로 준다.** 반려동물(Pet 타입) 아이템은 현지화 이름만으로는 못
골랐다(`새끼 고슴도치` 1001784 가 후보). 말(Vehicle_Horse)은 아이템이
아니라 포획(`CatchBySummonReq`)·구매로 얻는 것으로 보이며 **미확인**.

아이템→동반자 대응은 `ItemInfo` 필드에 직접 없다(`_sealableCharacterInfoList`
+0x158 정도). `_consumableTypeList`(+0x70) 의 소비 타입이 "고용" 인 아이템을
걸러내면 동반자 아이템 목록이 나올 것 — 후속.

### 2.5 소유 목록은 읽을 수 있다

`ClientMercenaryActorComponent` 인스턴스 8개 생존(0x2EAD9AE6690 …).
인벤토리 컴포넌트와 같은 패턴으로 걷으면 "내가 가진 탈것·펫" 뷰가 된다
(기존 Tier 1b). 레코드 구조는 미해독.

## 3. 용병이 왜 얽히나 (질문에 대한 답)

용병 **고용 UI·NPC 용병**은 필요 없다. 그러나 코드에서 "Mercenary" 는
플레이어가 **소유·소환·해제하는 모든 동반자**의 상위 개념이고, 말·탈것·
펫·가축이 그 하위 타입(type 2·3·4·5)이다. 탈것을 등록하는 것 = 타입 2
용병을 고용하는 것. 그래서 탈것·펫 추가는 반드시 `HireMercenary*`
메시지를 지난다. 우회 경로는 없다(치트는 스텁).

## 4. 구현 선택지

### A안 — 지급만 (0 RE, 오늘 가능)

지급 패널에 "동반자 아이템" 필터/즐겨찾기 세트를 두고 `동행의 부적`
6종을 지급한다. 등록은 플레이어가 인벤에서 아이템을 쓴다. 필요한 것:
버릴 세이브에서 **지급한 부적이 실제로 사용·등록되는지** 1회 실측.
아이템 지급 자체는 검증된 경로라 위험이 낮다.

### B안 — 원클릭 등록 (소켓 Phase 2 인프라 재사용, 중간)

인벤토리 행별 "등록" 버튼: 슬롯(A,B) 조립 → 5바이트 헤더 + 4바이트 본문
→ 역직렬화 0x2965510 구동(소켓과 같은 서버패킷 구동, 게임 스레드 실행
지점, SEH). 선행: (1) `hook_one_socket` 로 이 메시지 1회 캡처해 A·B 의미
확정, (2) 소켓 Phase 2 의 "핸들 학습·구동" 코드가 정리된 뒤 얹기.
실제 게임플레이 거래라 소켓과 같은 등급의 오염 위험 — 버릴 세이브 필수.

### C안 — 아이템 없는 임의 탈것/펫 (대공사, 비권장)

VehicleInfo 34종(Dragon·Wyvern·Elephant·WarMachine…) 을 키만으로 소유에
넣는 메시지는 없다. `SummonCharacterData` 저수준 소환(45필드)은 소유가
아니라 월드 스폰이고, 기존 판정(고위험·불확실)이 그대로다. 대신 그 탈것에
대응하는 **아이템이 있는지**를 `_consumableTypeList` 로 먼저 훑는 것이
싸다 — 있으면 A/B 안으로 흡수된다.

### D안 — 소유 목록 뷰 (읽기 전용, 낮음)

`ClientMercenaryActorComponent` 8개를 걷어 "가진 탈것·펫" 표. A/B 안의
결과 확인 수단으로도 쓰인다.

## 5. 권고

A → D → B 순. C 는 아이템 유무 조사 뒤 판단. 첫 실측은 A안(부적 지급 →
게임 내 사용) 이며 새 코드 없이 오늘 되는 시험이다.

## 6. A안 진행 (2026-09-05)

- `bin64/cdtoybox_stash.txt` 에 세트 **"동반자 부적"** 추가(부적 6종, 1개씩).
  원본은 `cdtoybox_stash.txt.bak`.
- 보관함 패널에 **"다시 읽기"** 버튼(stash_panel.cpp). 파일을 손으로 고친 뒤
  게임 재시작 없이 반영한다. 저장 안 한 변경은 버린다.
- 빌드 OK, 291 테스트 통과. **배포는 게임 실행 중이라 거부됨**(deploy.ps1
  PID 34256). 게임 종료 후 `scripts/deploy.ps1`.
- **실측 성공 (13:59~14:00, 세션 0x479720E0200).** 세트 "전부 지급"으로
  부적 6종이 5초 간격으로 들어갔고, 인벤에서 사용하니 **6종 전부 등록됐다**:
  특수 탑승물 탭에 바위엄니 혹멧돼지·서릿발 백곰·서릿발 알파인 아이벡스·
  순백의 사슴·은빛 송곳니 5종, 반려동물 탭에 피닉스 1종. 크래시·오염 없음.
- 확정된 사실: `동행의 부적` = 탈것/펫 등록 아이템. 피닉스는 **반려동물**
  (Pet 타입), 나머지 5종은 특수 탑승물(Vehicle 타입). §2.4 의 추정이 맞다.
- A안은 이것으로 완료. 다음은 D안(소유 목록 뷰) 또는 B안(원클릭 등록).
  B안의 가치는 "아이템 사용" 한 번을 줄이는 것뿐이라, 우선순위는 낮다.
  더 큰 남은 질문은 **다른 탈것(Dragon·Wyvern·Elephant 등)과 다른 펫에
  대응하는 아이템이 있는가** — `_consumableTypeList` 로 훑는 C안 선행 조사.

## 7. 아이템 대응 전수 조사 (2026-09-05, 실측) — 부적 6종이 전부다

질문: 드래곤·와이번·코끼리 등 나머지 탈것과 다른 펫에 대응하는 아이템이
있는가. 라이브 메모리에서 아이템 6813 → 사용정보 10137 → 스킬 2061 을
사슬로 걸어 답했다(스크래치 `item_scan.py`, ctypes ReadProcessMemory).

### 사슬 구조 (재사용 가능)

- `ItemInfo._itemUseInfoList`(+0x80) = `{u32* 색인배열, u32 개수}`.
  원소는 **ItemUseInfoManager 레코드의 행 색인**(키 아님).
- `ItemUseInfo` 레코드(0x60): +0x00 키, +0x08 `_stringKey`("<아이템>_item_use_N",
  동작을 안 알려줌), **+0x18 → 동작 객체(다형)**. 객체 vtable 의 RTTI 로
  동작 클래스가 나온다. 20종: Skill 3656, RegisterReserveSlot 1604,
  PlaySequencerOnly 1401, FeedToTarget 1155, Inspect 1111, RandomBox 441,
  SealToEquip 412, SummonGimmickWithCatch 149, SendEventToDockingGimmick 99,
  SummonCharacterWithCatch 34(전부 물고기 방생), DestroyOnly 26, OpenUI 14,
  UseSealed 10, SubLevelUp 8, CustomizeCharacter 6, ExpandInventorySlot 4,
  ExpandFarmSlot 3, InventoryBuff 2, TeleportRevivePoint 1, ConvertCharacter 1.
  **"고용" 전용 동작 클래스는 없다.**
- `ItemUseData_Skill` 객체: +0x30 → `{u32 스킬 행색인, u32 1}` 배열, +0x38 개수.
- `SkillInfoManager`(인스턴스 0x479083CD000, 2061행): 레코드 +0x00 u16 키,
  +0x08 `_stringKey`.

### 결과

부적의 사용 = `ItemUseData_Skill` → 스킬 `Active_Hire_*` 부여(+ 두 번째
사용정보 RandomBox 는 부수). 스킬 표 2061행 중 `Active_Hire_*` 는 **정확히
6개**이고, 각각을 주는 아이템은 **부적 6종뿐**이다:

| 스킬 행 | 스킬 | 아이템 |
|---|---|---|
| 0x637 | Active_Hire_Riding_Bear_Amulet | 1003843 Riding_Bear_Amulet |
| 0x638 | Active_Hire_Riding_Wolf_Amulet | 1003844 Riding_Wolf_Amulet |
| 0x639 | Active_Hire_Riding_Deer_Amulet | 1003845 Riding_Deer_Amulet |
| 0x63A | Active_Hire_Riding_Warthog_Amulet | 1003847 Riding_Warthog_Amulet |
| 0x63B | Active_Hire_Riding_AlpineIbex_Amulet | 1003846 Riding_AlpineIbex_Amulet |
| 0x63C | Active_Hire_Pet_Phoenix_Amulet | 1003921 Pet_Phoenix_Amulet |

아이템 내부 이름 전수 검색(riding/pet/dragon/wyvern/elephant/camel/iguana/
cucu/carmabird/warmachine/wagon/ship/dokev/domestic…)도 위 6종 외에 등록
아이템이 없음을 뒷받침한다. 관련이지만 "추가"가 아닌 것:

- `Item_Rare_Collect_opuntia`(1000397) → `Skill_CallDragon`,
  `Item_Rare_Collect_Taro`(1002090) → `Skill_CallVehicle`: **이미 가진**
  탈것을 부르는 스킬. 소유 추가 아님.
- `Pet_Slot_Expansion_*` 8종(type 54): 펫 슬롯 확장. 펫 자체 아님.
- `Pet_Phoenix_Feather`(1003920): 사용정보 없음(재료/퀘스트).
- `*_Horse_Report`·`Legend_*_Report`(type 54): 도감/보고서.
- 말먹이 `HorseFeed_*`, 아비스기어 `AddHorseExp/AddPetFriendly`: 성장 보조.

### 판정

- **아이템으로 추가 가능한 동반자 = 특수 탑승물 5 + 반려동물 1, 이미 A안으로
  전부 지급·등록 완료.** 이 방향은 더 확장할 것이 없다.
- 말(Vehicle_Horse), 드래곤·ATAG(전투기계), 개·새 펫 등은 아이템이 아니라
  **스토리/포획(`TrocTrCatchBySummonReq`)/퀘스트로 부여**된다. 이를
  오버레이에서 넣으려면 `HireMercenaryToTargetReq`(대상 NPC 고용) 또는
  저수준 소환+고용 조립이 필요 — 기존 판정대로 대공사·고위험. 비권장.
- 남는 실용 후보: **D안(소유 목록 뷰)**, 그리고 `Skill_CallDragon/
  CallVehicle` 스킬을 주는 수집 아이템 2종은 "탈것 즉시 호출" 편의로
  쓸 수 있는지 시험할 가치가 있다(지급→사용, 새 코드 없음).
