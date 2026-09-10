#include <windows.h>

#include <cctype>
#include <cstring>
#include <string>

#include "core/file_version.h"
#include "core/vk_name.h"
#include "harness.h"

namespace {

bool is_dotted_quad(const std::string& s) {
    int dots = 0;
    if (s.empty()) return false;
    for (const char c : s) {
        if (c == '.') {
            ++dots;
        } else if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return dots == 3;
}

}  // namespace

TEST(vk_name_known_keys) {
    char buf[16];
    CHECK(std::strcmp(cdtb::vk_name(0x2D, buf, sizeof(buf)), "Insert") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x79, buf, sizeof(buf)), "F10") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x23, buf, sizeof(buf)), "End") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x70, buf, sizeof(buf)), "F1") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x87, buf, sizeof(buf)), "F24") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x41, buf, sizeof(buf)), "A") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x35, buf, sizeof(buf)), "5") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x60, buf, sizeof(buf)), "Num0") == 0);
}

TEST(vk_name_unknown_is_hex) {
    char buf[16];
    CHECK(std::strcmp(cdtb::vk_name(0xE7, buf, sizeof(buf)), "0xE7") == 0);
    CHECK(std::strcmp(cdtb::vk_name(0x100, buf, sizeof(buf)), "0x100") == 0);
}

TEST(file_version_reads_kernel32) {
    wchar_t sys[MAX_PATH]{};
    ::GetSystemDirectoryW(sys, MAX_PATH);
    std::string v;
    CHECK(cdtb::file_version_string(std::wstring(sys) + L"\\kernel32.dll", &v));
    CHECK(is_dotted_quad(v));
}

TEST(file_version_missing_file_is_false) {
    std::string v = "untouched";
    CHECK(!cdtb::file_version_string(L"Z:\\no\\such\\file.exe", &v));
    CHECK_EQ(v, std::string("untouched"));
}
