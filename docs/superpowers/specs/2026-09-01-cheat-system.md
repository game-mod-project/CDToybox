# 게임에 남아 있는 개발자 치트 시스템

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
