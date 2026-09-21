# D3D12 오버레이 구현 리서치 — 무엇을 틀렸고 무엇이 맞는가

> **정리 (2026-09-21) — ✅ 해결.** §5 의 두 결함 수정(스왑체인 생성 훅으로 큐를 확정 ·
> 펜스 동기화)이 들어갔고 오버레이는 완료·안정이다(`STATUS.md` §1.1,
> `TROUBLESHOOTING.md` §1.6).

- 작성일: 2026-08-31
- 계기: Task 8/9에서 오버레이가 렌더되다가 폰트 추가 후 `0xC0000005`로 죽음
- 대상: Crimson Desert 2.00.01 (D3D12 + NVIDIA Streamline + Denuvo)

## 1. 사후 분석 — 무엇을 잘못했나

### 1.1 절차 오류

**한 빌드에 두 변경을 묶었다.** Task 8은 스크린샷으로 정상 동작(64.6 FPS)이 확인된 상태였다. 그 다음 커밋에 한글 폰트 로드와 진단 UI 전체를 함께 넣었고, 깨졌을 때 어느 쪽이 원인인지 이분할 수 없었다. 계획서 자체가 "Task 8 검증 → Task 9"로 분리해 두었는데, 게임 재시작을 아끼려고 합쳤다.

교훈: 인게임 검증이 비싸다는 이유로 변경을 묶으면, 깨졌을 때 그 비용을 몇 배로 되돌려 받는다.

### 1.2 기술 결함 — 레퍼런스를 읽지 않고 기억으로 구현

ImGui 공식 DX12 예제를 `external/imgui/examples/`에 vendoring 해두고도 읽지 않았다. 읽었다면 즉시 드러났을 것들:

| 공식 예제 | 내 구현 |
|---|---|
| `FrameContext { CommandAllocator, FenceValue }` | `FrameCtx { allocator, back_buffer, rtv }` — **FenceValue 없음** |
| `WaitForNextFrameContext()` — 얼로케이터 재사용 전 펜스 대기 | **없음.** 바로 `Reset()` |
| 제출 후 `Signal(fence, ++v); ctx.FenceValue = v;` | **없음.** 펜스 자체가 없음 |
| `WaitForPendingOperations()` — RT 해제 전 GPU 완료 대기 | **없음.** 바로 `Release()` |

Microsoft 문서가 이것을 명시한다:

> The calling application is responsible for ensuring the GPU is not currently reading submitted command lists from a previous execution. **The runtime will drop the call and remove the device** if the command queue fence indicates a previous execution has not completed.

디바이스 제거는 이후 모든 D3D12 호출을 무효화한다. 증상(`0xC0000005`, 모듈 밖 주소)과 부합한다.

**왜 처음엔 동작했나.** 백버퍼 3개를 순환하면 같은 인덱스가 3프레임 뒤에 돌아오고, 그 사이 GPU가 대체로 작업을 끝낸다. 우연히 맞았을 뿐이다. 폰트 텍스처 업로드가 추가되면서 GPU 작업량과 타이밍이 바뀌자 전제가 깨졌다.

## 2. 커맨드큐 획득 — 설계 자체가 틀렸다

이것이 펜스 누락보다 앞선 1차 결함이다.

내 구현은 `ExecuteCommandLists`를 후킹해 **처음 본 DIRECT 타입 큐**를 잡아 ImGui에 넘겼다. NVIDIA Streamline 공식 문서가 이 가정을 정면으로 부정한다:

> When DLSS-G is active there could be **multiple command queues and multiple asynchronous presents**, so **overlays in general must not make assumptions about swap-chain and command queues**. Overlays should intercept `IDXGIFactory::CreateSwapChainXXX` to obtain the correct swap-chain and command queue used to present frames.

이 게임은 `bin64/sl.interposer.dll`을 정적 import 하며 DLSS/DLSS-G를 지원한다. 그리고 실측 로그가 큐가 둘 이상임을 보여준다:

```
커맨드큐 캡처:   0x8e2ba10     ← 우리가 잡아 ImGui에 넘긴 큐
재진입 로그 q:   0x8e2b050     ← 다른 DIRECT 큐
```

