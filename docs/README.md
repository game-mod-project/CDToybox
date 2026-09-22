# 문서 지도

**갱신:** 2026-09-22 · exe **1.0.0.2949**

조사 기록 60편 · 계획 6편이 어디에 무엇이 있고 **지금도 유효한가**를 한 곳에 모았다.
상태는 문서 자신의 문장이나 **이후 문서의 명시적 문장**으로만 정했다. 조사 기록 55편은
제목 아래(제목 위에 옛 배너가 있는 편은 맨 위)의 `정리 (2026-09-21)` 배너가 같은 상태와
근거를 적는다. 나머지 11편 — 계획 6편 · `socket-wire-format` · `story-mount-clone-design` ·
`dragon-wheel-handoff` · `boss-room-action-limit` · `game-update-2949` — 은 그 배너가 없고,
문서 첫머리의 상태 배너가 그 역할을 한다. 본문은 역사 기록이라 고치지 않았다 — **배너가
본문보다 우선한다.**

## 먼저 읽을 것

| 문서 | 무엇 |
|---|---|
| [STATUS.md](STATUS.md) | **지금 무엇이 되고 · 안 되고 · 왜 그런가.** 맨 앞 "한눈에" 표가 요약이다 |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | 겪은 문제와 원인·해결. 증상으로 찾는 색인이 앞에 있다. §8 미해결 · §9 닫힌 길 |
| [../CLAUDE.md](../CLAUDE.md) | 작업 규율 — 브랜치 · 공유 워크트리 · 빌드·시험·배포 |
| [special-function-items.md](special-function-items.md) | `specguard` 대상 32종 — 치트로 만들면 인벤 렌더에서 죽던 장비 |

## 상태 표기

| 표기 | 뜻 |
|---|---|
| ✅ 해결 · 완료 | 문서나 이후 문서가 해결·구현·게임 확인을 적었다 |
| ⏳ 미해결 · 보류 | 명시적으로 미해결·보류이고 이후 해결 기록이 없다 |
| ❌ 반증 · 취소 | 주된 결론이 이후에 뒤집혔다 — **그 결론을 인용하지 말 것** |
| 🗑 폐기 | 기능을 걷어냈다 |
| 📜 기록 | 세션 인계·정리처럼 상태가 하나가 아닌 역사 기록 |
| 📐 설계 | 설계 문서. 구현 여부를 함께 적는다 |

범죄수치·수배는 **다른 세션 담당**이라 이 지도에서 다루지 않는다(현황은 STATUS §1.24).

**조사 기록이 따로 없는 기능** — STATUS 에만 있다: §1.11 크래시·멈춤 진단 · §1.13 플레이어
치트·낙사 방지(설계는 아래 12 의 `player-teleport-port`) · §1.16 가방·보관함 용량 ·
§1.17 스킬 포인트(어비스 결속) · §1.18 강화 조건 관문 · §1.19 지식 표 · §1.20 원소 휠 ·
§1.22 UI 상태 기억 · §1.23 장비의 캐릭터 구분.

---

## 1. 기반 — 오버레이 · 스캐너 · 후킹 · 도구

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [d3d12-overlay-research](superpowers/specs/2026-08-31-d3d12-overlay-research.md) | ✅ 해결 | 큐 확정(스왑체인 생성 훅) · 펜스 동기화가 들어갔다. STATUS §1.1, TS §1.6 |
| [stage0-scaffold-design](superpowers/specs/2026-08-31-stage0-scaffold-design.md) | 📐 구현됨 | 골대는 들어왔다. 큐 획득 방식 · 단축키(`Insert`/`F10`) · 섹션 가정은 바뀌었다. STATUS §1.1 · §1.2 · §2.1 |
| [plans/stage0-scaffold](superpowers/plans/2026-08-31-stage0-scaffold.md) | ✅ 완료 | 계획의 산출물 전부 |
| [static-analysis](superpowers/specs/2026-09-03-static-analysis.md) | ✅ 해결 · §8 ❌ | 지급 경로 해독·소켓·내구도 싣기. "수리 불가 = 아이템 종류의 성질" 은 반증(인스턴스 단위). STATUS §6 |
| [frame-boundary](superpowers/specs/2026-09-04-frame-boundary.md) | ⏳ 보류 | 후보 둘은 막다른 길. 깨끗한 경계 찾기는 급하지 않은 별도 과제 |
| [startup-loading](superpowers/specs/2026-09-08-startup-loading.md) | ✅ 해결 | 3분 30초 → 29초. 남은 병목은 2026-09-12 분석 통과에서 묶었다(231 → 35초). 주기 스캔은 아직 각자. STATUS §6 "다음에 이어갈 지점" B |
| [dead-code-cleanup](superpowers/specs/2026-09-10-dead-code-cleanup.md) | ✅ 완료 | A · C · D 삭제, 2026-09-12 릴리스 |

