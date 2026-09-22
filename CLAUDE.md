# CDToybox

Crimson Desert Enhanced (Steam AppID 3321460) 용 **개인 모드**. 네이티브 C++20 /
D3D12 ImGui 오버레이. 싱글플레이 전용.

- 진입: `xinput1_4.dll` 프록시 — `dinput8.dll` 은 이 게임의 임포트 테이블에
  없고 `xinput1_4.dll` 이 있어서 그것을 프록시한다.
- 목표: 패스파인더 ToyBox 급의 인게임 도구 모음.

---

## 1. 브랜치 전략 (필수)

`main` 은 **보호 브랜치**다. GitHub 설정 여부와 무관하게 **규칙으로 강제**한다.
`develop` 이 통합 브랜치다.

**2단계 PR**

1. **작업 브랜치 분기** — `develop` 에서 `feat/*` · `fix/*` · `chore/*` ·
   `docs/*` 를 만든다.
2. **develop 으로 PR → 머지** — 작업 브랜치 → `develop` PR 을 올리고 검증이
   통과하면 머지한다.
3. **main 으로 PR → 머지(릴리스)** — 릴리스 시점에만 `develop` → `main` PR.
   `main` 에는 오직 이 경로로만 들어간다.

**규칙**

- `main` · `develop` 에 **직접 push 금지**. 모든 변경은 PR 을 거친다.
- 머지는 기본 merge commit. 스쿼시·리베이스는 명시 요청이 있을 때만.
- **머지한 PR 의 브랜치는 로컬·원격 모두 직접 지운다** — `git branch -d <브랜치>` ·
  `git push origin --delete <브랜치>`. 저장소의 "머지 후 자동 삭제" 는 켜지 않는다
  (2026-09-22 사용자 결정: 수동 삭제). 안 지워서 지난 브랜치 54개가 원격에 쌓여
  있었다(2026-09-21 정리). `archive/*` 는 지우지 않는다.
- 원격: `origin = github.com/game-mod-project/CDToybox`.

**⚠️ CI 워크플로가 없다.** `.github/` 자체가 없으므로 "CI 녹색 확인" 을
**로컬 검증이 대신한다.** 코드가 바뀌는 PR 은 머지 전에 반드시:

```powershell
.\scripts\build.ps1          # 성공해야 한다
.\build\cdtb_tests.exe       # 0 failures 여야 한다
```

문서만 바뀌는 PR 은 생략해도 된다.

---

## 2. 워크트리 — 여러 세션이 하나를 같이 쓴다

작업 디렉토리가 게임 폴더로 열려 있어도 소스와 git 은 전부 `E:\CDToybox` 에
있다. 워크트리는 **늘어나고 줄어드니 세지 말고 물어본다** — `git worktree list`
가 지금 무엇이 붙어 있는지 알려 준다(본 트리 `E:/CDToybox` = `develop`. 보조 트리
목록은 금방 낡으니 여기 적지 않는다).
**남의 트리는 건드리지 않는다.**

**트리를 나눠도 배포본은 하나다** — 게임의 `bin64\xinput1_4.dll` 은 세션 수와 무관하게
하나라서, 한 트리가 develop 을 안 받은 채 배포하면 남의 기능이 사라진다(2026-09-18 실제로
겪었다). 배포는 **develop 정본에서**, 배포 전에 한 줄:
`git merge-base --is-ancestor origin/develop HEAD` (0 이 아니면 배포하지 않는다).
`deploy.ps1` 은 **백업하지 않으므로** 직전 DLL 을 먼저 따로 복사해 둔다.

**git 의 HEAD · 인덱스 · 워킹트리는 저장소당 하나다.** 브랜치를 만들어도 "내
것" 이 되지 않는다. 다른 세션이 체크아웃하면 내 다음 커밋이 남의 브랜치로 가고,
내가 체크아웃하면 남이 보호 브랜치 위에서 커밋하게 된다. 실제로 두 번 사고가
났다.

- **커밋·머지 직전에 매번** `git -C E:/CDToybox branch --show-current` 로
  확인한다. 브랜치를 만든 시점과 커밋하는 시점 사이에 바뀌어 있을 수 있다.
  `develop` 이 아니면 그 트리에서 머지하지 않는다.
- 남의 미커밋 변경(`git status`)이 보이면 그 파일에 손대지 않는다.
  `git checkout --` · `reset` · `stash` 를 그 파일에 쓰지 않는다.
- 남의 브랜치를 되감지(`branch -f` · `reset`) 않는다.
- 시작·종료 때 `git reflog -8` 로 남의 체크아웃이 끼어들었는지 본다.

**다른 세션이 라이브 분석 중인지 보는 법:** `Get-Process CrimsonDesert` 와
`bin64/CDToybox.log` 의 갱신 시각. 게임이 돌고 있으면 그 세션의 배포본과
작업 공간을 건드리지 않는다.

격리가 필요하면 워크트리를 따로 판다:

```powershell
git -C E:/CDToybox worktree add E:/CDToybox-xxx -b <브랜치> develop
```

둘을 잊지 말 것 — (1) 그 트리에서 `git submodule update --init`
(imgui · minhook 이 비어 있어 빌드가 **즉시 실패**한다), (2) 그 트리의
`build/` 는 새로 만들어져 첫 빌드가 전체 빌드다. 제거할 때는 서브모듈 때문에
`git worktree remove` 가 거부하므로 `--force` 가 필요하다.

