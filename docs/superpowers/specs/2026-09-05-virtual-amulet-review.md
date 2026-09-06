# "가상 부적" 검토 — 부적 없는 종(포획·알)에 부적과 같은 아이템을 만들 수 있나 (2026-09-05)

질문: `동행의 부적` 이 없는 탈것·반려동물(야생 포획 종, 알 부화 종)에 대해
부적과 같은 기능을 하는 **추가 아이템**을 구성할 수 있는가.

정적 분석으로 판정했다(게임이 꺼져 있어 라이브 검증은 §5 스크립트로 남김).

## 1. 결론

- **새 아이템 레코드를 만드는 것은 비권장.** 아이템·사용정보·스킬·버프
  표가 전부 고정 배열 + 정렬 색인이고, 세이브가 아이템 키를 참조한다.
  런타임에 레코드를 끼워 넣어도 다음 로드에서 그 키를 모르는 게임이
  세이브를 깨뜨릴 수 있다.
- **대신 "가상 부적"이 가능해 보인다.** 부적 6종은 지급 가능한 소모품이고,
  부적의 동작은 아이템 자체가 아니라 **스킬이 가리키는 소환 데이터의
  캐릭터 키**에 있다. 사용 직전에 그 키를 원하는 종으로 바꾸고, 사용 뒤
  원복하면 같은 아이템으로 어떤 동반자든 등록을 시도할 수 있다. 데이터
  표는 사용 시점에 읽히므로 메모리 패치가 즉시 먹는다(아이템 표 `+0x238`
  소켓 수 패치가 즉시 반영된 실측과 같은 성질).
- **알 부화는 있다 — 처음 판정은 틀렸다.** "실행 파일에 Hatch/Egg 이름이
  없다"는 검색 결과로 기능이 없다고 단정한 것이 오류다. 실제로는 아이템
  `와이번의 알`(1004389)·`쿠쿠새의 알`(1004388)·`오래된 쿠쿠새의 알`(1000146),
  `이구아나 부화장`(1002404), `돌 둥지 솟대`(1004574) 가 있고, 실행 파일의
  스폰 사유 열거형에 `HatchingGimmick` 값이 있다(`MountVehicle`, `Reserve`
  옆). 즉 부화는 아이템 사용이 아니라 **둥지 기믹의 타이머**(인게임 약 1시간,
  이탈 시 초기화)가 끝나면 "알 부화하기" 상호작용으로 펫 캐릭터를
  `spawnReason=HatchingGimmick` 으로 소환·등록하는 흐름이다(기믹 조건
  `CheckGrowableGimmick`·`CheckGimmickOnTime`·`CheckRemainTimer`). 등록의
  최종 형태는 부적과 같은 "소환된 액터 등록"이라, 부화 종(새끼 와이번 등)도
  가상 부적의 대상이 된다.

## 2. 부적이 실제로 하는 일 (정적 사슬)

```
아이템(Riding_Bear_Amulet 1003843)
 └ _itemUseInfoList +0x80 → ItemUseInfo 행 0x23EC
    └ +0x18 동작 객체 = ItemUseData_Skill
       └ +0x30 → {스킬 행 0x637 (Active_Hire_Riding_Bear_Amulet)}
          └ SkillInfo._buffLevelList +0x18 → BuffInfo
             └ BuffInfo._buffDataList +0x28 → SummonBuffData (버프 처리기
               Common/ServerSummonBuffProcessor)
                └ SummonCharacterData (46필드)
                   _characterKey (+0xEA 부근, 겹침 4개 중 하나)   ← 종을 정한다
                   _appearanceName +0x38, _summonSpawnType +0x42,
                   _summoneeCatchType +0x60, _specialType +0x8C, _spawnReason +0x84 …
                      └ 소환된 개체 → 등록 (TrocTrSummonMercenaryAfterRegistAck /
                        FrameEventRegistMercenary, MercenaryInfo._summonAfterRegist)
```

등록 진입점 셋(아이템 2454·대상 2338·프레임이벤트 `FrameEventRegistMercenaryReq`)
의 처리기를 전부 봤다. 모두 핸들→액터 조회(0x2ADE280/0x2ADD350) 또는
인벤 슬롯 조회 뒤 상태표(0x1398540)·공용 라우터(0x26AFF70)로 간다.
**캐릭터 키만 받아 등록하는 진입점은 없다.** 그래서 부적은 "먼저 소환하고
그 액터를 등록"하는 구조이고, 이것이 가상 부적이 성립하는 이유다.

