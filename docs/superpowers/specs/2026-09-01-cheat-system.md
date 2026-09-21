# 게임에 남아 있는 개발자 치트 시스템

> **정리 (2026-09-21) — 📜 기록.** 치트 경로는 이후 동작이 확인됐다
> (`2026-09-01-cheat-messages.md`). 디버그 콘솔은 **닫힌 길**이다(`2026-09-03-debug-console.md`,
> `TROUBLESHOOTING.md` §9). 여기 적힌 콘솔 전역 `0x1465BB288` 은 오기(`0x14625B288`)이고
> "콘솔 `+0x50` = 표시 플래그" 도 틀렸다(`2026-09-03-debug-console.md` §1,
> `STATUS.md` §2.2.1). 아이템 키는 16비트가 아니다(`2026-09-01-item-table.md`).

**날짜:** 2026-09-01
**방법:** EXE 파일만 읽음. 게임 실행 불필요.

## 요약

Crimson Desert 출시 빌드에는 **개발자 치트 명령 시스템과 디버그
콘솔이 통째로 남아 있다.** 아이템 지급을 메모리 조작으로 구현할
이유가 없다 — 게임 자신의 경로를 쓰면 인벤토리·스택·UI 갱신·저장이
전부 알아서 맞는다.

이것이 카메라 작업과 결정적으로 다른 점이다. 카메라는 "렌더가 읽는
값"을 찾아야 했고 그것이 어디 있는지 게임이 알려 주지 않았다. 치트는
게임이 이름과 구조를 전부 남겨 두었다.

## 치트 요청 클래스 35개

RTTI 이름으로 확인. 전부 `pa` 네임스페이스.

| 분류 | 클래스 |
|---|---|
| 아이템 | `TrocTrCreateItemFromTrItemValueCheatReq` |
| | `TrocTrDeleteItemCheatReq` |
| | `TrocTrSpawnItemToGroundByCheatReq` |
| | `TrocTrVaryEnduranceItemByCheatReq` |
| | `TrocTrSetInventorySlotCountByCheatReq` |
| 캐릭터 | `TrocTrVaryStatCheatReq` |
| | `TrocTrKillCheatReq` |
| | `TrocTrSpawnCharacterCheatReq` |
| | `TrocTrMovePlayerByCheatReq` |
| | `TrocTrMoveFieldCheatReq` |
| | `TrocTrCharacterPresetSpawnGimmickByCheatReq` |
| 퀘스트·지식 | `TrocTrCompleteQuestByCheatReq` / `Ack` |
| | `TrocTrResetQuestByCheatReq` / `Ack` |
| | `TrocTrLearnKnowledgeByCheatReq` |
| | `TrocTrLearnKnowledgeGroupByCheatReq` |
| | `TrocTrRemoveKnowledgeByCheatReq` |
| 월드 | `TrocTrDestroyAllFieldCheatReq` |
| | `TrocTrResetFogDataByCheatReq` / `Ack` |
| | `TrocTrChangeGameLevelStateByCheatReq` |
| | `TrocTrReloadGimmickCheatAck` |
| 용병 | `TrocTrRequestHireMercenaryForCheatReq` |
| | `TrocTrRequestDischargeMercenaryForCheatReq` |
| | `TrocTrChangeHiredMercenaryWithSummonForCheatReq` |
| 기타 | `TrocTrAiControlChangeCheatReq` |
| | `TrocTrCheatDirectPlayReq` |
| | `TrocTrManagementForCheatReq` |
| | `TrocTrResetFriendlyCheatReq` / `Ack` |
| | `TrocTrResetGameAdviceCheatReq` |
| | `TrocTrResetHistoryByCheatReq` |
| | `TrocTrCompleteQuestByCheatAck` |

관련 문자열: `DevCheat`, `TestCheat`, `eErrNoInvalidCheatIndex`,
`_saveByCheat`, `OnChangeStateByCheat`.

## 정적 데이터 테이블 149개

전부 같은 형태다. **하나를 읽는 법을 알면 전부 읽는다.**

```
StaticInfoManager2<XxxKey, XxxInfo, XxxInfoManager, 키타입>
```

키타입은 `G`(unsigned short) 또는 `I`(unsigned int).

아이템 계열:

