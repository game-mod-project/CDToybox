#pragma once

namespace cdtb::render {

// 착용 장비 에디터. 힙 스캔으로 잡은 착용 장비의 소켓에 보석을 박고(빈 칸만),
// **잠긴 칸을 열고**, 담금질(+0x0A)·장비 연마(+0x58)를 바꾼다. 월드에 있는 플레이어형
// 캐릭터(클리프·웅카·데미안) 중 누구의 장비를 볼지 콤보로 고른다(선택은 ini 에 남는다).
// NPC·컨테이너 핸들이 필요 없다.
//
// 잠긴 칸 열기는 `eq_unlock_sockets` - 게임의 지급 코드가 하는 것과 같은 두
// 줄(레코드 +0x70 = 열린 칸 수, 칸 [4])을 직접 쓴다. 게임 소켓 메시지 경로는
// 안 쓴다(실측 2026-09-08, specs/2026-09-07-socket-grant-unlock-research.md).
//
// 읽기는 분석 스레드가 캐시한다(equip_snapshot). 쓰기는 both-realms 로
// 직접 메모리에 쓰고 read-back 검증한다. RE-EQUIP 해야 화면에 반영된다.
void draw_equip_panel(bool* open);

}  // namespace cdtb::render
