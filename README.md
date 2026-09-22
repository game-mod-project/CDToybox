# CDToybox

Crimson Desert Enhanced (Steam AppID 3321460, exe 1.0.0.2944) 용 **개인 모드**.
네이티브 C++20 / D3D12 ImGui 오버레이. 싱글플레이 전용.

- **진입:** `xinput1_4.dll` 프록시 — `dinput8.dll` 은 이 게임의 임포트 테이블에
  없고 `xinput1_4.dll` 이 있어서 그것을 프록시한다.
- **켜기** `Insert` · **비활성화** `F10` (게임 `bin64/CDToybox.ini` 에서 바꾼다).
- **목표:** 패스파인더 ToyBox 급의 인게임 도구 모음.

## 빌드

```powershell
git submodule update --init --recursive   # imgui · minhook — 없으면 빌드가 즉시 실패한다
.\scripts\build.ps1                       # VS2022 BuildTools + Ninja, RelWithDebInfo
.\build\cdtb_tests.exe                    # 단위 시험 (0 failures 여야 한다)
.\scripts\deploy.ps1                      # build\xinput1_4.dll -> 게임 bin64\
```

`deploy.ps1` 은 **게임이 실행 중이면** 거부하고, **산출물이 `src/` 보다 오래
됐으면**(= 빌드가 조용히 실패했을 수 있으면) 거부한다.

처음 한 번은 **Defender 제외**를 넣는다. 서명 없는 DLL 이 API 를 후킹하고 힙을
전수 탐색하므로 머신러닝 휴리스틱에 오탐으로 걸려 **배포한 DLL 이 조용히
격리된다**(겪었다 — [TROUBLESHOOTING 6.25](docs/TROUBLESHOOTING.md#625-defender-가-배포한-dll-을-먹었다)).

```powershell
.\scripts\setup-defender-exclusions.ps1   # UAC 로 승격, 워크트리는 자동 수집
.\scripts\setup-defender-exclusions.ps1 -DryRun   # 무엇이 등록될지만 본다
```

워크트리를 새로 만들면 다시 돌린다. 그 `build` 폴더가 더해진다.

## 문서

| 문서 | 내용 |
|---|---|
| [docs/STATUS.md](docs/STATUS.md) | **먼저 읽을 것.** 지금 무엇이 되고 · 안 되고 · 왜 그런가 |
| [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | 겪은 문제와 원인·해결. 증상으로 찾는 색인이 앞에 있다 |
| [docs/README.md](docs/README.md) | **문서 지도** — 조사 기록·계획을 주제별로 묶고 편마다 지금 상태를 적었다 |
| [CLAUDE.md](CLAUDE.md) | 작업 규율 — 브랜치 전략 · 공유 워크트리 · 빌드 · 구조 |
| [docs/superpowers/specs/](docs/superpowers/specs/) | 기능별 리버스 근거 · 설계 |
| [docs/superpowers/plans/](docs/superpowers/plans/) | 단계별 구현 계획 (7편 — 5편 완료, 1단계는 값 스캐너만 완료·카메라 보류, 아이템 설명·효과 계획은 진행 중) |

개인용. 싱글플레이 전용.
