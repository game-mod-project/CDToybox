# 카메라 쓰기 경로 정적 분석

**날짜:** 2026-08-31
**대상:** CrimsonDesert.exe (Enhanced 2.00.01), 이미지 베이스 0x140000000
**방법:** PE 파싱 + 바이트 패턴 스캔. 디버거·디스어셈블러 없이 파일만 읽음.

## 출발점

인게임 하드웨어 브레이크포인트가 활성 카메라 `+0xA4` 를 쓰는 명령
하나를 잡았다: RIP `0x140A59516`. 데이터 브레이크포인트는 쓰기가
**끝난 뒤** 보고하므로 실제로 쓴 명령은 그 앞이다.

```asm
0x140A594FB  48 8B 83 88 00 00 00   mov   rax, [rbx+0x88]
0x140A59502  48 8D 48 7C            lea   rcx, [rax+0x7C]
0x140A59506  BA A4 00 00 00         mov   edx, 0xA4        ; null 폴백
0x140A5950B  48 85 C0               test  rax, rax
0x140A5950E  48 0F 44 CA            cmovz rcx, rdx
0x140A59512  C5 FA 11 09            vmovss [rcx], xmm1     ; <- 실제 쓰기
0x140A59516  0F B6 97 80 00 00 00   movzx edx, [rdi+0x80]  ; <- 보고된 RIP
```

## 핵심: +0x28 어긋남

감시 주소는 `active + 0xA4` 였는데 명령은 `rax + 0x7C` 에 썼다.
따라서 **`rax == active + 0x28`** 이다.

이는 탐색 로직과 일치한다 — `discover_with()` 는
`PlayerCameraComponent + 0x88` 을 읽어 **0x28 을 빼서** 카메라를
얻는다. 즉 `rbx == PlayerCameraComponent` 이고,
`[rbx+0x88]` 은 카메라 객체의 내부 포인터(카메라+0x28)다.

**이 함수에서 본 오프셋은 전부 +0x28 을 더해야 카메라 기준이 된다.**

## 함수 0x140A59230 ~ 0x140A5976C

int3 패딩으로 잡은 경계. 널 안전 필드 복사 관용구가 12번 반복된다:

```asm
mov   rax, [rbx+0x88]        ; 대상 카메라(+0x28)
lea   rcx, [rax+OFFSET]      ; disp8 또는 disp32
mov   edx, OFFSET            ; rax 가 null 이면 쓸 더미 주소
test  rax, rax
cmovz rcx, rdx
vmovss [rcx], xmmN           ; 또는 mov [rcx], dl
```

| 함수 기준 | 카메라 기준 | 소스 | 종류 |
|---|---|---|---|
| +0x78 | +0xA0 | `[rdi+0x80]` | byte |
| +0x7C | **+0xA4** | `[rdi+0x70]` | float — 실측 히트 지점 |
| +0x80 | +0xA8 | | float |
| +0x84 | +0xAC | `[rdi+0x78]` | float |
| +0x88 | +0xB0 | | float |
| +0x8C | +0xB4 | | float (근접평면) |
| +0x94 | +0xBC | `[rdi+0x9A]` | byte |
| +0xA4 | +0xCC | | float |
| +0xC0 | +0xE8 | `[rdi+0x54]` | |
| +0xCC | +0xF4 | `[rdi+0x60]` | |
| +0xD8 | +0x100 | | |

**FOV(+0x9C)도 위치(+0x6C)도 회전(+0x5C)도 이 함수에 없다.**

### 스캔 함정

처음에 `lea rcx,[rax+disp8]`(`48 8D 48 XX`)만 찾아 +0x7C 와 +0x78 만
나왔다. disp8 은 **부호 있는** 값이라 0x80 이상 오프셋은
`lea rcx,[rax+disp32]`(`48 8D 88 XX XX XX XX`)로 인코딩된다.
두 형태를 다 봐야 한다.

## 이미지 전수 조사

실행 가능 섹션(`.rdata` 76MB, `.sbss` 251MB) 전체에서 같은 관용구를
찾았다. 총 57곳.

| 카메라 오프셋 | 곳 | 대표 주소 |
|---|---|---|
| **+0x9C (FOV)** | 6 | 0x1409854ED, 0x1409855A5, 0x14098565D, 0x140985715 |
| +0xA8 | 8 | 0x1409854AD … |
| +0xAC | 8 | 0x1409854CD … |
| +0xB0 | 8 | 0x14098550A … |
| +0xA4 | 2 | 0x140A594FB, 0x142DCB90D |
| +0x50 (스케일) | 2 | 0x142DCDAB4, 0x1517F38C4 |
| **+0x5C (회전)** | 0 | — |
| **+0x6C (위치)** | 0 | — |

