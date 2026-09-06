#pragma once

#include <format>
#include <string>
#include <string_view>
#include <utility>

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

}  // namespace cdtb::log