## 2. 카메라 · 프리카메라 — ⏳ 보류

카메라는 렌더가 읽는 값을 아직 못 찾았다. 프리카메라는 코드만 있고 훅을 걸지 않는다(STATUS §2.4 · §3).

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [camera-offsets](superpowers/specs/2026-08-31-camera-offsets.md) | ❌ 반증 | 위치·회전 오프셋이 틀렸고, 정정이 가리킨 월드 좌표 `+0x360` 도 패치 뒤 `+0x378`. TS §4.19 |
| [camera-write-paths](superpowers/specs/2026-08-31-camera-write-paths.md) | ❌ 반증 | "좌표를 쓰면 카메라가 따라 움직인다" 는 틀렸다. TS §7.3 · §4.2 |
| [stage1-freecam-design](superpowers/specs/2026-08-31-stage1-freecam-design.md) | 📐 일부 | M1·M2 는 RTTI 로 했다. 값 스캐너는 들어왔고 프리카메라(M4)는 보류 |
| [plans/stage1-m1-m2-value-scanner](superpowers/plans/2026-08-31-stage1-m1-m2-value-scanner.md) | ✅ 일부 완료 | 값 스캐너 완료 · 카메라 보류 |

## 3. 개발자 치트 · 디버그 흔적

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [cheat-messages](superpowers/specs/2026-09-01-cheat-messages.md) | ✅ 해결 (값은 옛 빌드) | 인벤토리 직행 지급이 지금 지급의 바탕. ID·RVA 는 2944 에서 재번호(TS §5.1.1). 쓸 수 있는 치트는 10개뿐(STATUS §6). 바닥 스폰은 걷어냈다 |
| [cheat-system](superpowers/specs/2026-09-01-cheat-system.md) | 📜 기록 | 콘솔 전역·`+0x50` 해석은 틀렸다. 디버그 콘솔은 닫힌 길(TS §9) |
| [debug-console](superpowers/specs/2026-09-03-debug-console.md) | ⏳ 보류 · 닫힌 길 | 콘솔 UI 인스턴스가 0개. STATUS §2.2.1, TS §9 |
| [field-names](superpowers/specs/2026-09-03-field-names.md) | ✅ 완료 | `tools/rtti/fields.py`. STATUS §2.3 |

## 4. 아이템 · 인벤토리 · 지급

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [item-table](superpowers/specs/2026-09-01-item-table.md) | ✅ 해결 | STATUS §1.7 · §1.8. 개수 6,810 은 그때 값(2026-09-04 뒤 6,813) |
| [localization-lookup](superpowers/specs/2026-09-01-localization-lookup.md) | ✅ 해결 | STATUS §1.6. 주소 표는 옛 빌드 값 |
| [inventory](superpowers/specs/2026-09-02-inventory.md) | ✅ 해결 · 후반 ❌ | 레코드 구조·식별자 유효(STATUS §6.1). 후반 소켓·내구도 결론 다수 반증 — 소켓은 `socket-grant-unlock-research` 가 우선 |
| [inventory-export-import-design](superpowers/specs/2026-09-06-inventory-export-import-design.md) | 🗑 폐기 | export/import 는 2026-09-06 제거, 다시 만들지 않았다. 전제였던 소켓 벽 둘은 없어졌다 |
| [special-item-crash-and-session-handoff](superpowers/specs/2026-09-07-special-item-crash-and-session-handoff.md) | ✅ 해결 | 크래시는 `specguard`. 인계한 세션 먹통은 2026-09-10 세션 표 포화로 풀렸다 |
| [session-table-saturation](superpowers/specs/2026-09-10-session-table-saturation.md) | ✅ 해결 | 축출(F2) · 쓰레기 칸 막기(F1). TS §3.4 |
| [special-function-items.md](special-function-items.md) | ✅ 참조 자료 | `specguard` 대상 32종 목록 |

