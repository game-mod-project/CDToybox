# 디버그 콘솔 실험 (2026-09-03)

> **정리 (2026-09-21) — ⏳ 보류 · 닫힌 길.** 콘솔 UI 인스턴스가 0개라 어떤 플래그도
> 소비되지 않는다는 결론은 그대로다. `STATUS.md` §2.2.1 은 보류, `TROUBLESHOOTING.md`
> §9 는 닫힌 길로 둔다 — 다시 파지 말 것.

`specs/2026-09-01-cheat-system.md` 의 "디버그 콘솔" 절에 적힌 다음 단계를
실제로 밟았다. 게임을 켜고 전역을 읽고, 콘솔 객체를 찾고, 플래그를 한
바이트씩 써 봤다. **결론: 콘솔 코드와 UI 컨트롤러는 통째로 남아 있지만
페이지 인스턴스가 없어 어떤 플래그도 소비되지 않는다.** 살리려면 UI
페이지를 만드는 경로를 찾아야 한다.

## 1. 문서의 오류 두 가지

**전역 주소.** 앞 문서는 `0x1465BB288` 이라 적었는데 디스어셈블로 다시
보니 `0x14625B288` (RVA `0x625B288`) 이다. 두 자리가 바뀌어 적혔다.
`xref_data.py` 로 `0x65BB288` 을 찾으면 0곳이고, `0x625B288` 은 448곳이다.

**`+0x50` 은 콘솔 표시 플래그가 아니다.** `ShowDebugConsole` 콜백이 얻는
객체(`앱+0x110`) 는 콘솔이 아니라 **창·커서 관리자**다. 그 `+0x40` 이
게임 창 HWND(`WindowsLauncherClassName`, "Crimson Desert"), `+0x48/+0x4C`
가 커서 좌표, `+0x50` 이 **마우스 커서 표시 여부**다. vtable `+0x48` 은
`ClipCursor(NULL)` -> `ClientToScreen` -> `SetCursorPos` 를 부르는 "커서
보이기"다. 실측에서 메뉴 화면이라 이미 1 이었다.

## 2. 실행 중에 확인한 것

| 항목 | 값 |
|---|---|
| 전역 `RVA 0x625B288` | **널이 아니다** (`0x2C1255ADCF0`). 등록 코드가 돈다 |
| 핸들러 노드 | 힙에 1개. `{vtbl 0x144D08910, ctx, 함수 0x14050B650, id}` |
| ctx | `pa::WinAppLauncher` (RTTI 확인) |
| `ctx+0x08` | 앱 객체 (vtable `0x1453E0008`, RTTI 없음) |
| `ctx+0x10` | 핸들 객체 -> `[0]` -> `+0x28` -> **상태 객체 Y** |
| `앱+0x110` | 창·커서 관리자 (1 참고) |
| `Y+0x118` | 0 ("콘솔 사용 가능"으로 추정) |
| `Y+0x119` | 0 ("표시 요청"으로 추정) |
| `pa::GameConsoleCommandHandler` 인스턴스 | **0개** |
| `UiDebugCommandConsole` 컨트롤러 인스턴스 (vtable `0x1453EEE28`) | **0개** — 메뉴·인게임 모두 |

핸들러 노드는 `probe heapptr 0x14050B650` 으로 찾는다. 노드 `-0x10` 이
노드 시작이고 `+0x08` 이 ctx 다.

## 3. `ShowDebugConsole` 콜백 (RVA `0x50B650`) 이 하는 일

```
창 = 앱->vtbl[0xD8]()                       ; [앱+0x110]
if 창 && 창->vtbl[0x28]() && 창+0x50 != 1:  ; 표시 가능 && 커서 숨김 상태
    창->vtbl[0x48](-1)                       ; 커서 보이기
    창+0x50 = 1
    이벤트버스(창+0x30).post(IEvent{타입 0x62DDB90, 표시=1, x, y})
Y = [[ctx+0x10]] + 0x28
if Y+0x118: Y+0x119 = 1
```

이벤트 타입 `0x62DDB90` 은 콘솔 전용이 아니다. 일반 커서 표시 함수
(`0x94B430`) 도 같은 이벤트를 던진다. **즉 콜백은 커서를 보이고 `+0x119`
를 세울 뿐, 콘솔 UI 를 만들지 않는다.** `HideDebugConsole`(`0x50B750`) 은
커서를 숨기고 `+0x119` 를 무조건 세운다.

## 4. 콘솔 UI 는 실제로 있다

`UiDebugCommandConsole` 은 UI 컨트롤러 클래스 등록 표(함수 `0x2FB2660`,
checkbox · UIModal · UITreeView 등과 나란히)에 이름 해시로 등록돼 있고
팩토리는 `0x2FC3E60`, vtable 은 `0x53EEE28` 이다. 초기화 함수
`0x32F5C70` 이 다음을 묶는다.

