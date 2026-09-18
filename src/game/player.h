#pragma once

#include <cstdint>

#include "mem/reader.h"
#include "mem/rtti.h"

// 플레이어 스탯 게이지 배열 기반 치트(B-1: Godmode·무한 자원) + 플레이어 식별.
//
// 체인 (CT CE-검증, 우리 라이브 재검증 2026-09-05):
//   char(equip comp+0x08) +0x68 actor +0x20 marker +0x18 root +0x58 = 게이지 배열
//   entry k = arr + k*0x90 :  +0x00 i32 타입  +0x08 i64 현재  +0x18 i64 최대(base)
//   게이트: entry[0].type==0 (Health). 평면 오프셋(CDR godmode 방식):
//     HP  cur +0x08 / max +0x18      (게이지 0)
//     STA cur +0x6C8 / max +0x6D8    (게이지 12, type 22)
//     SPI cur +0x758 / max +0x768    (게이지 13, type 21/23)
//   안 건드리는 타입: 17/18(발열·자연발화로 **추정**), 48(탈것 화염으로 추정).
//     ⚠️ 이 이름도 "위험" 판정도 **측정된 적이 없다**(2026-09-18 추적). 이 줄을
//     넣은 커밋(6d26f6c)에 근거가 없고, 참고한 CT 문서에도 이 셋은 안 나온다.
//     아래 freeze 는 고정 오프셋 셋만 쓰므로 애초에 닿지도 않는다 - 금지를
//     강제하는 코드는 없다. 근거로 인용하기 전에 실제로 재 볼 것.
//
// 플레이어 식별: 게이지 배열이 Health 게이트를 통과하고 **정신력 풀**(type
// 21/23, max>0)을 가지면 플레이어다(적/NPC 는 정신력 풀이 없다 - CT 통찰).
//
// 출처: XeTrinityz(=ReXooGen)/Trinity (MIT), Nexus 3209 CT 실측.

namespace cdtb::game {

// char 에서 게이지 배열을 유도한다(체인 + Health 게이트). 실패면 0.
std::uintptr_t player_gauge_array(const mem::Reader& reader, std::uintptr_t ch);

// 이 char 가 플레이어인가(게이지 배열 + 정신력 풀 보유). equip 테이블 선택
// 안정화에 쓴다 - 주변 NPC 장비 테이블과 확실히 구분된다.
bool char_is_player(const mem::Reader& reader, std::uintptr_t ch);

// --------------------------------------------------------------- 발견/캐시
// 분석 스레드에서 부른다. **힙 스캔 없음**(값싸다) - equip 이 이미 고른 플레이어
// comp(equip_player_comp)에서 char→게이지 배열을 잡아 캐시한다. 한 번 잡으면
// 고정(스티키): Health 게이트가 유지되는 한 그대로 쓴다(체인 순간 실패에도
// 안 흔들림). equip 이 플레이어(정신력 풀+착용 최다)를 고르므로 대상은 정확.
void player_discover(const mem::Reader& reader);
bool player_ready();

// 고정된 플레이어 char. 없으면 0.
std::uintptr_t player_char();

// 지금 고른 캐릭터의 **모든 realm root**(클라+서버)를 모은다. 게이지 배열이 아니라
// 그 한 단계 앞인 root 다 - 데미지 디스패처가 rcx 로 넘기는 것이 이 root 이기
// 때문이다(2026-09-12 실측). 낙사 방지가 쓴다. 넣은 개수를 돌려준다.
int player_roots(const mem::Reader& reader, std::uintptr_t* out, int max);

// 지금 고른 캐릭터의 **모든 realm char**(클라+서버). root 가 아니라 그 앞의
// 캐릭터 객체다 - 낙하 피해의 sourceCtx 가 바로 이것이다(2026-09-12 실측).
int player_chars(const mem::Reader& reader, std::uintptr_t* out, int max);

struct PlayerVitals {
    std::int32_t hp_cur = 0, hp_max = 0;
    std::int32_t sta_cur = 0, sta_max = 0;
    std::int32_t spi_cur = 0, spi_max = 0;
    bool ok = false;
};
PlayerVitals player_vitals(const mem::Reader& reader);

// --------------------------------------------------------------- 토글/적용
void player_set_godmode(bool on);
void player_set_inf_stamina(bool on);
void player_set_inf_spirit(bool on);
bool player_godmode();
bool player_inf_stamina();
bool player_inf_spirit();

// 매 틱 freeze 적용(분석 스레드). **모드(주입 DLL)에서만.** SEH 로 쓴다.
// 게이트가 깨지면(캐릭터 전환·지역 이동) 조용히 넘어가고 다음 발견을 기다린다.
void player_apply(const mem::Reader& reader);

}  // namespace cdtb::game
