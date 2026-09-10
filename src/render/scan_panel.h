#pragma once

namespace cdtb::render {

// 카메라 패널을 그린다. 스캔 패널에서 고정한 주소를 쓰므로
// 같은 번역 단위에 있다.
void draw_camera_panel(bool* open);

// 워커 스레드를 정리한다. 오버레이 해체 시 부른다.
void shutdown_scan_panel();

}  // namespace cdtb::render
