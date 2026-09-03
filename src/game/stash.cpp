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

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string to_hex(const std::uint8_t* p, std::size_t n) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out += kDigits[p[i] >> 4];
        out += kDigits[p[i] & 0x0F];
    }
    return out;
}

// `s<슬롯>:<보석키>:<원본12hex>` 하나를 읽는다. 어디든 어긋나면
// false 다 - 그 토큰만 버리고 줄은 살린다.
bool parse_socket(const std::string& tok, StashSocket* out) {
    const std::size_t c1 = tok.find(':', 1);
    if (c1 == std::string::npos) return false;
    const std::size_t c2 = tok.find(':', c1 + 1);
    if (c2 == std::string::npos) return false;

    const std::string hex = tok.substr(c2 + 1);
    if (hex.size() != sizeof(out->raw) * 2) return false;

    StashSocket s;
    s.slot = static_cast<std::uint32_t>(
        std::strtoul(tok.c_str() + 1, nullptr, 10));
    s.key = static_cast<std::uint32_t>(
        std::strtoul(tok.c_str() + c1 + 1, nullptr, 10));
    if (s.key == 0) return false;

    for (std::size_t i = 0; i < sizeof(s.raw); ++i) {
        const int hi = hex_value(hex[i * 2]);
        const int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        s.raw[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    *out = s;
    return true;
}

// `item` 줄의 개수 뒤에 붙은 토큰들을 읽는다. 모르는 토큰은 버린다.
void parse_item_extras(const char* rest, StashEntry* e) {
    if (rest == nullptr) return;
    std::string text(rest);
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t') {
            ++pos;
        }
        if (start == pos) break;
        const std::string tok = text.substr(start, pos - start);

        if (tok[0] == 't') {
            e->temper = static_cast<std::uint32_t>(
                std::strtoul(tok.c_str() + 1, nullptr, 10));
        } else if (tok[0] == 'w') {
            e->sharpness = static_cast<std::uint32_t>(
                std::strtoul(tok.c_str() + 1, nullptr, 10));
        } else if (tok[0] == 'e') {
            e->endurance = static_cast<std::uint32_t>(
                std::strtoul(tok.c_str() + 1, nullptr, 10));
        } else if (tok[0] == 's') {
            StashSocket s;
            if (parse_socket(tok, &s)) e->sockets.push_back(s);
        }
    }
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
                   std::to_string(e.count);
            // 없는 것은 안 쓴다. 대부분의 아이템은 둘 다 없어서, 붙이면
            // 옛 파일과 달라 보이고 눈으로 읽기도 나빠진다.
            if (e.temper != 0) out += " t" + std::to_string(e.temper);
            // 내구도는 0 도 뜻이 있다(부서진 것). "없음" 은 따로 둔다.
            if (e.endurance != kStashNoEndurance) {
                out += " e" + std::to_string(e.endurance);
            }
            if (e.sharpness != 0) out += " w" + std::to_string(e.sharpness);
            for (const auto& k : e.sockets) {
                out += " s" + std::to_string(k.slot) + ":" +
                       std::to_string(k.key) + ":" +
                       to_hex(k.raw, sizeof(k.raw));
            }
            out += "\n";
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
            char* rest = nullptr;
            const auto n =
                static_cast<std::int64_t>(std::strtoll(end, &rest, 10));
            if (k != 0 && n > 0) {
                StashEntry e;
                e.key = k;
                e.count = n;
                parse_item_extras(rest, &e);
                sets_.back().items.push_back(std::move(e));
            }
        }
        // 모르는 줄은 조용히 버린다.
    }
    return true;
}

}  // namespace cdtb::game
