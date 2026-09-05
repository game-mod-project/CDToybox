#pragma once

namespace cdtb::render {

// 착용 장비 에디터. cplayer 경로로 잡은 착용 장비의 **이미 열린 소켓**에
// 보석을 박고(빈 칸만), 연마를 바꾼다. NPC·컨테이너 핸들이 필요 없다.
// 잠긴 소켓은 열지 못한다(게임 소켓 경로가 아니면 불가).
//
// 읽기는 분석 스레드가 캐시한다(equip_snapshot). 쓰기는 both-realms 로
// 직접 메모리에 쓰고 read-back 검증한다. RE-EQUIP 해야 화면에 반영된다.
void draw_equip_panel(bool* open);

}  // namespace cdtb::render
