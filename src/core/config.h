#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace cdtb {

struct Config {
    int toggle_key = 0x2D;   // VK_INSERT
    // F10. 예전 기본값은 End 였는데, 프리카메라의 상하 이동이
    // PgUp/PgDn 이라 바로 아래 End 를 잘못 눌러 오버레이가 꺼지는
    // 일이 생겼다. 이동 키에서 멀리 떨어뜨린다.
    int unload_key = 0x79;   // VK_F10
    bool show_diagnostics = true;

    // 소켓 상한 올리기. 아이템표는 매 실행 exe 에서 다시 읽히므로 이
    // 설정이 있어야 세션마다 다시 걸린다.
    // 자세한 것은 `game::socket_cap_apply` 주석.

    // 부위 하나에 줄 칸 수. 부위는 (분류 +0xA3, 장비타입 +0x42) 쌍이다 -
    // 분류만으로는 갑옷(3/5)과 망토(3/75)가 안 갈린다.
    struct SocketCapPart {
        int category = 0;
        int equip_type = 0;
        int want = 0;
    };
    // ini 표기: `socket_cap_parts = 3:5=5,24:4=5,49:10=3`
    std::vector<SocketCapPart> socket_cap_parts;

    // (구) 전 부위 일괄. 부위 목록이 비어 있을 때만 쓴다 - 예전 ini 를
    // 그대로 읽어 주기 위한 것이고, 화면에서 한 번 저장하면 부위 목록으로
    // 바뀐다. 0 이면 안 건다.
    int socket_cap = 0;
};

namespace config {

// 파일이 없거나 읽을 수 없으면 기본값을 반환한다.
Config load(const std::wstring& path);
bool save(const std::wstring& path, const Config& c);

// `3:5=5,24:4=2` 를 부위 목록으로. 한 항목이 어긋나면 그 항목만 버린다 -
// 파일 한 줄 때문에 나머지 설정을 잃을 이유가 없다. 값이 0..5 밖이거나
// 0 이면(=안 건드림) 버린다. 파일 없이 시험할 수 있게 밖으로 낸다.
std::vector<Config::SocketCapPart> parse_socket_cap_parts(std::string_view v);

}  // namespace config
}  // namespace cdtb
