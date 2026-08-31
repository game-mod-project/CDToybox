#include "core/config.h"

#include <charconv>
#include <fstream>
#include <string_view>

namespace cdtb::config {
namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// 10진과 0x 접두 16진을 모두 받는다. 실패 시 fallback.
int to_int(std::string_view v, int fallback) {
    int base = 10;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        base = 16;
        v.remove_prefix(2);
    }
    int out = 0;
    const char* first = v.data();
    const char* last = v.data() + v.size();
    const auto res = std::from_chars(first, last, out, base);
    if (res.ec != std::errc{} || res.ptr != last) return fallback;
    return out;
}

}  // namespace

Config load(const std::wstring& path) {
    Config c;
    std::ifstream in(path);
    if (!in.good()) return c;

    std::string line;
    while (std::getline(in, line)) {
        const std::string_view sv = trim(line);
        if (sv.empty() || sv.front() == ';' || sv.front() == '#' ||
            sv.front() == '[') {
            continue;
        }
        const std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;

        const std::string_view key = trim(sv.substr(0, eq));
        const std::string_view val = trim(sv.substr(eq + 1));
        if (val.empty()) continue;

        if (key == "toggle_key") {
            c.toggle_key = to_int(val, c.toggle_key);
        } else if (key == "unload_key") {
            c.unload_key = to_int(val, c.unload_key);
        } else if (key == "show_diagnostics") {
            c.show_diagnostics = (to_int(val, 1) != 0);
        }
    }
    return c;
}

bool save(const std::wstring& path, const Config& c) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.good()) return false;
    out << "[CDToybox]\n";
    out << "; Win32 virtual-key code\n";
    out << "toggle_key = 0x" << std::hex << c.toggle_key << "\n";
    out << "unload_key = 0x" << std::hex << c.unload_key << "\n";
    out << std::dec;
    out << "show_diagnostics = " << (c.show_diagnostics ? 1 : 0) << "\n";
    return out.good();
}

}  // namespace cdtb::config
