#pragma once

#include <cstdint>

// **휠 칸 등록 채우기** (2026-09-16 실측).
//
// 동반자마다 "어느 휠 칸에 올려놨는가" 를 적는 u16 이 있다(등록 항목 +0x148).
// 0xFFFF 면 아무 칸에도 안 올라가 있다는 뜻이고, 그러면 소환 판정
// `0x20170E0` 의 마지막 관문이 빈손이 되어 거부 코드 4205559856 이 난다.
// 드래곤이 딱 그 상태였다 - **잠긴 게 아니라 칸에 안 올라가 있었다.**
//
// 채울 때는 종행을 못 박아야 한다. 빈 칸은 말에도 여럿 있고(가진 말을 다
// 올려놓진 않는다) 거기까지 채우면 유령 동반자가 생긴다.
//
// 여기는 상태와 판정만 둔다. 실제 쓰기는 조회 훅(`dragondiag.cpp`)이 한다 -
// 표의 주인은 호출 인자로만 오기 때문이다.

namespace cdtb {
namespace game {

// 항목을 채워야 하는가. `cur` 은 항목이 지금 적고 있는 칸(0xFFFF = 없음),
// `species` 는 그 항목의 종행.
bool wheel_fill_wanted(std::uint16_t cur, std::uint16_t species,
                       const std::uint16_t* rows, int n);

// ---------------------------------------- 같은 종에 레코드가 여럿일 수 있다
//
// **2026-09-16 에 실제로 겪었다.** 명부에 `Riding_Dragon_1`(종행 7008)이 **둘**
// 있었고, 휠에 올라가 있던 쪽이 **빈 껍데기**였다:
//
//   1000483  생명 1     성장치 1/1    휠칸 0x0000  <- 소환 경로가 집던 쪽
//   1000724  생명 2500  성장치 30/30  휠칸 0xFFFF
//
// 그래서 소환은 되는데 스탯이 0 이라 완결되지 않았다. 등록을 1000724 로 옮기자
// 스탯이 서고 소환됐다. 앞선 문서가 이 둘을 "realm 마다 한 벌" 로 읽은 것은
// 틀렸다 - `clan client` 목록에 둘 다 나오는 **서로 다른 동반자**다.
//
// 그러니 채울 때 **종만 보고 전부 올리면 안 된다.** 종이 같은 것들 중
// **제일 멀쩡한 하나**를 고른다.
inline constexpr std::uintptr_t kEntryHp = 0xA0;     // i32 생명
inline constexpr std::uintptr_t kEntryGrow = 0x158;  // i32 성장치

// 생명 칸의 **"게임이 정한다" 센티널.** 와이번·A.T.A.G. 가 이 값인데 스탯이
// 정상으로 뜬다(실측) - 그러니 껍데기(생명 1)보다 **나은** 쪽으로 친다.
inline constexpr std::int32_t kHpDefault = -1;
inline constexpr std::int32_t kHpRankMax = 0x7FFFFFFF;

// A 가 B 보다 나은 후보인가. 생명이 크면 낫고(-1 은 가장 큼), 같으면 성장치로
// 가른다. 순수 - 시험한다. 둘 다 같으면 거짓(먼저 본 것을 유지 - 순서가 안정된다).
bool wheel_fill_better(std::int32_t hp_a, std::int32_t grow_a,
                       std::int32_t hp_b, std::int32_t grow_b);

// 기능을 켤지(설정에서 온다).
void wheel_fill_set_enabled(bool on);
bool wheel_fill_enabled();

// 대상 종행(얹기가 고른 것들). n <= 0 이면 아무것도 안 채운다.
void wheel_fill_set_rows(const std::uint16_t* rows, int n);

// 대상 목록을 빌려 본다. 개수를 돌려주고 `*out` 에 배열을 준다.
int wheel_fill_rows(const std::uint16_t** out);

}  // namespace game
}  // namespace cdtb
