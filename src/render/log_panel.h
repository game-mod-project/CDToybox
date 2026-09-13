#pragma once

namespace cdtb::render {

// 로그 창. core/log 의 고리 버퍼에서 **새 줄만** 받아 그린다.
// 게임 메모리를 읽지도 쓰지도 않는다 - 본창의 "*"(쓰기) 표식이 없는 이유다.
void draw_log_panel(bool* open);

}  // namespace cdtb::render
