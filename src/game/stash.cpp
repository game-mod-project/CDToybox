#include "game/stash.h"

#include <algorithm>
#include <cstdlib>

namespace cdtb::game {
namespace {

// 앞뒤 공백을 떼고 캐리지 리턴도 지운다. 파일이 CRLF 로 저장된다.
std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) {
        --e;
    }
    return s.substr(b, e - b);
}

bool starts_with(const std::string& s, const char* p) {
    const std::size_t n = std::char_traits<char>::length(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}

}  // namespace

bool Stash::is_favorite(std::uint32_t key) const {
    return std::find(favs_.begin(), favs_.end(), key) != favs_.end();
}

void Stash::toggle_favorite(std::uint32_t key) {
    const auto it = std::find(favs_.begin(), favs_.end(), key);
    if (it != favs_.end()) {
        favs_.erase(it);       // 나머지 순서는 그대로 둔다
    } else {
        favs_.push_back(key);
    }
}

StashSet* Stash::set_at(int i) {
    if (i < 0 || i >= set_count()) return nullptr;
    return &sets_[static_cast<std::size_t>(i)];
}

const StashSet* Stash::set_at(int i) const {
    if (i < 0 || i >= set_count()) return nullptr;
    return &sets_[static_cast<std::size_t>(i)];
}

int Stash::add_set(const std::string& name) {
    sets_.push_back(StashSet{name, {}});
    return set_count() - 1;
}

void Stash::remove_set(int i) {
    if (i < 0 || i >= set_count()) return;
    sets_.erase(sets_.begin() + i);
}

std::string Stash::serialize() const {
    std::string out = "# CDToybox 보관함\n";
    for (const auto k : favs_) {
        out += "fav " + std::to_string(k) + "\n";
    }
    for (const auto& s : sets_) {
        out += "set " + s.name + "\n";
        for (const auto& e : s.items) {
            out += "item " + std::to_string(e.key) + " " +
                   std::to_string(e.count) + "\n";
        }
    }
    return out;
}

bool Stash::parse(const std::string& text) {
    favs_.clear();
    sets_.clear();

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        const std::string line =
            trim(text.substr(pos, (nl == std::string::npos) ? nl : nl - pos));
        if (nl == std::string::npos) {
            pos = text.size() + 1;
        } else {
            pos = nl + 1;
        }
        if (line.empty() || line[0] == '#') continue;

        if (starts_with(line, "fav ")) {
            const auto k = static_cast<std::uint32_t>(
                std::strtoul(line.c_str() + 4, nullptr, 10));
            if (k != 0 && !is_favorite(k)) favs_.push_back(k);
        } else if (starts_with(line, "set ")) {
            // 이름에 공백이 들어간다. 줄 끝까지가 이름이다.
            sets_.push_back(StashSet{trim(line.substr(4)), {}});
        } else if (starts_with(line, "item ")) {
            // 세트 밖의 항목은 버린다. 손으로 고치다 깨질 수 있다.
            if (sets_.empty()) continue;
            char* end = nullptr;
            const auto k = static_cast<std::uint32_t>(
                std::strtoul(line.c_str() + 5, &end, 10));
            const auto n =
                static_cast<std::int64_t>(std::strtoll(end, nullptr, 10));
            if (k != 0 && n > 0) {
                sets_.back().items.push_back(StashEntry{k, n});
            }
        }
        // 모르는 줄은 조용히 버린다.
    }
    return true;
}

}  // namespace cdtb::game
