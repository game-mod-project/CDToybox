#include "render/ui_persist_state.h"

namespace cdtb::render {

bool UiPersist::header_open(std::string_view key) const {
    auto it = headers_.find(key);
    return it != headers_.end() && it->second;
}

void UiPersist::set_header_open(std::string_view key, bool open) {
    headers_[std::string(key)] = open;
}

bool UiPersist::window_open(std::string_view key, bool def) const {
    auto it = windows_.find(key);
    return it == windows_.end() ? def : it->second;
}

void UiPersist::set_window_open(std::string_view key, bool open) {
    windows_[std::string(key)] = open;
}

namespace {

void append_all(std::string* out, char kind,
                const std::map<std::string, bool, std::less<>>& m) {
    for (const auto& [key, on] : m) {
        out->push_back(kind);
        out->push_back('=');
        out->append(key);
        out->push_back('=');
        out->push_back(on ? '1' : '0');
        out->push_back('\n');
    }
}

}  // namespace

std::string UiPersist::serialize() const {
    std::string out;
    append_all(&out, 'H', headers_);
    append_all(&out, 'W', windows_);
    return out;
}

void UiPersist::parse_line(std::string_view line) {
    if (line.size() < 4 || line[1] != '=') return;
    const char kind = line[0];
    if (kind != 'H' && kind != 'W') return;

    // 키에 `=` 가 있을 수 있다(보관함 세트 이름). 값은 **마지막** `=` 뒤다.
    const std::size_t sep = line.rfind('=');
    if (sep <= 1) return;

    const std::string_view key = line.substr(2, sep - 2);
    const std::string_view value = line.substr(sep + 1);
    if (key.empty()) return;
    if (value != "0" && value != "1") return;

    const bool on = value == "1";
    if (kind == 'H') {
        set_header_open(key, on);
    } else {
        set_window_open(key, on);
    }
}

}  // namespace cdtb::render