| 위젯 / 이벤트 | 처리기 | 하는 일 |
|---|---|---|
| `cpp-command-input-field` | — | 입력창 (`this+0x40`) |
| `IInputTextEvent_RETURN_PRESSED` | `0x32F61C0` | 입력 문자열을 읽어 명령 실행. `[this+0x28]+0x118` 을 본다 |
| `IInputTextEvent_CANCEL_EDIT` | `0x32F62C0` | 백틱 한 글자면 지우고 `+0x50 = 0` |
| `IInputTextEvent_TEXT_EDITED` | `0x32F6300` | `[this+0x28]+0x119 = 1` |
| `IInputTextEvent_COMPLETE_WORD_PRESSED` | `0x32F6310` | 자동 완성 |
| `active` | `0x32F6500` | 키 처리. 0x26/0x28 위아래 = 히스토리, **0xC0 백틱 = `+0x119 = 1`** |

즉 컨트롤러의 `+0x28` 이 상태 객체 Y 이고, `+0x118/+0x119` 는 Y 의
콘솔 플래그가 맞다. 관련 문자열도 한 덩어리로 남아 있다.

```
/Root/CommandHistory/History_%#[@Command]
There are no proper commands
========== Candidate Command List ==========
CommandConsoleLogTextLine   cpp-command-log-wrap   cpp-command-input-field
.cpp-command-debug-button   cpp-debug-text-button  cpp-command-input-wrap
DebugConsole                /Root/CommandHistory
```

`DebugConsole` 은 `pa::GlobalVariableString` 으로 정적 등록된다
(`0x2816C0`, 해시 `0x879F7F45`, 전역 `0x6315A80`). 페이지 이름으로 보인다.

## 5. 플래그를 써 봤다 (안 된다)

인게임 상태에서 한 바이트씩 썼고 화면(`PrintWindow` 캡처)과 바이트를
전후로 비교했다.

| 쓴 것 | 3초 뒤 | 화면 |
|---|---|---|
| `Y+0x119 = 1` | 그대로 1 (소비 안 됨) | 변화 없음 |
| `Y+0x118 = 1` (위에 더해) | 둘 다 그대로 1 | 변화 없음 |

둘 다 0 으로 되돌렸다. **아무도 이 바이트를 읽지 않는다** — 읽는 코드는
`UiDebugCommandConsole` 컨트롤러 안에 있는데 그 인스턴스가 없다.

실행 파일 전체에서 `[reg+0x28]` 뒤에 `+0x118/+0x119` 바이트를 만지는
사슬은 위 콜백 둘과 컨트롤러 처리기 셋, 그리고 무관한 렌더 옵션
setter(`0x2C37100`, 오프셋 우연 일치)뿐이다.

## 6. 남은 길

콘솔을 띄우려면 **`UiDebugCommandConsole` 컨트롤러를 가진 UI 페이지를
만들어야** 한다. 후보 순서:

1. `DebugConsole` 이름(해시 `0x879F7F45`)으로 페이지를 여는 UI 매니저
   경로를 찾는다. `0x2FB4654` 의 `0x3809D0(매니저, &해시)` 가 "이름으로
   컨트롤러 클래스 찾기"이고, 그 매니저(`rdi`)가 페이지 생성 API 를
   가질 것이다.
2. 페이지 레이아웃 자산이 PAZ 에 남아 있는지 본다. `.pamt` 색인은
   이름이 해시라 문자열로는 못 찾는다. 해시 함수는 `0x10D4AF0`
   (시드 `0x2FFFF`) 이다.
3. 자산이 없으면 컨트롤러만 팩토리(`0x2FC3E60`)로 만들어 붙일 수 있는지
   본다 — 입력창 `cpp-command-input-field` 를 못 찾으면 `0x32F5C70` 이
   그냥 반환하므로 빈 페이지에서는 동작하지 않는다.

콘솔이 열려도 `GameConsoleCommandHandler` 인스턴스가 없으므로 명령
처리기 쪽도 따로 살아 있어야 한다. `EngineConsoleCommandHandler` 는
문자열만 있고 RTTI 클래스가 없다.

## 배운 것

- **주소를 옮겨 적을 때는 xref 로 되확인한다.** 두 자리 바뀐 주소를
  검증 없이 두 문서가 이어받았다.
- **콜백이 "무엇을 얻는지"는 그 객체의 필드로 판정한다.** `+0x40` 이
  HWND 인 것을 보고서야 콘솔이 아니라 창 관리자임을 알았다.
- 인스턴스 0개는 `probe heapptr <vtable VA>` 한 줄로 확인된다. 플래그를
  쓰기 전에 그것부터 볼 것.
- 게임 창이 배경이면 `CopyFromScreen` 은 다른 창을 찍는다.
  `PrintWindow(hwnd, hdc, 2)` 는 배경에서도 게임 화면을 준다.
