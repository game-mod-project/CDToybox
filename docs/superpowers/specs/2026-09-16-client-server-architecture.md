# 이 게임의 클라이언트/서버 구조 (2026-09-16)

> **정리 (2026-09-21) — ✅ 완료.** 한 프로세스 안의 클라/서버 계층 구조는 실측으로
> 확정했고, 이후 서버 사본 쓰기(탈것 체력)가 뒷받침한다(`STATUS.md` §1.21.3). 단 §2-2 의
> "동반자 등록 표는 realm 마다 한 벌(드래곤 1000483 · 1000724)" 은 **틀렸다** — 서로
> 다른 동반자 둘이다(`STATUS.md` §1.21).

**싱글플레이 게임인데 안이 MMO 서버 프레임워크다.** 이 문서는 그것을
추측이 아니라 **실행 파일과 실행 중 프로세스에서 잰 것**으로 못박는다.

왜 지금 적는가 — 드래곤 조사에서 "서버가 안 내준다" 를 두 번 썼고 두 번 다
뜻이 흔들렸다(`2026-09-16-story-vehicle-wheel.md` §4-1). *원격 서버* 와
*같은 프로세스 안의 서버 계층* 은 전혀 다른 말이다. 앞의 것이면 손댈 수
없고, 뒤의 것이면 **디투어 하나 거리**다. 여기서 뒤쪽임을 확정한다.

---

## 0. 한 줄

**클라이언트도 서버도 `CrimsonDesert.exe` 한 프로세스 안에 있다.** 둘은
메시지(`TrocTr*Req`/`Ack`)로 이야기하고, 그 메시지는 소켓이 아니라 **가상
세션**(`NwVirtualAsyncSession`·`NwVirtualSyncSession`)을 지난다. 그래서
**양쪽 다 후킹·읽기·쓰기가 된다.**

---

## 1. 근거

### 1-1. 클래스가 셋으로 갈라져 있다 (정적, 실행 파일)

`CrimsonDesert.exe`(1.0.0.2850)에서 `.?AV…@pa@@` RTTI 이름 **5,091개**를
뽑아 세었다.

| 접두사 | 개수 |
|---|---|
| `Client*` | 310 |
| `Server*` | 176 |
| `Common*` | 236 |

같은 기능이 **세 벌**로 존재한다. 표본:

| 기능 | Client | Server | Common |
|---|---|---|---|
| `…InventoryActorComponent` | O | O | O |
| `…MercenaryActorComponent` | O | O | O |
| `…MercenaryClanActorComponent` | O | O | O |
| `…EquipSlotActorComponent` | O | O | O |
| `…KnowledgeActorComponent` | O | O | O |
| `…CharacterControlActorComponent` | O | O | O |
| `…FrameEventActorComponent` | O | O | O |
| `…FactionActorComponent` | — | O | O |

액터·필드 계층도 같은 모양이다:

```
ClientActor / ClientActorManager / ClientActorContainer / ClientField
CommonActor / CommonActorManager / CommonActorContainer / CommonField / CommonFieldSector
ServerActor / ServerActorManager / ServerField / ServerFieldSector / ServerFieldAttacher
```

`Common*` 이 공통 알맹이고 `Client*`/`Server*` 가 각 realm 의 껍질이다.
**이것이 우리가 "realm 이 둘" 이라고 불러온 것의 정체다.**

### 1-2. 두 realm 이 같은 프로세스에서 동시에 살아 있다 (라이브)

`bin64/CDToybox.log`(2026-09-16 15:25 세션)에서 그대로 나온다:

```
세션 1 0x26DB80E0200 -> 액터 0x26DB819B680 (.?AVServerInventoryActorComponent@pa@@)
세션 2 0x26D80044740 -> 액터 0x26DBD74AF00 (.?AVClientInventoryActorComponent@pa@@)
```