## 3. 가상 부적 설계

1. **운반체**: 부적 6종 중 하나(예: 1003843). 지급 패널로 준다(검증됨).
2. **대상 선택**: 동반자 탭(또는 근처 탭)에서 종을 고른다. 야생 종은
   `_isHirable=1` 이어야 한다(야생 전부 1).
3. **패치**: 그 부적의 스킬 → 버프 → `SummonCharacterData._characterKey` 를
   대상 캐릭터 키로 바꾼다. 원본 값을 기억한다.
4. **사용**: 플레이어가 인벤에서 부적을 쓴다(또는 후속으로 2454 구동).
5. **원복**: 사용 후(캡처 훅 `획득/아이템` 또는 `등록후소환` 이 뜨면) 원래
   키로 되돌린다.

### 3.1 불확실한 점 (라이브로 풀 것)

| 불확실 | 확인 방법 |
|---|---|
| `_characterKey` 의 실제 오프셋(겹침 4개) | 곰 부적의 SummonCharacterData 를 덤프해 20871/20877(Riding_Bear_1000/1001) 이 어디 있는지 |
| 종을 캐릭터 키 하나로 정하는지, `_appearanceName`·`_summonTagNameHash` 도 같이 맞춰야 하는지 | 6종 부적의 데이터를 나란히 비교(다른 값이 키뿐인지) |
| 등록되는 타입이 캐릭터의 `_mercenaryInfo` 행에서 오는지 | 흑마(31378, 말 타입)로 시험 → "말" 탭에 뜨면 확인 |
| 야생 캐릭터(`_Wild`)를 그대로 넣어도 되는지, Domestic 변형이 필요한지 | 전설마는 유니크 야생(31378)이라 그대로 시험. 일반 말은 `_Domestic` 키로 |
| 고용 수 제한(`MercenaryInfo._defaultLimitHireCount`)·중복 거부 | 같은 종 두 번 시험 |

## 4. 대안 비교

| 방식 | 난이도 | 위험 | 판정 |
|---|---|---|---|
| 새 아이템 레코드 삽입 | 매우 높음(표 5개+색인) | 세이브 손상 | 비권장 |
| **가상 부적(스킬 소환 데이터 패치)** | 낮음(포인터 사슬 3단 + u16 쓰기) | 낮음~중간(원복 실패 시 부적이 다른 종을 냄) | **권장** |
| 2338 구동(근처 야생 개체 핸들) | 중간(핸들 대응 미해결) | 중간(서버 게이트) | Phase 2 병행 |
| 즉시 포획 패치(Nexus 방식) | 중간(우리 exe 재시그니처) | 낮음 | Phase 2 병행 |

가상 부적은 **개체를 찾아갈 필요가 없다**(소환이 따라온다)는 점에서 2338
구동보다 낫다. 반면 부적 소모·소환 위치(플레이어 옆) 등 부적의 성질을 그대로
따른다.

## 5. 라이브 검증 스크립트 (게임 켜지면)

스크래치 `amulet_chain.py`: SkillInfoManager 행 0x637~0x63C 의
`_buffLevelList` → BuffInfoManager → `_buffDataList` 객체(vtable RTTI 로
`SummonBuffData` 확인) → 0x100 바이트 덤프에서 캐릭터 키 후보(20871/20877,
20873/20878 …)를 찾는다. 6종을 나란히 놓고 다른 바이트만 남기면 종을 정하는
필드가 드러난다.

## 6. 알 부화 종에 대한 보충

- 부화 결과물 후보: `Animal_Baby_Wyvern`(Pet 타입, 포획 목록에 1개), 쿠쿠새
  새끼 `Animal_KukuBirdbaby`, 이구아나 `Item_Iguana`(아이템)·`Animal_Iguana`.
  정확한 부화 산출 캐릭터는 둥지 기믹 데이터(SummonCharacterData,
  `_spawnReason`=HatchingGimmick)에서 읽어야 한다 — 게임이 켜지면
  `GimmickInfoManager` 에서 HatchingGimmick 사유를 가진 소환 데이터를 찾는다.
- 가속 대안: 둥지 기믹의 남은 시간(`CheckRemainTimer`) 패치로 즉시 부화.
  가상 부적과 달리 알·둥지·현장이 필요하지만 게임 흐름 그대로라 위험이
  가장 낮다.

## 7. 근거 파일

