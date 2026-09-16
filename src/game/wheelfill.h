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

// 기능을 켤지(설정에서 온다).
void wheel_fill_set_enabled(bool on);
bool wheel_fill_enabled();

// 대상 종행(얹기가 고른 것들). n <= 0 이면 아무것도 안 채운다.
void wheel_fill_set_rows(const std::uint16_t* rows, int n);

// 대상 목록을 빌려 본다. 개수를 돌려주고 `*out` 에 배열을 준다.
int wheel_fill_rows(const std::uint16_t** out);

}  // namespace game
}  // namespace cdtb
