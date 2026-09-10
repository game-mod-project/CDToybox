#include "core/file_version.h"

#include <windows.h>

#include <cstdio>
#include <vector>

namespace cdtb {

bool file_version_string(const std::wstring& path, std::string* out) {
    if (out == nullptr) return false;
    DWORD handle = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) return false;
    std::vector<unsigned char> block(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, block.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT len = 0;
    if (!::VerQueryValueW(block.data(), L"\\", reinterpret_cast<LPVOID*>(&info),
                          &len) ||
        info == nullptr || len < sizeof(VS_FIXEDFILEINFO)) {
        return false;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
                  static_cast<unsigned>(HIWORD(info->dwFileVersionMS)),
                  static_cast<unsigned>(LOWORD(info->dwFileVersionMS)),
                  static_cast<unsigned>(HIWORD(info->dwFileVersionLS)),
                  static_cast<unsigned>(LOWORD(info->dwFileVersionLS)));
    *out = buf;
    return true;
}

}  // namespace cdtb