## 5. 소켓 · 장비 편집

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [socket-grant-unlock-research](superpowers/specs/2026-09-07-socket-grant-unlock-research.md) | ✅ 해결 — **소켓의 정본** | 지급 소켓 · 제자리 언락 · 부위별 상한. 남은 것: 지급 처리기 교착(TS §2.7) · 장기 영향 미측정(STATUS §6 C) |
| [socket-wire-format](superpowers/specs/2026-09-04-socket-wire-format.md) | 🗑 폐기 | 메시지 구동 경로만 죽었다(TS §9). 소켓 기능 자체는 레코드 직접 쓰기로 된다 |
| [equip-editor-port](superpowers/specs/2026-09-05-equip-editor-port.md) | 📐 구현됨 · 두 곳 틀림 | STATUS §1.12. `+0x0A` 는 연마가 아니라 담금질(연마는 `+0x58`), MGRCHAIN 경로는 지웠다 |

## 6. 동반자(탈것·반려동물) 얻기 · 종 교체

지금 되는 것: 근처 획득(2338) + **종 교체**로 원하는 종을 얻는다(STATUS §1.10). 정본은 `companion-species-swap-session`.

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [companion-species-swap-session](superpowers/specs/2026-09-10-companion-species-swap-session.md) | ✅ 해결 — **정본** | 전 과정 화면 확인. §12-1 은 2026-09-12 해결(STATUS §1.10.1). 2026-09-21 버튼 잠금 결함 고침(TS §7.17) |
| [companion-catalog-and-acquire-research](superpowers/specs/2026-09-08-companion-catalog-and-acquire-research.md) | ✅ 해결 | 시간순 조사 기록 — 중간 단정 일부는 뒤에서 정정됐다(문서 머리말) |
| [catchable-companions](superpowers/specs/2026-09-05-catchable-companions.md) | ✅ 참조 자료 | 포획 대상 라이브 추출 표. 반박된 줄 없음 |
| [vehicle-pet-review](superpowers/specs/2026-09-04-vehicle-pet-review.md) | 📐 일부 | 뷰어는 들어왔다(STATUS §1.9). 소환 치트를 "안전" 이라 한 것은 틀렸다 — 금지(TS §1.9) |
| [companion-summon-acquire-design](superpowers/specs/2026-09-05-companion-summon-acquire-design.md) | 📐 일부 | 2338 획득·근처 탭 구현. 거부 코드 해석 둘은 틀렸다(TS §4.8). 물고기 방생 소환은 닫힌 길 |
| [vehicle-pet-add-review](superpowers/specs/2026-09-05-vehicle-pet-add-review.md) | ✅ A안 완료 · 부수 결론 ❌ | 부적 6종 등록은 됐다. "경로는 2454 하나뿐" 은 뒤집혔다 |
| [virtual-amulet-review](superpowers/specs/2026-09-05-virtual-amulet-review.md) | ❌ 반증 | "가상 부적" 전제가 같은 날 뒤집혔다. 목적은 2338 로 풀렸다 |

**확인 전 불일치 하나** — 명부 레코드 `+0x148` 을 위 두 편은 "소유자 캐릭터 행", STATUS §1.21 은 "올려 둔 휠 칸" 이라 적었다(TS §8).

## 7. 스토리 탈것(드래곤 · A.T.A.G.) — ✅ 둘 다 해결

