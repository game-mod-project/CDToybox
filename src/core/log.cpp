#include "core/log.h"

#include <chrono>
#include <cstdio>
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

    // 파일이 없으면 표준 출력으로 낸다. 명령줄 도구(cdtb_probe)가
    // 같은 코드를 돌릴 때 결과를 볼 수 있어야 한다.
    if (!g_file.is_open()) {
        std::printf("%s %.*s\n", level_tag(level),
                    static_cast<int>(message.size()), message.data());
        return;
    }

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