| 테이블 | 내용 |
|---|---|
| `StaticInfoManager2<ItemKey, ItemInfo, ItemInfoManager, G>` | **마스터 아이템 표** (키 16비트 → 최대 65,536종) |
| `<EquipKey, EquipInfo, ...>` | 장비 |
| `<EquipTypeKey, ...>` | 장비 종류 |
| `<ItemGroupKey, ...>` | 아이템 묶음 |
| `<ItemUseKey, ItemUseInfo, ..., I>` | 사용 효과 |
| `<CraftToolKey, ...>` / `<CraftToolGroupKey, ...>` | 제작 도구 |
| `<ElementalMaterialKey, ...>` | 재료 |
| `<TradeMarketItemKey, ...>` | 거래소 |
| `<RecoverableLostItemKey, ...>` | 분실 회수 |

인벤토리 계열 클래스:
`ClientInventoryActorComponent`, `CommonInventoryActorComponent`,
`ServerInventoryActorComponent`, `ItemPushInventory`, `ItemPopInventory`,
`InventoryInfoManager`.

## 디버그 콘솔

문자열로 확인:

```
PearlAbyssEngine.ShowDebugConsole
PearlAbyssEngine.HideDebugConsole
PearlAbyssEngine.ToggleShowMouseCursor
EngineConsoleCommandHandler   UiDebugCommandConsole   DebugConsole
open_console / close_console / echo_console / noecho_console
"BlackSpace Debug Console Helper."
```

`PearlAbyssEngine.*` 는 점 표기 액션 이름이고, 앞서 카메라 조사에서
본 `PearlAbyssEngine.Debug.DebugCameraState` 와 같은 계열이다.

### 등록 코드 (0x14050C673 ~)

```asm
mov  rcx, [0x1465BB288]        ; 디버그 시스템 전역. 널이면 통째로 건너뜀
test rcx, rcx
je   skip
xor  r9d, r9d
xor  r8d, r8d
lea  rdx, [0x144D08C18]        ; "PearlAbyssEngine.ShowDebugConsole"
call 0x1437BEEB0               ; 이름으로 액션 찾기 -> rax
test rax, rax
je   skip
lea  rcx, [0x14050B650]        ; 콜백 함수
...                            ; {함수, r15d} 쌍을 스택에 만들고
call 0x14050D180               ; 핸들러 등록
```

바로 다음 블록이 같은 꼴로 `HideDebugConsole` 을 등록한다.

**전체가 `if (전역 != null)` 로 감싸여 있다.** 출시 빌드에서 그 전역이
비어 있으면 등록 자체가 일어나지 않는다. 확인 필요.

### 콘솔 표시 콜백 (0x14050B650)

```asm
mov  rdi, rcx                  ; 핸들러 컨텍스트
mov  rcx, [rcx+8]
mov  rax, [rcx]
call [rax+0xD8]                ; 콘솔 객체 얻기 -> rbx
test rax, rax  / je 종료
mov  r8, [rax]
mov  rcx, rax
call [r8+0x28]                 ; 표시 가능 검사 (bool)
test al, al    / je 종료
cmp  byte [rbx+0x50], 1        ; 이미 표시 중인가
je   종료
mov  rax, [rbx]
mov  rdx, -1
mov  rcx, rbx
call [rax+0x48]
mov  byte [rbx+0x50], 1        ; 표시 플래그를 세운다
```

**콘솔 객체의 `+0x50` 바이트가 표시 여부다.**

## 다음 단계 (게임 실행 필요)

1. `probe types Console` / `objects Console` 로 살아 있는 콘솔 객체를
   찾는다. `UiDebugCommandConsole`, `EngineConsoleCommandHandler` 가
   후보다.
2. 전역 `0x1465BB288` 이 널인지 확인한다. 널이면 디버그 시스템이
   아예 만들어지지 않은 것이고, 콘솔 경로 대신 치트 요청을 직접
   구성하는 쪽으로 간다.
3. 콘솔 객체를 찾으면 `+0x50` 을 1 로 세워 본다. 읽기 한 번, 쓰기 한
   번이라 카메라 때처럼 대량으로 쓰는 위험이 없다.
4. 콘솔이 열리면 아이템뿐 아니라 35개 치트 전부가 게임 자신의 UI 로
   열린다.