정본은 **`story-vehicle-wheel`** 과 드래곤 해법 `dragon-external-research`(STATUS §1.21). 그 전의 편들은 결론이 뒤집혔다.

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [story-vehicle-wheel](superpowers/specs/2026-09-16-story-vehicle-wheel.md) | ✅ 해결 — **정본** | §0-A 가 결론. 2944 에서는 `wheel_fill` 휠 칸 채우기가 안 걸린다(`dragondiag` 건너뜀, TS §8) |
| [dragon-external-research](superpowers/specs/2026-09-16-dragon-external-research.md) | ✅ 해결 | 호출 지식 한 칸 + 휠에 올라간 빈 껍데기 레코드. "레벨 표로는 안 된다" 는 취소 |
| [atag-parts](superpowers/specs/2026-09-15-atag-parts.md) | ✅ 해결 | 파트 16종이 연구 없이 지급·장착. 재소환 쿨다운 50분/60분 두 값은 확인 전 |
| [story-mount-clone-design](superpowers/specs/2026-09-11-story-mount-clone-design.md) | 📐 미구현 | 필요했던 것은 복제가 아니라 카테고리 한 칸이었다 |
| [summon-wheel-research](superpowers/specs/2026-09-12-summon-wheel-research.md) | ❌ 반증 | "서버 주도라 못 한다" 는 A.T.A.G. · 드래곤 모두 틀렸다. 측정값은 유효 |
| [dragon-wheel-handoff](superpowers/specs/2026-09-14-dragon-wheel-handoff.md) | ❌ 반증 | "게이트 `0x2ACA250` = 소환 관문" 전제가 틀렸다 |
| [dragon-call-position](superpowers/specs/2026-09-15-dragon-call-position.md) | ❌ 반증 | §8 · §9 결론 취소 — 다른 칸을 만졌다(TS §4.29) |
| [dragon-summon-final](superpowers/specs/2026-09-15-dragon-summon-final.md) | ❌ 반증 | 옛 배너의 "드래곤에 대해서는 맞았다" 도 틀렸다 |
| [vehicle-wheel-data](superpowers/specs/2026-09-15-vehicle-wheel-data.md) | ❌ 처방 반증 | "메인 휠 허용 목록에 넣기" 는 아이콘만 올린다. 1바이트 카테고리 읽기는 맞았다 |

## 8. 탈것 호출 제한 · 탈것 수치

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [vehicle-place-and-dismount](superpowers/specs/2026-09-16-vehicle-place-and-dismount.md) | ✅ 마을 · 호출 위치 해결 | STATUS §1.21.1 · §1.21.2. 남은 것: 지도의 붉은 구역 · §8 보스룸 |
| [mount-vitals-authority](superpowers/specs/2026-09-19-mount-vitals-authority.md) | 📜 기록 | 탈것 체력·스태미나 게임 확인(STATUS §1.21.3). 한 세션 13건의 상세 |
| [boss-room-action-limit](superpowers/specs/2026-09-21-boss-room-action-limit.md) | ❌ 가설 반증 · ⏳ 보스룸 미해결 | 행동 제한 허용 목록 가설은 게임에서 반증, 토글은 #93 에서 걷어냈다. 읽기(`actionlimit`)만 남았다. STATUS §1.21.4, TS §7.15 · §7.18 |

## 9. 게임 구조

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [client-server-architecture](superpowers/specs/2026-09-16-client-server-architecture.md) | ✅ 완료 | 한 프로세스 안의 클라/서버 계층. §2-2 의 "realm 마다 한 벌" 은 틀렸다(서로 다른 동반자 둘) |

## 10. 게임 갱신 대응

