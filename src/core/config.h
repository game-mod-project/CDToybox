#pragma once

#include <string>

namespace cdtb {

struct Config {
    int toggle_key = 0x2D;   // VK_INSERT
    int unload_key = 0x23;   // VK_END
    bool show_diagnostics = true;
};

namespace config {

// 파일이 없거나 읽을 수 없으면 기본값을 반환한다.
Config load(const std::wstring& path);
bool save(const std::wstring& path, const Config& c);

}  // namespace config
}  // namespace cdtb
