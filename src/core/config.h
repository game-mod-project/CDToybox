#pragma once

#include <string>

namespace cdtb {

struct Config {
    int toggle_key = 0x2D;   // VK_INSERT
    // F10. 예전 기본값은 End 였는데, 프리카메라의 상하 이동이
    // PgUp/PgDn 이라 바로 아래 End 를 잘못 눌러 오버레이가 꺼지는
    // 일이 생겼다. 이동 키에서 멀리 떨어뜨린다.
    int unload_key = 0x79;   // VK_F10
    bool show_diagnostics = true;
};

namespace config {

// 파일이 없거나 읽을 수 없으면 기본값을 반환한다.
Config load(const std::wstring& path);
bool save(const std::wstring& path, const Config& c);

}  // namespace config
}  // namespace cdtb