현행 빌드는 **1.0.0.2949**. 갱신 때마다 `patch-recheck` 절차와 TS §5 를 본다.

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [patch-recheck](superpowers/specs/2026-09-01-patch-recheck.md) | ✅ 지금도 쓰는 절차 | 갱신 뒤 확인 목록은 TS §5.5 도 |
| [game-update-2949](superpowers/specs/2026-09-22-game-update-2949.md) | ✅ 완료 (게임 확인 2026-09-22) | 고정 RVA 전부 재도출, §11.1 전 항목 게임 확인. 재도출 경로는 `tools/rtti/rederive.py` 로 옮겼다 |
| [game-update-2944](superpowers/specs/2026-09-18-game-update-2944.md) | 📜 기록 | specguard 5/8 · spawnguard · 구동 자리는 이후 해결. 남은 것: 세션 전역 A/B · `dragondiag`(TS §8). 2944 자리들은 2949 에서 다시 짚었다 |
| [specguard-div-family](superpowers/specs/2026-09-20-specguard-div-family.md) | ✅ 해결 | 5/8 게임 확인, 실제로 4회 막은 것까지 확인(STATUS §1.15). 가족 표식은 2949 재도출에도 그대로 쓰였다 |
| [spawnguard-callsite](superpowers/specs/2026-09-20-spawnguard-callsite.md) | ✅ 해결 | `0x2B8DD36`(2944), 게임 확인. 2949 는 `0x2B8DD46`(같은 지문). 실제로 막은 횟수는 기록 없음 |
| [game-update-2850](superpowers/specs/2026-09-11-game-update-2850.md) | ✅ 완료 | 2850 대응. 세션 전역 A/B 는 지금도 미확정 |
| [game-update-break](superpowers/specs/2026-09-04-game-update-break.md) | ✅ 해결 · 소켓 판정 ❌ | 대응표·지급 복구. "소켓 전달을 없앴다" 는 틀렸다 |

## 11. 오버레이 UI/UX

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [overlay-uiux-design](superpowers/specs/2026-09-10-overlay-uiux-design.md) | 📐 구현됨 | 세 단계 전부, 화면 검증 끝. STATUS §1.14 |
| [plans/uiux-p1](superpowers/plans/2026-09-10-uiux-p1.md) · [p2](superpowers/plans/2026-09-11-uiux-p2.md) · [p3](superpowers/plans/2026-09-11-uiux-p3.md) | ✅ 완료 | 각 단계 계획 |
| [panel-shared-widgets-design](superpowers/specs/2026-09-10-panel-shared-widgets-design.md) | 📐 구현됨 | 필터바 · 필터 술어 · 보석 선택기 |
| [plans/panel-shared-widgets](superpowers/plans/2026-09-10-panel-shared-widgets.md) | ✅ 완료 | |
| [panel-shared-widgets-review](superpowers/specs/2026-09-10-panel-shared-widgets-review.md) | ✅ 완료 | Critical/Important 없음 |

## 12. 참고 모드(CT/ASI) 이식

| 문서 | 상태 | 지금 참인 것 · 근거 |
|---|---|---|
| [ct-asi-feature-review](superpowers/specs/2026-09-05-ct-asi-feature-review.md) | 📐 일부 | B-1 · B-7 · B-9 들어옴. B-5 텔레포트 등 나머지는 구현 기록 없음 |
| [player-teleport-port](superpowers/specs/2026-09-05-player-teleport-port.md) | 📐 일부 | B-1 플레이어 치트 구현(STATUS §1.13). **B-5 텔레포트는 구현 기록 없음** |

## 13. 세션 인계 · 정리 기록 — 📜

그날의 기록이다. 결론은 위 주제별 정본을 본다.

| 문서 | 지금 참인 것 · 근거 |
|---|---|
| [2026-09-04 session-handoff](superpowers/specs/2026-09-04-session-handoff.md) | 소켓 Phase 2 는 제거됐다. "소켓 지급 차단" · "처리기는 아이템 라우터" 는 틀렸다 |
| [2026-09-05 session-companion-handoff](superpowers/specs/2026-09-05-session-companion-handoff.md) | §7~§12 오진. §13.4 표시명 폴백도 고쳤다 |
| [2026-09-06 session-close](superpowers/specs/2026-09-06-session-close.md) | 근처 획득 유효. §4 "관문이 막는다" 는 폐기 |
| [2026-09-07 companion-add-session](superpowers/specs/2026-09-07-companion-add-session.md) | §2 소환 치트 방향은 금지 확인 뒤 걷어냈다 |
| [2026-09-08 companion-register-session](superpowers/specs/2026-09-08-companion-register-session.md) | 소환 치트 금지 유효. §4 C안은 닫힌 길 |
| [2026-09-12 session-close](superpowers/specs/2026-09-12-session-close.md) | 릴리스 시점 정리. 명부 폴백(O2) · 세션 전역 A/B 는 아직 열려 있다 |
