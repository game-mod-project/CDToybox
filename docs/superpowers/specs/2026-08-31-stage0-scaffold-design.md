# CDToybox 0단계 설계 — 오버레이 + 스캐너 골대

> **정리 (2026-09-21) — 📐 설계 → 구현됨.** 프록시 · D3D12 훅 · ImGui 오버레이 · 스캐너
> 골대는 들어왔다(`STATUS.md` §1.1 · §1.2, 계획 `plans/2026-08-31-stage0-scaffold.md`).
> 여기서 달라진 것 셋 — 큐를 `ExecuteCommandLists` 첫 호출에서 잡는 설계는 틀려
> 걷어냈고(`2026-08-31-d3d12-overlay-research.md` §2 · §5), 단축키는 `End` 가 아니라
> 켜기 `Insert` · 비활성화 `F10` 이며, 실행 코드는 `.text1` 한 곳이 아니다(섹션 배치는
> `STATUS.md` §2.1).

- 작성일: 2026-08-31
- 대상: Crimson Desert Enhanced 2.00.01 (Steam AppID 3321460, buildid 24994088)
- 범위: 0단계(기반 골대)만. 게임 내부 구조 역공학은 1단계 이후.

## 1. 목표와 비목표

### 목표

게임 프로세스 안에서 **우리 코드가 돌고, 화면에 UI를 그리고, 메모리를 패턴으로 탐색할 수 있다**는 것을 증명하는 최소 기반을 만든다. 이 기반 위에서 이후 모든 기능이 구현된다.

### 비목표 (0단계에서 하지 않는 것)

- 게임 자료구조 역공학 (플레이어 좌표, 인벤토리 등)
- 게임 상태 읽기/쓰기
- 기능 메뉴 (치트, 텔레포트, 아이템)
- 다국어, 배포 패키징, 자동 업데이트

개인용 프로젝트이므로 라이선스 파일, 공개 배포 문서, i18n 스캐폴딩은 만들지 않는다.

## 2. 대상 환경 (실측)

| 항목 | 값 | 확인 방법 |
|---|---|---|
| 실행 파일 | `bin64/CrimsonDesert.exe` (363,522,456 B) | 파일 시스템 |
| 게임 버전 | 2.00.01 | 런처 로그 `GameVersion: 2.00.01` |
| 엔진 | BlackSpace (Pearl Abyss 자체) | sentry `environment: blackspace_release` |
| 렌더러 | D3D12 (+ DXGI, Agility SDK `d3d12/d3d12core.dll`) | import 테이블 |
| 업스케일러 | NVIDIA Streamline (`sl.interposer.dll`), DLSS/XeSS/FSR | import 테이블 + bin64 |
| DRM | Denuvo Anti-Tamper | PE 섹션 비표준 구성 + 바이너리 내 DENUVO 문자열 |
| 안티치트 | 없음 | bin64에 EAC/BattlEye 부재 |
| 표시 모드 | Fullscreen | `user_engine_option_save.xml` 의 `_displayType` |

### PE 섹션 구조 (스캐너 설계에 직결)

```
.rdata   vsize=0x0496b000
.text1   vsize=0x014fc000   <-- 실행 코드 본체 (약 21MB)
.00cfg   vsize=0x0083d000
.sdata   vsize=0x002b4000
.text    vsize=0x00003000   <-- 12KB. 실질적으로 비어 있음
.data2 / .idata / .sbss / .udata / .didata / .xpdata / .arch
```

**Denuvo가 섹션을 재배치해 실행 코드가 `.text`가 아니라 `.text1`에 있다.** 관례적으로 `.text`만 스캔하는 구현은 이 게임에서 아무것도 찾지 못한다. 스캐너는 반드시 **섹션 이름이 아니라 `IMAGE_SCN_MEM_EXECUTE` 특성 플래그**로 스캔 범위를 결정해야 한다.

### 빌드 도구 (실측, 추가 설치 불필요)

| 도구 | 경로 |
|---|---|
| MSVC 14.44.35207 | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\` |
| vcvarsall.bat | `...\BuildTools\VC\Auxiliary\Build\vcvarsall.bat` |
| CMake (번들) | `...\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| Ninja (번들) | `...\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` |
| Windows SDK | `C:\Program Files (x86)\Windows Kits\10\` (10.0.26100.0, 헤더+Lib) |

x64 DLL / C++20 컴파일은 사전 검증 완료.

## 3. 아키텍처

네 개의 층이 한 방향으로만 의존한다. 아래층은 위층을 알지 못한다.

```
  proxy   (진입)      게임이 우리 DLL을 로드하게 만든다
    |
  core    (기반)      로그 · 설정 · 가드
    |
  mem     (하부)      모듈 조회 · 패턴 스캔 · 훅 설치
    |
  render / input      D3D12 오버레이 · 입력 라우팅
