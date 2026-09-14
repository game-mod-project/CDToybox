#pragma once

#include <cstdint>

#include "mem/reader.h"
#include "mem/rtti.h"

// 낙사 방지(No Fall Damage) - **플레이어 전용**.
//
// 2026-09-12 재설계. 빌드 2.01.00 이 옛 사이트(낙하 누적값 쓰기)를 지웠고 돌아오지
// 않는다 - 2850 이미지에서 옛 AOB `48 89 5F ?? 48 8B 5C 24 ?? 48 89 77 ?? 66 89 6F`
// 은 0곳이고, 결합부 `48 89 77 ?? 66 89 6F` 마저 0곳이다. 그래서 누적기가 아니라
// **데미지/스테이터스 디스패처의 진입점**을 훅한다(참고 모드도 같은 결론을 냈다).
//
// 디스패처 호출 규약(2850 실측, RVA 0x01719850):
//   rcx = 대상 소유자   dx = statusId(0 = Health)   r8 = 시각
//   r9  = 델타(데미지는 음수)      [rsp+0x28] = sourceCtx(인자 5)
//
// **여기까지는 라이브에서 확정됐다**(2026-09-12): 피해 이벤트 120건 중 내 것 6건,
// 그중 생명 피해는 `rcx` = 내 root, `dx` = 0, 델타 -337,500 이었고 같은 시각 생명이
// 정확히 334,500 줄었다. 관찰 학습이 없으므로 **첫 낙하부터** 판정한다.
//
// **아직 확정 안 된 것은 "낙하인가" 를 가르는 판별식이다.** CT v5.0 의
// `sourceCtx+0x68`(가해자 슬롯) 방식은 이 빌드에서 낙하를 못 걸러 냈다 - 낙하
// 피해도 가해자가 있는 것으로 분류돼 그대로 통과했다(취소함 0 / 통과시킴 4).
// 그래서 케이브가 **출처의 정체를 기록**하면서 **판별 규칙을 런타임에 바꿀 수 있게**
// 해 두었다(nofall_cave.h 의 NofallRule). 게임을 껐다 켜지 않고 후보를 갈아 본다.
//
// 내 root = [[[char+0x68]+0x20]+0x18] - player.h 의 게이지 체인과 같은 자리다.
// 같은 캐릭터가 클라·서버 두 realm 으로 존재하고 디스패처는 서버 root 를 넘기므로
// **둘 다** 먹인다(player_roots). 캐릭터 교체·지역 이동으로 바뀌므로 캐시하지 않는다.
//
// 출처: CT v5.0 "No Fall Damage"(mul0095/Trinity, MIT) 이식 - 사이트와 규약은
//       그대로, 낙하 판별만 이 빌드에 맞춰 다시 잡는 중이다.
//
// **모드(주입 DLL)에서만** 부른다. 라이브 코드 패치.

namespace cdtb::game {

// 훅을 설치한다(사이트 AOB 가 유일할 때만). 이미 설치돼 있으면 참. 실패면 거짓.
bool nofall_install(const mem::Rtti& rtti, const mem::Reader& reader);
bool nofall_installed();

// AOB 가 이 게임 빌드에서 유일 매칭되지 않아 설치 불가로 판정됨(안전, 무효).
bool nofall_unsupported();

// 내 root 두 개를 다시 계산해 케이브에 건네준다. **렌더 틱(~16ms)에서** 부른다 -
// 분석 통과는 수십 초라 캐릭터 교체 뒤 낡은 root 로 남는 창이 너무 길다.
// 꺼져 있으면 0 을 써서 케이브가 곧바로 빠져나가게 한다.
void nofall_refresh(const mem::Reader& reader);

// 켜기/끄기. 끄면 root 를 지워 케이브가 곧바로 빠져나간다(훅은 남되 무효).
void nofall_set(bool on);
bool nofall_enabled();

// 판별 규칙(nofall_cave.h 의 NofallRule). 다음 피해부터 바로 먹는다.
std::uint64_t nofall_rule();
void nofall_set_rule(std::uint64_t rule);

// 케이브가 센 것과 마지막으로 본 값들. 미설치면 전부 0.
struct NofallDiag {
    std::uint64_t owner = 0;        // 내 root #1
    std::uint64_t owner2 = 0;       // 내 root #2 (다른 realm)
    std::uint64_t zeroed = 0;       // 취소함
    std::uint64_t let_through = 0;  // 통과시킴
    std::uint64_t last_src = 0;     // 마지막 sourceCtx
    std::uint64_t last_vt = 0;      // 그 객체의 vtable(클래스를 푸는 열쇠)
    std::uint64_t last_atk = 0;     // [sourceCtx+0x68]
    std::uint64_t last_delta = 0;   // 그때의 델타(음수)
    std::uint64_t src_low = 0;      // 출처가 아예 없던 횟수
    std::uint64_t src_bad = 0;      // 말 안 되는 포인터였던 횟수
};
NofallDiag nofall_diag();

// 가해자 역참조가 매핑 안 된 주소를 물어 폴트 가드가 끼어든 횟수. 0 이어야
// 정상이고, 오르면 그 판정이 이 빌드에서 불안정하다는 신호다(피해는 통과시킨다).
std::uint64_t nofall_faults();

}  // namespace cdtb::game