FOV 쓰기는 `+0xA8 → +0xAC → +0x9C → +0xB0` 순서로 연달아 나오고 그
블록이 4번 복제돼 있다(0x1409854AD, 0x140985565, 0x14098561D,
0x1409856D5). 하나의 "카메라 파라미터 적용" 루틴이다.

**위치와 회전은 이 경로(컴포넌트+0x88)로 설정되지 않는다.** 다른
방식이므로 정적으로는 더 좁힐 수 없다. 런타임 하드웨어
브레이크포인트로 찾아야 한다.

## FOV 히트 0회의 해석

이전 실행에서 FOV 감시가 히트 0회였는데, 정적으로는 쓰는 코드가
6곳 있다. 두 가지가 가능하다.

1. 그 코드가 8초 관찰 창 안에 돌지 않았다.
2. **감시 설치 후 만들어진 스레드가 썼다.**

2번은 실제 결함이었다. `WriteWatch::install()` 이 설치 시점의
스레드에만 디버그 레지스터를 걸고 이후 생성 스레드를 방치했다.
`refresh_threads()` 로 메웠고 회귀 테스트를 붙였다
(`watchpoint_catches_write_from_thread_created_after_install`).

## 프리카메라 활성화 단서 (문자열)

| 문자열 | 위치 | 뜻 |
|---|---|---|
| `eErrNoFreeCamModeEnabled` | 0x145091AD8 | 프리캠 모드 게이트가 존재 |
| `FREECAM_MODE_ACTION_XXX` | 0x1450645F0 | 액션 상태 이름 |
| `_debugCameraState` | 0x1452EF0F8 | 리플렉션 필드 |
| `debugCameraState` | 0x1452F2558 | 표시 이름 |
| `PearlAbyssEngine.Debug.DebugCameraState` | — | 스크립트 타입 |

`eErrNoFreeCamModeEnabled` 의 두 참조는 전부 **열거형 이름 등록
테이블**이라 런타임 게이트가 아니다.

`debugCameraState` 는 `0x142C76768` 에서 참조되고, 주변이
`_renderCollisionGeometry`·`_viewMode`·`showEngineOption`·
`B: Debug Information` 같은 **엔진 렌더 디버그 옵션 목록**이다.
출시 빌드에 개발자 옵션 UI가 남아 있다는 뜻이지만, 렌더 디버그
문맥이므로 컬링 카메라 고정에 가깝지 날아다니는 프리캠은 아니다.

참조 지점 디코딩:

```asm
0x142C7673C  lea  rdx, "_debugCameraState"
0x142C76743  lea  rcx, [0x1465E8EA0]
0x142C7674A  call 0x142C86260
...
0x142C76768  lea  rdx, "debugCameraState"
0x142C7676F  lea  rcx, [0x1465E8EC8]
0x142C76776  call 0x1403390F0
```

`0x1465E8EA0` / `0x1465E8EC8` 는 RVA 0x65E8Exx 로 **`.00cfg`(RW)
섹션의 초기화되지 않은 영역**이다(RawSize 0x354000 < 섹션 내
오프셋 0x780Exx). 파일에는 없고 런타임에만 존재하는 전역이다.

### 섹션 표 (Denuvo 로 뒤섞여 있음)

| 이름 | RVA | VirtSize | RawSize | 속성 |
|---|---|---|---|---|
| `.rdata` | 0x00001000 | 0x0496B000 | 0x0496B000 | **XR** |
| `.text1` | 0x0496C000 | 0x014FC000 | 0x014FC000 | R |
| `.00cfg` | 0x05E68000 | 0x0083D000 | 0x00354000 | RW |
| `.sbss` | 0x06B32000 | 0x0EFEE910 | 0x0EFEEA00 | **XRW** |
| `.text` | 0x06959000 | 0x00003000 | 0x00003000 | R |

실행 플래그는 `.rdata` 와 `.sbss` 에 있다. **섹션 이름으로
코드 영역을 고르면 안 된다** — `IMAGE_SCN_MEM_EXECUTE` 를 봐야 한다.

VirtSize > RawSize 인 섹션(`.00cfg`)에서 RVA→파일오프셋 변환에
`max(VirtSize, RawSize)` 를 쓰면 raw 밖 주소가 엉뚱한 파일 위치로
매핑된다. 실제로 한 번 쓰레기 값을 읽었다. RawSize 로 잘라야 한다.

## 다음 측정

정적으로는 여기까지다. 남은 것은 런타임에서만 나온다:

1. 위치·회전을 쓰는 명령 (4슬롯 동시 감시)
2. `CameraManager`/`FreeCamCamera` vtable 함수 주소 → 오프라인 정적 분석
3. `PlayerCameraComponent+0x88` 을 프리카메라로 바꿨을 때의 반응