```

`mem`은 게임에 대한 지식을 전혀 갖지 않는다. 1단계 이후의 모든 역공학 작업이 이 인터페이스 위에서만 이뤄지도록 격리한다.

### 3.1 진입 — XInput 프록시

**선택 근거.** 후보를 게임 import 테이블(총 92개 DLL)로 실측 비교했다.

| 후보 | 게임이 import하는 함수 | 판정 |
|---|---|---|
| `dinput8.dll` | 없음 | **불가.** import 테이블에 없어 로드되지 않는다 |
| `XINPUT1_4.dll` | ordinal 2, 3 | **채택.** 렌더 체인과 무관, 포워딩 최소 |
| `dxgi.dll` | `CreateDXGIFactory1`, `CreateDXGIFactory2` | 보류. `sl.interposer.dll`이 DXGI를 감싸 간섭 위험 |
| `d3d12.dll` | 5개 | 보류. Agility SDK(`d3d12/d3d12core.dll`) 로딩과 충돌 위험 |
| `WINMM.dll` | 27개 | 보류. 기존 ASI 로더와 파일명 충돌 |

프록시의 역할은 "우리 DLL을 프로세스에 넣는 것" 뿐이고 렌더 후킹은 별도 vtable 방식이므로, 렌더 경로에 개입하지 않는 XInput이 가장 안전하다.

**포워딩 대상.** 시스템 `C:\Windows\System32\XINPUT1_4.dll`의 export를 실측했다: named 8개(@1 DllMain 포함) + NONAME ordinal 7개(@100~@109). 게임은 @2·@3만 사용한다.

우리가 export할 함수는 **7개**다.

| Ordinal | 이름 |
|---|---|
| @2 | `XInputGetState` |
| @3 | `XInputSetState` |
| @4 | `XInputGetCapabilities` |
| @5 | `XInputEnable` |
| @7 | `XInputGetBatteryInformation` |
| @8 | `XInputGetKeystroke` |
| @10 | `XInputGetAudioDeviceIds` |

`@1 DllMain`은 export하지 않는다(우리 DllMain은 별개다). `@6`·`@9`는 시스템 DLL에도 존재하지 않는다. NONAME ordinal 7개는 시그니처가 공개되지 않았고 게임도 쓰지 않으므로 생략한다.

**원본 로드.** `GetSystemDirectoryW()`로 얻은 절대경로 뒤에 `XINPUT1_4.dll`을 붙여 `LoadLibraryW`한다. 상대 경로를 쓰면 애플리케이션 디렉터리가 먼저 검색되어 **자기 자신을 다시 로드**하므로 절대 사용하지 않는다.

원본 로드나 `GetProcAddress`가 실패하면 각 스텁은 `ERROR_DEVICE_NOT_CONNECTED`(1167)를 반환한다. 게임은 컨트롤러 없음으로 인식하고 정상 진행한다.

**DllMain.** 로더 락 안에서는 최소 작업만 한다.

1. `DisableThreadLibraryCalls`
2. 원본 DLL 로드 + 함수 포인터 7개 확보
3. 초기화 워커 스레드 생성 후 즉시 반환

로그 파일 생성·훅 설치·D3D12 객체 생성은 전부 워커 스레드에서 한다.

### 3.2 렌더 — D3D12 vtable 후킹

**접근 비교.**

- **A. 더미 객체 vtable 후킹 — 채택.** 임시 D3D12 디바이스·커맨드큐·스왑체인을 만들어 vtable에서 함수 주소를 읽고 MinHook로 트램폴린을 건다. vtable은 D3D12/DXGI 런타임 소속이라 **게임 패치와 무관하게 안정적**이다. 이 층은 한 번 만들면 게임 업데이트로 깨지지 않는다.
- **B. kiero 등 기존 라이브러리 벤더링.** 코드는 짧아지나 의존성이 늘고, 문제 발생 시 내부를 알기 어렵다.
- **C. 외부 투명 창 오버레이 — 탈락.** 표시 모드가 Fullscreen이라 게임 위에 보이지 않는다.

**후킹 대상 3개.**

| 인터페이스 | 함수 | vtable 인덱스 |
|---|---|---|
| `IDXGISwapChain` | `Present` | 8 |
| `IDXGISwapChain` | `ResizeBuffers` | 13 |
| `ID3D12CommandQueue` | `ExecuteCommandLists` | 10 |

`ExecuteCommandLists`를 후킹하는 이유는 ImGui DX12 백엔드가 **게임의 커맨드큐**를 필요로 하는데 SwapChain만으로는 얻을 수 없기 때문이다. 첫 호출 시 큐 포인터를 캡처하고, 이후에는 원본을 그대로 통과시킨다.

**더미 객체 생성 절차.**

1. `CreateWindowExW`로 1x1 크기의 보이지 않는 임시 창 생성
2. `D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, ...)`
3. `ID3D12Device::CreateCommandQueue` (DIRECT 타입)
4. `CreateDXGIFactory1` 로 팩토리 생성 후 `CreateSwapChain`
5. 세 객체의 vtable에서 위 3개 주소를 읽음
6. 객체 전부 해제, 임시 창 파괴 — 주소만 남긴다

**Present 훅 동작.**

최초 1회:

1. SwapChain에서 `ID3D12Device`와 `DXGI_SWAP_CHAIN_DESC`(백버퍼 개수, HWND) 획득
2. RTV descriptor heap(백버퍼 수만큼), SRV descriptor heap(ImGui 폰트용) 생성
3. 백버퍼 수만큼 `ID3D12CommandAllocator` 생성, `ID3D12GraphicsCommandList` 1개 생성
4. `ImGui::CreateContext` 이후 `ImGui_ImplWin32_Init(hwnd)` 와 `ImGui_ImplDX12_Init` 호출
5. WndProc 서브클래싱 설치 (3.3 참조)

매 프레임:

1. `ImGui_ImplDX12_NewFrame` / `ImGui_ImplWin32_NewFrame` / `ImGui::NewFrame`
2. UI 구성
3. `ImGui::Render`
4. 현재 백버퍼 인덱스의 allocator를 리셋, 커맨드리스트 기록, 리소스 배리어 전환
5. 캡처해 둔 커맨드큐로 `ExecuteCommandLists`
6. 원본 `Present` 호출

**ResizeBuffers 훅.** RTV·백버퍼 참조를 해제하고 원본을 호출한 뒤 재생성한다. 해상도 변경이나 전체화면 전환 시 크래시를 막는 유일한 경로다.

### 3.3 입력

SwapChain의 `GetDesc`로 얻은 HWND에 `SetWindowLongPtrW`로 WndProc을 서브클래싱한다.

- 항상 `ImGui_ImplWin32_WndProcHandler`에 메시지를 먼저 전달한다.
- 오버레이가 **열려 있을 때만** 마우스·키보드 메시지를 게임에 전달하지 않고 소비한다.
- 토글 키는 기본 `Insert`(VK 0x2D). `CDToybox.ini`에서 변경 가능하다.

XInput 프록시가 `XInputGetState`를 이미 경유하므로 컨트롤러 입력 가로채기는 추가 후킹 없이 가능하다. 다만 0단계에서는 원본을 그대로 통과시키기만 하고 실제 사용은 하지 않는다.

### 3.4 메모리

**`mem/module`** — 모듈 베이스와 스캔 범위를 제공한다. 섹션 순회 시 `IMAGE_SCN_MEM_EXECUTE`가 설정된 섹션만 반환한다(2절의 `.text1` 문제).

**`mem/scanner`** — IDA 스타일 패턴 문자열을 받는다. 예: `48 8B 05 ?? ?? ?? ?? 48 85 C0`. `??`는 임의 바이트다. API는 다음 셋이다.

- `find_first(range, pattern)` — 첫 일치 주소. 없으면 0
- `find_all(range, pattern, max)` — 일치 주소 목록
- `parse(pattern)` — 파싱 결과. 검증·테스트용

**`mem/hook`** — MinHook의 RAII 래퍼. 생성 시 설치, 소멸 시 해제. 설치 실패는 예외가 아니라 상태 플래그로 보고한다.

## 4. 데이터 흐름

```
게임 시작
  -> 로더가 bin64\xinput1_4.dll 로드 (정적 import)
  -> DllMain: 원본 로드, 워커 스레드 생성, 즉시 반환
  -> [워커] 로그 초기화 -> INI 로드
  -> [워커] 더미 D3D12 객체로 vtable 주소 3개 획득 -> 해제
  -> [워커] MinHook 설치 3개 -> 종료
  -> [게임 스레드] ExecuteCommandLists 최초 호출 -> 커맨드큐 캡처
  -> [게임 스레드] Present 최초 호출 -> ImGui 초기화 + WndProc 서브클래싱
  -> [게임 스레드] Present 매 호출 -> 오버레이 렌더
  -> Insert 키 -> 오버레이 표시 토글