콘솔이 막히면 차선책은 `ItemInfoManager` 로 아이템 표를 읽어 UI 를
만들고, `ItemPushInventory` 경로로 지급하는 것이다. 그 경우에도
데이터는 게임 표에서 오므로 아이템 이름·분류가 정확하다.

## 주의

카메라에서 배운 것을 되풀이하지 않는다.

- **화면으로 확인하기 전에는 동작한다고 하지 않는다.**
- **검증하지 않은 주소에 대량으로 쓰지 않는다.** 콘솔 플래그는 단일
  바이트 하나다.
- **한 번에 한 가지만 바꾼다.**

---

# 아이템 표 정찰 (실행 중인 게임, 읽기 전용)

주소는 실행마다 바뀐다. 구조만 의미가 있다.

## ItemInfoManager

```
ItemInfoManager        0x4F9F64E4F80   (RTTI 로 찾음)
  +0x00  vtable        0x14511D768
  +0x10  포인터        0x4F9E0B13180
  +0x28  포인터        색인 표
  +0x30  6810 / 6810   크기 / 용량
  +0x50  포인터 배열   0x50 간격
  +0x58  포인터 배열   0x500 간격   <- 아이템 레코드
  +0x60  포인터
  +0x80  포인터 배열   0x10 간격
```

**아이템 6,810개.** `0x1A9A` 가 매니저 안에서 여러 번 반복되는 것으로
확인했다.

## 색인 표 (매니저+0x28)

8바이트 항목 6,810개. `(u32, u32)` 쌍이다.

```
(0x898=2200,     0)
(0xC351=50001,   690)
(0xF4C3D,        1374)
(0xC353=50003,   2061)
```

두 번째 값이 ~685씩 단조 증가한다. 오프셋 표로 보인다.
첫 번째 값은 레코드의 첫 필드와 일치한다 - 즉 **키**다.

## 아이템 레코드 (0x500 바이트)

첫 레코드 `0x4FA8CC92300`:

```
+0x00  0x898 = 2200      <- 키. 색인 표의 첫 항목과 같다
+0x08  포인터 -> 문자열 객체
+0x18  0x64 = 100
+0x20  포인터
+0x28  0x70, 0x898
+0x30  7
+0x38  포인터
+0x40  0xFFFFFFFF
+0x80  포인터 -> 0x50 짜리 객체
```

**레코드 안에 읽을 수 있는 이름이 없다.** 0x500 바이트 전체를 훑어
알파벳 4자 이상이 이어지는 곳이 한 군데도 없었다.

## 이름은 현지화 ID 다

`ItemInfo+0x08` 이 가리키는 문자열 객체:

```
+0x00  포인터
+0x08  15            <- 길이
+0x0C  해시
+0x10  1, 1          <- 참조 수?
+0x20  "9448928051312"   <- 인라인 문자열
```

같은 꼴로 `"214752659767408"` 도 나온다. 표시 이름이 아니라 **숫자
현지화 키**다.

병렬로 있는 0x50 짜리 객체에는 UTF-16 짧은 코드가 들어 있다:

```
+0x00  0x258C, 해시
+0x08  0x898 = 2200      <- 같은 키
+0x10  "214752659767408"
+0x30  "214752659767409"
+0x40  04 00 2D 00 37 00 43 00 44 00   <- UTF-16 "-7CD" (앞 4는 길이)
```

## 현지화 표

이름을 풀려면 한 겹 더 필요하고, 표는 이미 찾았다.

```
StaticInfoManager2<StringInfoKey, StringInfo, StringInfoManager, G>
StaticInfoManager2<LocalStringInfoKey, LocalStringInfo, LocalStringInfoManager, G>
pa::LocalizationStringBase
pa::LocalizationKey
```

## 다음에 이어갈 순서

1. `StringInfoManager` / `LocalStringInfoManager` 인스턴스를 찾아
   현지화 ID 조회 경로를 확인한다.
2. 키 2200 의 이름을 실제로 뽑아 검증한다. **화면에 보이는 이름과
   맞는지 확인하기 전에는 됐다고 하지 않는다.**
3. 오버레이에 검색 가능한 아이템 목록을 만든다.
4. `ClientInventoryActorComponent` 와 `ItemPushInventory` 지급 경로를
   조사한다.

여기까지 전부 읽기 전용이었고 게임을 한 번도 죽이지 않았다.