**올바른 방법**: `IDXGIFactory::CreateSwapChain`(vtable 10) 및 `IDXGIFactory2::CreateSwapChainForHwnd`(vtable 15)를 후킹한다. D3D12에서 이 함수들의 첫 인자 `pDevice`는 **커맨드큐**이며, 반환되는 스왑체인과 짝이 보장된다. 이로써 (큐, 스왑체인) 쌍을 추측이 아니라 사실로 얻는다.

Present 훅에서는 넘어온 스왑체인 포인터가 우리가 기록한 것과 같은지 대조해, 다른 스왑체인이면 건너뛴다.

**남은 불확실성**: Streamline 인터포저가 팩토리를 자체 프록시로 감싸면 게임이 보는 vtable과 실제 dxgi의 vtable이 다를 수 있다. 그 경우 우리는 실제 dxgi 계층에서 잡게 되는데, 이는 실제로 화면에 제시되는 스왑체인이므로 오히려 정확하다. 다만 **검증이 필요하다** — 훅에서 얻은 스왑체인 포인터와 Present로 넘어오는 포인터가 일치하는지 로그로 확인한다.

## 3. ImGui 1.92 특이사항

### 3.1 초기화 API 변경

`ImGui_ImplDX12_Init`의 위치인자 형태는 1.91.5에서 obsolete 되었고 `ImGui_ImplDX12_InitInfo` 구조체로 대체됐다. `CommandQueue` 필드가 추가된 것이 핵심이다 — 백엔드가 텍스처 업로드에 큐를 직접 쓴다.

### 3.2 SRV 디스크립터 할당자

1.92부터 폰트 아틀라스 외 텍스처에도 디스크립터가 필요하므로 `SrvDescriptorAllocFn`/`FreeFn` 콜백을 요구한다. 단일 디스크립터(legacy) 방식은 동적 폰트에서 부족하다. 우리는 64개 힙 + 프리리스트로 구현했고, 실측 결과 폰트 1개 로드 시 1회만 요청했다 — 이 부분은 정상이다.

### 3.3 텍스처 업로드가 우리 훅으로 재진입한다

`imgui_impl_dx12.cpp`의 업로드 경로(505~570행):

```cpp
bd->pTexCmdAllocator->Reset();
bd->pTexCmdList->Reset(bd->pTexCmdAllocator, nullptr);
... CopyTextureRegion ...
cmdList->Close();
ID3D12CommandQueue* cmdQueue = bd->pCommandQueue;
cmdQueue->ExecuteCommandLists(1, &cmdList);          // ← 우리가 후킹한 함수
cmdQueue->Signal(bd->Fence, ++bd->FenceLastSignaledValue);
bd->Fence->SetEventOnCompletion(...);
WaitForSingleObject(bd->FenceEvent, INFINITE);        // ← Present 안에서 GPU 무한 대기
```

두 가지 함의:

1. **우리 `ExecuteCommandLists` 훅이 재진입된다.** 훅 본문은 재진입 안전해야 한다(로깅·전역 카운터 조작 금지). 내 계측이 비원자적 전역 카운터를 써서 `depth=-6` 같은 무의미한 값을 냈다 — 계측 자체가 버그였다.
2. **게임의 `Present` 안에서 GPU를 무한 대기한다.** 폰트 업로드 1회면 감수할 만하지만, 매 프레임 발생하면 안 된다. 폰트는 초기화 시 한 번만 얹는다.

## 4. 이 게임의 특수 조건

| 조건 | 근거 | 함의 |
|---|---|---|
| NVIDIA Streamline | `sl.interposer.dll` 정적 import | 큐·스왑체인 가정 금지 |
| DIRECT 큐 복수 | 실측 로그 (`0x8e2ba10` vs `0x8e2b050`) | 큐를 추측으로 잡으면 안 됨 |
| 백버퍼 3개 | 실측 (`오버레이 초기화 완료: 백버퍼 3개`) | 프레임 컨텍스트 3개 |
| 포맷 `R8G8B8A8_UNORM`(28) | 실측 | HDR 특수 처리 불필요 |
| Fullscreen | `user_engine_option_save.xml` | 외부 창 오버레이 불가 |
| Denuvo | PE 섹션 재배치 | 렌더 후킹과는 무관(런타임 vtable) |
| 멀티스레드 렌더 | `ExecuteCommandLists` 동시 호출 관측 | 훅 본문은 스레드 안전해야 함 |

