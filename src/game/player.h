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
//   위험 타입(핀 금지): 17/18(발열·자연발화), 48(탈것 화염, 탈것 전용).
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
// 분석 스레드에서 부른다. **로컬 플레이어를 자립적으로 찾는다**: 정신력 풀을
// 가진 char 중 착용 조각이 가장 많은 것(동행 11 < 플레이어 18). 한 번 잡으면
// 고정(스티키)하고, 이후엔 Health 게이트만으로 유지한다 - 게이지 체인이 순간
// 실패해도 NPC/동행으로 새지 않는다. 재탐색(스티키 무효 시)만 힙 스캔.
void player_discover(const mem::Rtti& rtti, const mem::Reader& reader);
bool player_ready();

// 고정된 플레이어 char / 그 장비 컴포넌트(equip 이 같은 대상을 쓰도록). 없으면 0.
std::uintptr_t player_char();
std::uintptr_t player_comp();

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
