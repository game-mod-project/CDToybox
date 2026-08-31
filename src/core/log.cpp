#include "core/log.h"

#include <chrono>
#include <fstream>
#include <mutex>

namespace cdtb::log {
namespace {

std::mutex g_mutex;
std::ofstream g_file;

const char* level_tag(Level l) {
    switch (l) {
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        default:           return "INFO ";
    }
}

}  // namespace

void init(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    g_file.open(path, std::ios::out | std::ios::trunc);
}

void write(Level level, std::string_view message) {
    std::lock_guard lock(g_mutex);
    if (!g_file.is_open()) return;

    const auto now = std::chrono::system_clock::now();
    const auto local = std::chrono::current_zone()->to_local(now);
    g_file << std::format(
        "[{:%H:%M:%S}] {} {}\n",
        std::chrono::floor<std::chrono::milliseconds>(local),
        level_tag(level), message);

    // 크래시 직전 줄이 원인 추적의 유일한 단서이므로 매번 flush 한다.
    g_file.flush();
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
}

}  // namespace cdtb::log
