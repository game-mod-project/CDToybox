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

    // 소켓 상한 올리기. 0 이면 안 건다, 1..5 면 그 값으로 건다.
    //
    // 아이템표는 매 실행 exe 에서 다시 읽히므로 이 설정이 있어야 세션마다
    // 다시 걸린다. 자세한 것은 `game::socket_cap_raise` 주석.
    int socket_cap = 0;
};

namespace config {

// 파일이 없거나 읽을 수 없으면 기본값을 반환한다.
Config load(const std::wstring& path);
bool save(const std::wstring& path, const Config& c);

}  // namespace config
}  // namespace cdtb