주소 두 개가 **같은 프로세스의 힙**이다. 모드는 이 둘을 `rtti.class_of_object`
로 갈라 보고 **both-realms 쓰기**를 한다 — 장비·인벤토리·지식·스킬포인트가
전부 그렇게 되어 있다(`equip.h`·`inventory.h`·`knowledge.h`·`skillpoint.h`).
한쪽만 쓰면 다른 쪽이 되돌린다는 것이 실측으로 확인된 규칙이다
(TROUBLESHOOTING [3.3](../../TROUBLESHOOTING.md)).

### 1-3. 메시지 프로토콜이 통째로 들어 있다

`TrocTr*` 클래스가 **1,104개**다.

| 종류 | 개수 |
|---|---|
| `…Req` (클라 → 서버 요청) | 483 |
| `…Ack` (서버 → 클라 응답) | 519 |
| `…Nak` (거부) | 17 |

나머지는 타이머·데이터 객체다(`…OnceTimer` 61개, `…RepeatTimer` 19개 —
서버 쪽 지연 작업 스케줄러).

요청/응답이 **번호로 오간다**는 것도 실측이다 — 예: `TrocTrUseItemReq` ID
2676, `TrocTrFrameEventCallMercenaryReq` ID 2895, 알림 0x3F5.
번호→처리기 표는 정적으로 안 나오고 **실행 중에 등록**된다
(`2026-09-16-story-vehicle-wheel.md` §4-0).

### 1-4. 세션이 "가상" 이다

```
NwSession · NwAsyncSession · NwSyncSession · NwSyncSessionPool
NwVirtualAsyncSession        <- 소켓이 아닌 세션
NwVirtualSyncSession
NwServerFrameworkParameter · NwSyncClientFrameworkDesc
NetworkActor · NetworkThread · PacketBuffer · PacketBufferSend
```

**`NwVirtual*` 가 핵심이다.** 진짜 서버 프레임워크(`Nw*`)를 그대로 쓰되
전송 계층만 가상으로 갈아 끼운 모양이다. 싱글플레이에서 메시지가 실제로
오가는데 네트워크는 안 타는 이유가 이것으로 설명된다.

> **아직 안 잰 것:** `NwVirtual*` 인스턴스가 실제로 몇 개 살아 있고 우리
> 메시지가 그 객체를 지나는지는 **확인하지 않았다.** 클래스가 있다는 것까지가
> 실측이다. 확인하려면 RTTI 로 인스턴스를 세면 된다(`probe instcount`).

### 1-5. DB 계층까지 있다

```
Sql · SqlDb · SqlGameDb · SqlWorldDb · SqlLogDb · SqlCommon
SqlUser · SqlCharacter · SqlItem · SqlKnowledge · SqlQuest · SqlSkill
SqlMercenaryClan · SqlGuild · SqlHousing · SqlTradeMarket · SqlFriendly
SqlChallenge · SqlCollectionSpawn · SqlDrop · SqlHistory · SqlJournal
SqlSubLevel · SqlServerInformation · SqlGuardResultSetBase
```

그리고 조회 결과 객체 `Get*FetchResultSet` 무리
(`GetListMercenaryFetchResultSet`·`GetListItemFetchResultSet` …).
**세이브 파일이 이 DB 자리를 대신한다** — `%LOCALAPPDATA%\Pearl Abyss\CD\
save\<id>\slot*\save.save`, 머리 4바이트가 `SAVE` 이고 나머지는 암호화돼
있다(§3).

### 1-6. 로그인·로비 스택도 그대로다

```
ClientUserActor / CommonUserActor
ClientUserLoginActorComponent / CommonUserLoginActorComponent
ClientLobbyActorComponent / CommonLobbyActorComponent
AsyncLoginCompleteThreadProcess
CharacterLoginedScopeAttacher · CharacterValidLoginingScopeAttacherXXX
```

**결정적 한 줄** — 실행 파일에 이 클래스가 있다:

```
TrocTrClearCharacterLobbyDataForSinglePlayAck
```

*"싱글플레이용 캐릭터 로비 데이터 정리 응답"*. 싱글플레이가 **이 프레임워크의
한 갈래로 구현돼 있다**는 것을 게임이 스스로 밝히고 있다.

### 1-7. 서버 스크립트 네임스페이스