## 5. 수정 설계

### 5.1 커맨드큐 획득 (1차 결함)

- `CreateDXGIFactory1`로 더미 팩토리를 만들어 vtable에서 `CreateSwapChain`(10)과 `CreateSwapChainForHwnd`(15) 주소를 얻는다.
- 두 함수를 후킹해 `(pDevice=커맨드큐, 반환 스왑체인)` 쌍을 기록한다.
- Present 훅에서 스왑체인 포인터를 대조해 우리가 아는 쌍일 때만 렌더한다.
- `ExecuteCommandLists` 훅은 **제거한다.** 큐를 여기서 얻을 필요가 없어지고, 재진입 문제와 멀티스레드 경합이 함께 사라진다.

### 5.2 펜스 동기화 (2차 결함)

```
struct FrameCtx { ID3D12CommandAllocator* allocator; ID3D12Resource* back_buffer;
                  D3D12_CPU_DESCRIPTOR_HANDLE rtv; UINT64 fence_value; };

ID3D12Fence* g_fence;  HANDLE g_fence_event;  UINT64 g_fence_last = 0;

// 매 프레임, allocator->Reset() 전에
if (g_fence->GetCompletedValue() < f.fence_value) {
    g_fence->SetEventOnCompletion(f.fence_value, g_fence_event);
    WaitForSingleObject(g_fence_event, INFINITE);
}
...
queue->ExecuteCommandLists(1, lists);
queue->Signal(g_fence, ++g_fence_last);
f.fence_value = g_fence_last;

// 해체 / ResizeBuffers 전에는 전체 대기
queue->Signal(g_fence, ++g_fence_last);
g_fence->SetEventOnCompletion(g_fence_last, g_fence_event);
WaitForSingleObject(g_fence_event, INFINITE);
```

### 5.3 스레드 안전

- `g_frame_stage` 같은 진단 전역은 유지하되, 훅 본문에서 **로그를 찍지 않는다**(뮤텍스 + 파일 I/O를 렌더 핫패스에 두면 안 된다).
- 재진입 카운터가 필요하면 `thread_local`을 쓴다. 비원자적 전역 증감은 금지.

### 5.4 폰트

- 초기화 시 1회만 얹는다. 매 프레임 폰트 아틀라스를 건드리지 않는다.
- 파일 부재 시 기본 폰트로 폴백(이미 구현됨).

## 6. 재작업 시 검증 규칙

이번 실패의 직접 원인이 "변경을 묶은 것"이므로, 규칙으로 못박는다.

1. **인게임 검증이 필요한 변경은 한 번에 하나만 배포한다.** 두 개를 넣고 싶으면 두 번 배포한다.
2. 배포 전 `deploy.ps1`이 세 가지를 거부한다(이미 구현): 산출물이 소스보다 오래됨, 산출물 없음, 게임 실행 중.
3. 각 배포마다 로그를 지우고 시작해, 어느 실행의 기록인지 혼동하지 않는다.
4. 게임 실행은 사용자가 직접 한다. 자동 실행하지 않는다.

## 7. 참고 자료

- [ImGui 공식 DX12 예제](https://github.com/ocornut/imgui/blob/master/examples/example_win32_directx12/main.cpp) — `external/imgui/examples/example_win32_directx12/main.cpp`에 vendoring 되어 있음
- [ID3D12CommandQueue::ExecuteCommandLists (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists) — 디바이스 제거 조건
- [Executing and Synchronizing Command Lists (Microsoft Learn)](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)
- [NVIDIA Streamline ProgrammingGuide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md) — 오버레이는 큐·스왑체인을 가정하지 말고 `CreateSwapChainXXX`를 가로챌 것
- [DrNseven/D3D12-Hook-ImGui](https://github.com/DrNseven/D3D12-Hook-ImGui/blob/master/main.cpp) — 널리 복사되는 패턴이지만 **펜스 동기화가 없다.** 참고하되 그대로 따르지 말 것
- [Sh0ckFR/Universal-Dear-ImGui-Hook](https://github.com/Sh0ckFR/Universal-Dear-ImGui-Hook/blob/master/d3d12hook.cpp)
