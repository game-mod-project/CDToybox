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

// 화면이 읽어 갈 고리. init 에서 한 번 잡고 그 뒤로는 칸을 덮어쓴다 - write 는
// 훅 안에서도 불리므로 줄마다 새로 할당하지 않는 편이 낫다.
std::vector<RingLine> g_ring;
std::uint64_t g_total = 0;

const char* level_tag(Level l) {
    switch (l) {
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        default:           return "INFO ";
    }
}

}  // namespace

void ring_push(std::vector<RingLine>& ring, std::uint64_t& total, Level level,
               std::string_view time, std::string_view text) {
    if (ring.empty()) return;   // 자리가 없으면 조용히 버린다(파일에는 남는다)
    ++total;
    // 문자열은 칸을 재사용한다(assign). 한 바퀴 돈 뒤에는 대개 용량이 충분해
    // 할당이 일어나지 않는다.
    RingLine& slot = ring[static_cast<std::size_t>((total - 1) % ring.size())];
    slot.seq = total;
    slot.level = level;
    slot.time.assign(time);
    slot.text.assign(text);
}

std::uint64_t ring_collect(const std::vector<RingLine>& ring,
                           std::uint64_t total, std::uint64_t since,
                           std::vector<RingLine>* out) {
    if (out == nullptr || ring.empty() || total == 0) return 0;
    const std::uint64_t cap = static_cast<std::uint64_t>(ring.size());
    // 고리에 아직 남아 있는 가장 오래된 줄의 번호.
    const std::uint64_t oldest = total > cap ? total - cap + 1 : 1;
    std::uint64_t from = since + 1;
    std::uint64_t missed = 0;
    if (from < oldest) {
        missed = oldest - from;   // 그 사이는 덮여 사라졌다
        from = oldest;
    }
    for (std::uint64_t s = from; s <= total; ++s) {
        const RingLine& l = ring[static_cast<std::size_t>((s - 1) % cap)];
        // 번호가 안 맞으면 그 칸은 이미 다음 바퀴에 덮였다. 건너뛴다.
        if (l.seq != s) continue;
        out->push_back(l);
    }
    return missed;
}

std::uint64_t ring_since(std::uint64_t since, std::vector<RingLine>* out) {
    std::lock_guard lock(g_mutex);
    return ring_collect(g_ring, g_total, since, out);
}

std::uint64_t ring_count() {
    std::lock_guard lock(g_mutex);
    return g_total;
}

void init(const std::wstring& path) {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
    if (g_ring.empty()) g_ring.resize(kRingLines);
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

    // 시각을 **먼저** 만든다. 파일과 화면이 같은 글자를 쓰게 하려면 한 자리에서
    // 나와야 한다. 실패해도 로그 한 줄 때문에 예외를 던지지는 않는다 - 이 함수는
    // 훅 안에서도 불린다.
    std::string stamp;
    try {
        const auto now = std::chrono::system_clock::now();
        const auto local = std::chrono::current_zone()->to_local(now);
        stamp = std::format(
            "{:%H:%M:%S}",
            std::chrono::floor<std::chrono::milliseconds>(local));
    } catch (...) {
        stamp = "--:--:--.---";
    }

    // 창이 안 열려 있어도 쌓아 둔다. 열었을 때 직전 상황이 보여야 쓸모가 있다.
    if (g_ring.empty()) g_ring.resize(kRingLines);
    ring_push(g_ring, g_total, level, stamp, message);

    // 파일이 없으면 표준 출력으로 낸다. 명령줄 도구(cdtb_probe)가
    // 같은 코드를 돌릴 때 결과를 볼 수 있어야 한다.
    if (!g_file.is_open()) {
        std::printf("%s %.*s\n", level_tag(level),
                    static_cast<int>(message.size()), message.data());
        return;
    }

    g_file << std::format("[{}] {} {}\n", stamp, level_tag(level), message);

    // 크래시 직전 줄이 원인 추적의 유일한 단서이므로 매번 flush 한다.
    g_file.flush();
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file.is_open()) g_file.close();
}

}  // namespace cdtb::log