```
gameServerScript
gameServerScript::CreateGameServerScriptTestClass
```

문자열로 남아 있다. (하위 클래스 이름은 `@gameServerScript@@` 로 안 나온다 —
이름만 있고 RTTI 로 노출된 타입은 없다.)

### 1-8. 곁증거 — 배포된 DLL

`bin64/` 에 서버 운영용 라이브러리가 그대로 따라왔다:

| DLL | 무엇 |
|---|---|
| `librdkafka.dll` · `librdkafkacpp.dll` | Kafka 클라이언트(로그·이벤트 파이프) |
| `libeay32.dll` · `ssleay32.dll` | OpenSSL |
| `hermessdkcorewrapper_release.dll` | 펄어비스 자체 SDK |
| `sentry.dll` · `crashpad_handler.exe` | 크래시 리포팅 |

이것만으로는 아무것도 증명하지 못한다(빌드에 딸려 왔을 뿐일 수 있다).
§1-1~§1-6 의 보강 자료로만 둔다.

---

## 2. 그래서 무엇이 달라지는가

### 2-1. "서버가 안 내준다" 는 **결론이 아니라 위치 표시**다

원격 서버라면 끝이지만, 여기 서버는 **같은 힙 안의 객체들**이다.
따라서 다음은 전부 가능하다:

- `Server*ActorComponent` 인스턴스를 RTTI 로 찾아 **직접 읽고 쓴다**
- 서버 쪽 처리기·조건 객체에 **디투어를 건다**
- `Ack` 를 만드는 자리에서 **결과를 갈아 끼운다**

드래곤 조사가 클라 쪽 요청 조립 경로(`0x29411E0` → `0x2B78330` →
`0x2A22DE0` → `0x292B040`)만 따라가고 **받는 쪽을 한 번도 안 봤다**는 것이
지금 남은 공백이다.

### 2-2. 값은 항상 두 벌을 의심한다

realm 이 둘이므로 **표도 둘**이다. 실제로 겪은 것:

- 인벤토리·장비·지식·스킬포인트: 한쪽만 쓰면 되돌아간다 → both-realms 쓰기
- 동반자 등록 표: realm 마다 한 벌씩. 드래곤 등록 항목이 번호
  **1000483·1000724** 두 벌이었다(`story-vehicle-wheel.md` §0-2)
- 한쪽만 고치면 **클릭 순간에 옛 값이 잡힌다**(13:22 로그 실측)

`CharacterInfoManager` 처럼 **싱글턴인 것도 있다**(정적 표는 한 벌).
realm 이 둘인 것은 *액터 컴포넌트와 그 데이터*이지 *정적 게임 데이터*가
아니다. 매번 확인하고 쓴다.

### 2-3. 세이브 파일은 "서버 DB" 자리다

인게임 메모리를 고쳐도 안 남는 값들(가방 용량 §1.16, 스킬 포인트 §1.17)이
있는 이유가 이 구조로 설명된다 — **저장되는 것은 서버 realm 의 SaveData
직렬화 결과**이고, 클라 표시값은 파생물이다. `MercenarySaveData` 같은
클래스가 **플레이 중에는 인스턴스가 0개**인 것도 같은 이야기다(세이브·로드
순간에만 만들어지는 징검다리).

---

## 3. 세이브 파일 (아직 안 판 층)

```
%LOCALAPPDATA%\Pearl Abyss\CD\save\<steamid>\slot<N>\save.save
                                             \slot<N>\lobby.save
slot0~2 = 자동 저장 · slot100~108 = 수동 저장 1~9
```

이 기기 실측(2026-09-16): `59132039` 아래 slot0~slot107, `save.save` 가
**1.5~1.6MB**. 머리:

```
00000000  53 41 56 45 02 00 80 00  00 00 00 00 02 00 00 00   SAVE............
00000010  00 00 52 29 52 00 c1 f6  17 00 93 37 3b 24 26 78   ..R)R......7;$&x
```

`SAVE` 매직 + 헤더 뒤로는 **고엔트로피**(암호화/압축)다.
커뮤니티가 복호 방법을 공개해 두었다 — `parc_*`(PARC 컨테이너)·
`save_crypto.py`·`ben_save_decrypt.py`
(`github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS`, MIT).