```

## 5. 에러 처리와 안전장치

**최우선 원칙: 어떤 실패도 게임을 죽이지 않는다.** 훅 설치 실패, D3D12 객체 생성 실패, ImGui 초기화 실패는 모두 로그를 남기고 오버레이 없이 게임이 정상 동작하게 둔다.

- **로그.** `bin64\CDToybox.log`에 타임스탬프와 레벨로 기록한다. 크래시 원인 추적의 유일한 수단이므로 초기화 각 단계를 빠짐없이 남긴다.
- **훅 본문 보호.** `Present`·`ExecuteCommandLists` 훅 본문은 `__try`/`__except`로 감싼다. 예외 발생 시 오버레이를 영구 비활성화하고 원본만 호출한다. 게임 렌더 스레드에서 예외가 전파되면 즉시 크래시다.
  - MSVC는 소멸자를 가진 C++ 객체가 있는 함수에서 `__try`/`__except`를 허용하지 않는다(C2712). 따라서 훅 함수는 **SEH 껍데기와 실제 본문을 분리**한다: 훅 진입점은 C++ 객체 없이 `__try`만 두고, 렌더링 로직은 별도 함수로 호출한다.
- **초기화 1회 보장.** `Present` 훅의 최초 초기화는 원자적 플래그로 보호한다. D3D12는 멀티스레드 Present가 가능하다.
- **온라인 가드.** `core/guard`에 인터페이스만 정의하고 0단계에서는 항상 "오프라인"을 반환하는 stub으로 둔다. 게임이 `PartyManager`·`GuildManager`·`LinkingCheckAsync` 등 온라인 인프라를 갖고 있음이 런처 로그에서 확인되므로, 1단계에서 실제 판정을 구현하기 전까지는 **게임 상태를 쓰는 기능을 일절 만들지 않는다.**

**언로드에 관한 정정.** 프록시 DLL은 게임이 정적 import했으므로 `FreeLibrary`로 언로드할 수 없다. 따라서 "언로드"는 **기능 비활성화**를 뜻한다: 훅 해제 → ImGui 컨텍스트 파괴 → D3D12 리소스 해제 → WndProc 원복. 이후 재초기화가 가능해야 한다. 이것이 있어야 게임 재시작 없이 반복 테스트할 수 있다. 기본 단축키는 `End`.

## 6. 검증 전략

### 6.1 자동 테스트 — 스캐너

`mem/scanner`는 게임 없이 검증 가능한 유일한 모듈이므로 TDD로 만든다. 별도 콘솔 테스트 실행 파일 `cdtb_tests`를 CMake 타깃으로 둔다.

| 케이스 | 기대 |
|---|---|
| 패턴 파싱 `48 8B 05` | 길이 3, 와일드카드 없음 |
| 패턴 파싱 `48 ?? 05` | 길이 3, 인덱스 1이 와일드카드 |
| 패턴 파싱 실패 입력 (`4`, `ZZ`, 빈 문자열) | 실패 반환 |
| 버퍼 선두 일치 | 오프셋 0 |
| 버퍼 말미 일치 | 마지막 오프셋 |
| 불일치 | 0 반환 |
| 와일드카드 포함 일치 | 정확한 오프셋 |
| 경계 초과(패턴이 버퍼보다 김) | 0 반환 |
| `find_all` 다중 일치 | 전부 발견, 순서 보장 |

### 6.2 인게임 검증 — 스캐너 자체 진단

0단계에서 게임 자료구조를 모르므로, 스캐너의 정확성을 **게임 지식 없이** 증명한다. 오버레이가 두 가지를 표시한다.

1. **자기 모듈 마커 탐색.** `xinput1_4.dll` 안에 고유한 16바이트 상수 배열을 심고, 스캐너로 자기 모듈에서 그것을 찾는다. 실제 주소와 일치하면 정확성이 증명된다.
2. **게임 모듈 프롤로그 탐색.** `CrimsonDesert.exe`의 실행 섹션에서 흔한 함수 프롤로그 `48 89 5C 24 08 57 48 83 EC 20`을 `find_all`로 세고, 히트 수와 소요 시간(ms)을 표시한다. 21MB 실행 섹션에서 실용적 속도로 동작함을 증명한다.

이 두 가지로 역공학 없이 0단계가 닫히고, 소요 시간이 예측 가능해진다.

### 6.3 0단계 완료 기준 (수동 체크리스트)

1. 빌드가 성공하고 `xinput1_4.dll`이 생성된다.
2. `cdtb_tests`가 전부 통과한다.
3. bin64 배치 후 **게임이 정상 실행**된다. 컨트롤러가 연결돼 있다면 그대로 동작한다.
4. `Insert`로 오버레이가 열리고 닫힌다.
5. 오버레이에 게임 버전, 모듈 베이스, 실행 섹션 범위, 6.2의 두 진단 결과가 표시된다.
6. `End`로 비활성화 후 `Insert`로 재활성화해도 크래시하지 않는다.
7. `bin64\CDToybox.log`에 초기화 전 단계가 기록돼 있다.

## 7. 디렉터리 구조

```
E:\CDToybox\
├─ CMakeLists.txt
├─ .gitignore
├─ README.md
├─ docs\superpowers\specs\
├─ external\
│  ├─ imgui\            Dear ImGui (MIT, submodule)
│  └─ minhook\          MinHook (BSD-2-Clause, submodule)
├─ src\
│  ├─ dllmain.cpp
│  ├─ proxy\    xinput_proxy.cpp · xinput_proxy.h · exports.def
│  ├─ core\     log · config · guard
│  ├─ mem\      module · scanner · hook
│  ├─ render\   d3d12_hook · overlay
│  └─ input\    wndproc
├─ tests\       scanner_tests.cpp
└─ scripts\     deploy.ps1 (산출물 → bin64 복사)
```

## 8. 빌드

- 표준 C++20, MSVC 14.44 x64, 생성기 Ninja.
- CMake와 Ninja는 PATH에 없으므로 VS Build Tools 번들 절대경로를 사용한다.
- `vcvarsall.bat x64`로 환경을 활성화한 뒤 구성·빌드한다.
- 산출물 이름은 반드시 `xinput1_4.dll`이어야 한다(`OUTPUT_NAME` 지정).
- 테스트 타깃 `cdtb_tests`는 별도 실행 파일로 빌드한다.

## 9. 리스크

| # | 리스크 | 영향 | 완화 |
|---|---|---|---|
| 1 | NVIDIA Streamline이 SwapChain을 감싸 Present 훅이 예상과 다른 위치에 걸림 | 오버레이 미표시 또는 깜빡임 | 훅 진입 시 SwapChain 포인터를 로그로 남겨 실제 체인 확인. DLSS-G는 현재 GPU 하드웨어 스케줄링 미설정으로 초기화 실패 중이라 당장 무관 |
| 2 | Denuvo가 신규 DLL 로드나 후킹에 반응 | 게임 실행 불가 | ASI 로더가 이미 정상 동작함이 실증. 발생 시 프록시 대상을 `dxgi.dll`로 교체 |
| 3 | 게임 패치로 스캐너 패턴 무효화 | 1단계 이후 기능 정지 | vtable 후킹 층은 D3D12 런타임 소속이라 무관. 패턴은 재작업 대상으로 전제 |
| 4 | 전체화면 전환·해상도 변경 시 리소스 무효화 | 크래시 | `ResizeBuffers` 훅에서 재생성 (3.2) |
| 5 | `external/` 의존성 취득 실패 | 빌드 불가 | ImGui·MinHook을 `git submodule`로 고정 취득. 최초 1회 네트워크 접근 필요 |

## 10. 이후 단계와의 접점

0단계가 남기는 인터페이스가 1단계 이후를 결정한다.

- `mem/scanner` + `mem/module` — 모든 역공학 작업의 진입점
- `mem/hook` — 게임 함수 후킹의 유일한 통로
- `render/overlay` — 기능 UI가 붙을 자리
- `core/guard` — 게임 상태를 쓰는 모든 기능이 통과해야 할 관문

1단계(카메라 제어)는 `mem`으로 대상을 찾고, `hook`으로 걸고, `overlay`에 UI를 붙이고, `guard`를 통과시킨다. 0단계에서 이 네 접점이 실제로 동작함을 확인하는 것이 목적이다.
