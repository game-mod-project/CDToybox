#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cdtb::log {

enum class Level { Info, Warn, Error };

void init(const std::wstring& path);
void write(Level level, std::string_view message);
void shutdown();

template <class... A>
void infof(std::format_string<A...> f, A&&... a) {
    write(Level::Info, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void warnf(std::format_string<A...> f, A&&... a) {
    write(Level::Warn, std::format(f, std::forward<A>(a)...));
}
template <class... A>
void errorf(std::format_string<A...> f, A&&... a) {
    write(Level::Error, std::format(f, std::forward<A>(a)...));
}

// --------------------------------------------------------------- 고리 버퍼
//
// 화면(로그 창)이 읽어 갈 최근 줄을 들고 있는다. **파일 쓰기와 같은 잠금 안에서**
// 채우므로 파일과 순서가 어긋나지 않는다. 파일은 그대로 남는다 - 크래시 뒤에 읽는
// 것은 여전히 파일이다(crash-log-file).
inline constexpr std::size_t kRingLines = 2000;

struct RingLine {
    std::uint64_t seq = 0;    // 1부터. 번호가 끊기면 그 사이를 놓친 것이다
    Level level = Level::Info;
    std::string time;         // "12:44:48.465"
    std::string text;
};

// `since` 보다 큰 줄을 out 에 **덧붙인다**(비우지 않는다).
// 반환값은 **놓친 줄 수** - 고리가 한 바퀴 돌아 그 사이가 사라졌으면 0 보다 크다.
// 화면이 그 수를 내야 사용자가 "왜 중간이 비지" 를 혼자 겪지 않는다.
std::uint64_t ring_since(std::uint64_t since, std::vector<RingLine>* out);

// 지금까지 쓴 줄 수(= 마지막 일련번호).
std::uint64_t ring_count();

// 위 둘의 알맹이. 전역을 건드리지 않아 시험이 그대로 부를 수 있다 - 안 그러면
// 고리를 시험하려고 진짜 로그 파일을 열거나 표준 출력을 더럽혀야 한다.
void ring_push(std::vector<RingLine>& ring, std::uint64_t& total, Level level,
               std::string_view time, std::string_view text);
std::uint64_t ring_collect(const std::vector<RingLine>& ring,
                           std::uint64_t total, std::uint64_t since,
                           std::vector<RingLine>* out);

}  // namespace cdtb::log