---

## 3. 빌드 · 시험 · 배포

```powershell
.\scripts\build.ps1          # VS2022 BuildTools + Ninja, RelWithDebInfo
.\build\cdtb_tests.exe       # 단위 시험 — 끝줄의 `0 failures` 를 볼 것 (786개, 2026-09-22)
.\scripts\deploy.ps1         # build\xinput1_4.dll -> 게임 bin64\
```

`deploy.ps1` 은 스스로 두 가지를 거부한다 — **게임이 실행 중이면** 거부하고,
**산출물이 `src/` 보다 오래됐으면**(= 빌드가 조용히 실패했을 수 있으면) 거부한다.

**CMake 타깃**

| 타깃 | 산출물 | 쓰임 |
|---|---|---|
| `cdtoybox` | `xinput1_4.dll` | 게임에 주입되는 프록시 DLL |
| `cdtb_core` | 정적 라이브러리 | 공용 코어 (DLL·시험·도구가 공유) |
| `cdtb_tests` | `cdtb_tests.exe` | 단위 시험 (`/Od` 로 빌드) |
| `cdtb_probe` | `cdtb_probe.exe` | **외부 프로세스 메모리 탐침** |
| `cdtb_bench` | `cdtb_bench.exe` | 스캐너 벤치 |

서브모듈: `external/imgui`, `external/minhook`. 새로 클론했으면
`git submodule update --init --recursive` 먼저.

**오버레이 화면 검증** — `scripts/overlay-check.ps1` 은 게임을 포그라운드로
**가져오지 않고** PrintWindow 로 캡처하고 PostMessage 로 클릭한다.
`SetForegroundWindow` · `keybd_event` · `SendInput` 은 쓰지 않는다(사용자의
창을 건드린다). 스크립트 머리말에 ImGui 백엔드 관련 함정(WM_MOUSELEAVE,
`AllowOverlap`)이 적혀 있으니 손대기 전에 읽을 것.

**게임 갱신 뒤** — `tools/rtti/recheck.py <exe>` 로 무엇이 낡았나를 보고,
`tools/rtti/rederive.py <exe>` 로 새 값을 낸다(절차 `specs/2026-09-01-patch-recheck.md`).

**정적 분석 도구** — `tools/rtti/*.py`. 인터프리터는 `py -3.14`
(= `%LOCALAPPDATA%\Python\pythoncore-3.14-64\python.exe`) 를 쓴다.
`WindowsApps\python.exe` 는 스토어 스텁이라 멈춘다.

---

## 4. 구조

```
src/
  core/     로그 · 설정 · 크래시로그 · 가드 · 파일버전
  game/     게임별 기능 (인벤토리 · 지급 · 동반자 · 명부 · 장비 · 낙사 · 카메라 …)
  input/    입력 격리 · 커서
  mem/      리더 · RTTI · 스캐너 · 훅(minhook) · 워치포인트 · 영역 조사
  proxy/    xinput1_4 프록시 진입 (exports.def)
  render/   D3D12 훅 + ImGui 오버레이 패널
tools/
  probe/    cdtb_probe — 외부에서 게임 메모리를 읽는 탐침
  rtti/     파이썬 정적 분석 (cheat_report · find_class · xref_data …)
tests/      단위 시험
scripts/    build · deploy · overlay-check
docs/       아래 참조
```

## 5. 문서 지도

| 문서 | 내용 |
|---|---|
| `docs/STATUS.md` | **먼저 읽을 것.** 지금 무엇이 되고 · 안 되고 · 왜 그런가 |
| `docs/TROUBLESHOOTING.md` | 겪은 문제와 해결. 증상 색인이 앞에 있다 |
| `docs/README.md` | **문서 지도.** 조사 기록·계획을 주제별로 묶고 편마다 지금 상태(해결·미해결·반증·기록)와 근거를 적었다 |
| `docs/superpowers/specs/` | 기능별 리버스 근거 · 설계 (61편). 한 주제가 여러 편이면 **문서 지도와 최신 편의 머리말이 어느 편이 정본인지 밝힌다** |
| `docs/superpowers/plans/` | 단계별 구현 계획 (7편 — 5편 구현 완료, 1단계 계획은 값 스캐너만 완료·카메라 보류, 아이템 설명·효과 계획은 진행 중. 머리말 배너를 볼 것) |
| `docs/special-function-items.md` | `specguard` 대상 32종 — 치트로 만들면 인벤 렌더에서 죽는 장비 |

문서 곳곳이 `.superpowers/sdd/<플랜>/progress.md` 를 실행 장부로 가리킨다.
**그것들은 레포에 없다** — 로컬 SDD 작업 부산물이라 추적하지 않고(`.gitignore`),
작업한 트리에만 남으며 대개 이미 지워졌다. 레포 안에서 확인 가능한 근거는
`docs/superpowers/specs/` 쪽이다.

## 6. 추적하지 않는 것

`.gitignore` 참조. 특히 **참고 모드 내려받기**(Nexus CT/ASI 의 `*.CT` · `*.zip`
와 `CHANGELOG_Table.txt` · `CrimsonInvEditor.itemmap.txt`)는 **저장소에 두고
보되 추적하지 않는다** — `git status` 를 매번 어지럽혔다. 지우지 말 것.
