#include <windows.h>
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
    // 직전 실행의 로그를 남긴다.
    //
    // 예전에는 그냥 덮어썼다. 그래서 게임이 팅긴 뒤 다시 켜면 원인이
    // 담긴 로그가 사라졌다 - 2026-09-09 에 그 일로 두 번 진단을
    // 놓쳤다. 크래시 로그는 크래시 뒤에야 읽게 되므로 반드시 남아야
    // 한다. 두 판까지 보관한다.
    const std::wstring prev = path + L".prev";
    const std::wstring prev2 = path + L".prev2";
    ::DeleteFileW(prev2.c_str());
    ::MoveFileW(prev.c_str(), prev2.c_str());
    ::MoveFileW(path.c_str(), prev.c_str());
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