- SkillInfo 필드: `_buffLevelList` +0x18, `_usableCharacterInfoList` +0x80 (캐릭터/용병 직접 필드 없음)
- BuffInfo 필드: `_buffDataList` +0x28
- SummonCharacterData 46필드(위 발췌), `MercenaryInfo._summonAfterRegist` +0x23
- 버프 처리기 RTTI: `CommonSummonBuffProcessor`, `ServerSummonBuffProcessor`,
  `ConsumeSpawnerMercenaryBuffData`, `DecreaseMercenaryCooltimeBuffData`
- 등록 진입점 처리기: 2454 0x2965510, 2338 0x2960CA0, 프레임이벤트 0x2964690→0x2B79900


## 8. 라이브 검증 결과 (2026-09-05 저녁) — 사슬 가정이 틀렸다

게임을 켜고 스킬·버프·캐릭터 매니저를 RTTI 로 찾아(`amulet_chain2.py`)
부적 6종의 사슬을 실제로 따라갔다.

### 8.1 스킬 → 버프는 빈 껍데기

`Active_Hire_*` 6개 스킬의 `_buffLevelList`(+0x18, 원소 1개)가 가리키는
버프 데이터는 전부 **`VoidActiveBuffData`** 이고, 문자열은
"서릿발 백곰 동행의 부적 PatternDescription 용" 같은 **UI 설명용**이다.
§2 의 "스킬 → SummonBuffData → SummonCharacterData" 가정은 **틀렸다**.
스킬·버프 표 어디에도 캐릭터 키가 없다.

### 8.2 2454 처리기가 조회하는 표도 대응표가 아니다

전역 `0x6DBABA0`(vtable 0x546B260) 싱글턴은 노드 83개의 **서버 객체 상태
레지스트리**({id u32, 해시 u32, 객체 ptr, 상태 1/2})다. 2454 는 인벤 아이템
*인스턴스*의 id 로 이 표를 봐 상태를 검사할 뿐, 아이템 키 → 캐릭터 대응은
여기 없다.

### 8.3 프레임 이벤트 객체도 타입 등록용

`ServerFrameEventRegistMercenary`(1개)·`ClientFrameEventRegistMercenary`
(4개) 인스턴스는 0x10~0x18 바이트 타입 레코드이고 캐릭터 키가 없다.
등록 처리기(0x2B79900)는 세션 액터의 **예약된 프레임 이벤트 목록**
(`[컴포넌트+0x140]`, 0x38 바이트 항목, 시각·종류 대조)과 요청을 맞춰 보고
통과할 때만 등록한다 — 액션 차트가 실제로 예약한 이벤트만 인정한다.

### 8.4 결론 갱신

- 부적이 어느 캐릭터를 등록하는지는 **아이템·사용정보·스킬·버프 표에 없다.**
  남은 자리는 (a) 부적 사용 액션의 **액션 차트 프레임 이벤트 데이터**(사용
  순간에만 메모리에 올라옴), (b) 서버 고용 로직이 아이템 키로 참조하는
  별도 데이터. 어느 쪽이든 **부적을 한 번 실제로 사용하는 순간을 캡처**해야
  드러난다(등록이벤트 훅이 그 와이어를 찍는다).
- 따라서 "가상 부적"의 성립 여부는 **미확정**으로 내린다. 스킬 표 패치로는
  안 된다는 것만 확정.
- 캡처를 사용자 조작 없이 만드는 길: `TrocTrUseItemByItemInfoReq`(2976,
  역직렬화 0x29373D0)는 본문이 `u32 A(아이템 키로 추정), u32 B, u8 C(==0xD
  검사), u32 D` 로 **컨테이너 핸들을 쓰지 않는다** — 소켓을 막았던 핸들
  월드 문제가 없다. 지급한 부적을 이 메시지로 사용시키면 캡처가 뜬다.
  단 역직렬화 구동 인프라(`run_socket` 류)가 e2948b5 에서 제거돼 다시
  들여와야 한다(Phase 2 작업과 겹친다).

### 8.5 확정된 부수 사실

- `SkillInfo._buffLevelList` +0x18 = `{포인터 배열, u32 개수}`, 원소는
  버프 레벨 객체(RTTI 없음) → +0x?? 에 BuffInfo 레코드 포인터.
- `BuffInfo._buffDataList` +0x28 = `{포인터 배열, u32 개수}`(+0x30), 원소는
  RTTI 있는 `*BuffData` 객체.
- 스킬·버프·캐릭터 매니저는 모두 RTTI 인스턴스 1개, 배치는 아이템 표와 같다.