**우리 모드는 이 층을 한 번도 안 만졌다.** 자세한 것은
`2026-09-16-dragon-external-research.md` §3.

---

## 4. 구조 한 장

```
                      CrimsonDesert.exe  (한 프로세스)
 ┌─────────────────────────────┐        ┌─────────────────────────────┐
 │  클라이언트 realm            │        │  서버 realm                  │
 │  ClientActorManager         │        │  ServerActorManager          │
 │  ClientField                │        │  ServerField / …Sector       │
 │  Client*ActorComponent      │        │  Server*ActorComponent       │
 │  ClientFrameEvent*          │        │  ServerFrameEvent*           │
 │  (렌더·입력·UI)              │        │  (판정·스폰·소유·저장)         │
 └──────────┬──────────────────┘        └──────────────┬──────────────┘
            │        TrocTr*Req  →→→→→→→→→→→→→→→→→→→   │
            │        ←←←←←←←←←←←←←←←←←←←  TrocTr*Ack   │
            │                                          │
            │      NwVirtualAsyncSession / NwVirtualSyncSession
            │      (소켓 없음 - 같은 프로세스)
            │                                          │
 ┌──────────┴──────────────────────────────────────────┴──────────────┐
 │  Common*  — 두 realm 이 공유하는 알맹이                              │
 │  CommonActor · CommonField · Common*ActorComponent                 │
 └────────────────────────────────────────────────────────────────────┘
            │                                          │
 ┌──────────┴────────────┐              ┌──────────────┴──────────────┐
 │ 정적 게임 데이터 (싱글턴) │              │ Sql* / *SaveData            │
 │ *InfoManager, 134개 표  │              │ → save.save (SAVE 매직)     │
 │ 0008 그룹 .pamt/.paz   │              │                             │
 └───────────────────────┘              └─────────────────────────────┘
```

---

## 5. 다음에 확인할 것 (아직 안 잰 것만)

1. **`NwVirtual*` 인스턴스 수** — `probe instcount`. 있으면 메시지가 정말
   그 객체를 지나는지 vtable 훅으로 잰다.
2. **`Server*` 컴포넌트 인스턴스 목록** — 지금 모드는 realm 을 클래스 이름으로만
   갈라 본다. `ServerMercenaryClanActorComponent` 를 직접 잡으면 동반자
   소유·소환 판정을 **받는 쪽에서** 볼 수 있다.
3. **`Ack` 조립 자리** — `TrocTrCallVehicleMercenaryAck` 의 프로토타입 객체는
   이미 찾아 뒀다(`0x691C680`, 등록 `0x12ED35`). 그것을 채우는 코드를 잡으면
   드래곤/A.T.A.G. 의 갈림이 그 자리에서 보인다.

---

## 6. 근거 재현법

```bash
EXE="…/bin64/CrimsonDesert.exe"
grep -a -o '\.?AV[A-Za-z0-9_]*@pa@@' "$EXE" | sort -u > rtti_pa_classes.txt
grep -c '\.?AVClient' rtti_pa_classes.txt      # 310
grep -c '\.?AVServer' rtti_pa_classes.txt      # 176
grep -c '\.?AVCommon' rtti_pa_classes.txt      # 236
grep -c '\.?AVTrocTr'  rtti_pa_classes.txt     # 1104
grep -i 'NwVirtual\|SinglePlay' rtti_pa_classes.txt
```

라이브 근거는 `bin64/CDToybox.log` 의 `세션 N … ActorComponent` 줄.

---

## 7. 관련 문서

- `2026-09-16-story-vehicle-wheel.md` — 드래곤/A.T.A.G. 정본. §4-1 이 이
  구조를 처음 지적했고, 이 문서가 그것을 실측으로 채웠다
- `2026-09-16-dragon-external-research.md` — 외부(커뮤니티·데이터 파일) 조사
- `../../STATUS.md` §1.21 · `../../TROUBLESHOOTING.md` §8
