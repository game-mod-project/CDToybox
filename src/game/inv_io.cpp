#include "game/inv_io.h"

#include <windows.h>  // SEH(__try/__except, EXCEPTION_EXECUTE_HANDLER)

#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "core/log.h"
#include "game/inventory.h"

namespace cdtb::game {
namespace {

// 레코드 오프셋(inventory.cpp 와 같은 실측값).
constexpr std::size_t kRecTemper = 0x0A;      // u16 담금질
constexpr std::size_t kRecSharpness = 0x58;   // u16 연마
constexpr std::size_t kSocketStride = 6;      // 소켓 한 칸 바이트
constexpr std::size_t kSockGem = 0;           // u16 박힌 보석 순번
constexpr std::size_t kSockMarker = 2;        // u16 채움 마커(0xFFFF)
constexpr std::size_t kSockIndex = 4;         // u8  열림=k, 잠김=0xFF

// 인프로세스 직접 쓰기(주입 DLL 전용). SEH 로 감싼다. equip.cpp 와
// 같은 꼴이다 - C++ 소멸자가 필요한 지역 객체를 같은 스코프에 두면
// MSVC 가 __try 를 거부(C2712)하므로 최소 함수로 떼어 둔다.
bool wr16(std::uintptr_t a, std::uint16_t v) {
    __try {
        *reinterpret_cast<volatile std::uint16_t*>(a) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint16_t rd16(const mem::Reader& r, std::uintptr_t a) {
    std::uint16_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}
std::uint8_t rd8(const mem::Reader& r, std::uintptr_t a) {
    std::uint8_t v = 0;
    return r.read_value(a, &v) ? v : 0;
}

// 담금질·연마를 제자리로 쓰고 읽어 확인한다. 이미 같으면 참(건드릴
// 필요 없음)이되 바꿨다고 세지 않도록 changed 로 알린다.
bool set_u16(const mem::Reader& r, std::uintptr_t addr, std::uint16_t want,
             bool* changed) {
    *changed = false;
    if (rd16(r, addr) == want) return true;
    if (!wr16(addr, want)) return false;
    if (rd16(r, addr) != want) return false;
    *changed = true;
    return true;
}

// --- 파싱 도우미 ---

// "key=value" 토큰에서 key 가 맞으면 value 문자열을 준다.
bool kv(const std::string& tok, const char* key, std::string* val) {
    const std::size_t klen = std::strlen(key);
    if (tok.size() <= klen || tok.compare(0, klen, key) != 0 ||
        tok[klen] != '=') {
        return false;
    }
    *val = tok.substr(klen + 1);
    return true;
}

long long to_ll(const std::string& s, bool* ok) {
    if (s.empty()) {
        *ok = false;
        return 0;
    }
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    *ok = (end != nullptr && *end == '\0');
    return v;
}

// 소켓 토큰 목록 "3318:0,FFFF:1,-:2" 을 푼다.
void parse_sockets(const std::string& list, std::vector<InvSocketSnap>* out) {
    std::size_t i = 0;
    while (i < list.size()) {
        std::size_t comma = list.find(',', i);
        if (comma == std::string::npos) comma = list.size();
        const std::string tok = list.substr(i, comma - i);
        i = comma + 1;
        if (tok.empty()) continue;

        const std::size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        const std::string g = tok.substr(0, colon);
        const std::string k = tok.substr(colon + 1);

        InvSocketSnap s;
        bool ok = false;
        const long long slot = to_ll(k, &ok);
        if (!ok || slot < 0) continue;
        s.slot = static_cast<std::uint32_t>(slot);

        if (g == "-") {
            s.locked = true;
            s.gem = -1;
        } else if (g == "FFFF" || g == "ffff") {
            s.locked = false;
            s.gem = -1;   // 빈 열린 칸
        } else {
            const long long gem = to_ll(g, &ok);
            if (!ok || gem < 0) continue;
            s.locked = false;
            s.gem = static_cast<int>(gem);
        }
        out->push_back(s);
    }
}

}  // namespace

std::string inv_serialize(const std::vector<InvItemSnap>& items) {
    std::ostringstream os;
    os << "# CDToybox inventory export v2\n";
    os << "# 담금질·연마·열린 소켓 보석만 복원됩니다. 잠긴 소켓과 "
          "인벤토리에 없는 아이템은 건너뜁니다.\n";

    int cur_kind = -1;
    for (const auto& it : items) {
        if (static_cast<int>(it.kind) != cur_kind) {
            cur_kind = static_cast<int>(it.kind);
            os << "[kind " << cur_kind << "]\n";
        }
        os << "순번=" << it.index << " qty=" << it.count << " temper="
           << it.temper << " sharp=" << it.sharpness << " dur=";
        if (it.endurance == 0xFFFF) {
            os << '-';
        } else {
            os << it.endurance;
        }
        if (!it.sockets.empty()) {
            os << " sockets=";
            for (std::size_t i = 0; i < it.sockets.size(); ++i) {
                const auto& s = it.sockets[i];
                if (i != 0) os << ',';
                if (s.locked) {
                    os << '-';
                } else if (s.gem < 0) {
                    os << "FFFF";
                } else {
                    os << s.gem;
                }
                os << ':' << s.slot;
            }
        }
        os << '\n';
    }
    return os.str();
}

bool inv_parse(const std::string& text, std::vector<InvItemSnap>* out) {
    if (out == nullptr) return false;
    out->clear();

    std::istringstream is(text);
    std::string line;
    int cur_kind = 0;
    while (std::getline(is, line)) {
        // 끝의 CR 제거(윈도우 줄끝).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // 앞 공백 스킵.
        std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        if (line[b] == '#') continue;

        if (line[b] == '[') {
            // [kind N]
            std::string v;
            std::istringstream ls(line.substr(b + 1));
            std::string word;
            ls >> word;   // "kind"
            if (word == "kind") {
                long long k = 0;
                if (ls >> k) cur_kind = static_cast<int>(k);
            }
            continue;
        }

        // 아이템 줄: 공백으로 토큰화해 key=value 를 줍는다.
        std::istringstream ls(line);
        std::string tok;
        InvItemSnap it;
        it.kind = static_cast<std::uint16_t>(cur_kind);
        bool has_index = false;
        while (ls >> tok) {
            std::string v;
            bool ok = false;
            if (kv(tok, "순번", &v)) {
                const long long x = to_ll(v, &ok);
                if (ok && x >= 0) {
                    it.index = static_cast<std::uint32_t>(x);
                    has_index = true;
                }
            } else if (kv(tok, "qty", &v)) {
                const long long x = to_ll(v, &ok);
                if (ok) it.count = x;
            } else if (kv(tok, "temper", &v)) {
                const long long x = to_ll(v, &ok);
                if (ok && x >= 0) it.temper = static_cast<std::uint16_t>(x);
            } else if (kv(tok, "sharp", &v)) {
                const long long x = to_ll(v, &ok);
                if (ok && x >= 0) it.sharpness = static_cast<std::uint16_t>(x);
            } else if (kv(tok, "dur", &v)) {
                if (v == "-") {
                    it.endurance = 0xFFFF;
                } else {
                    const long long x = to_ll(v, &ok);
                    if (ok && x >= 0) {
                        it.endurance = static_cast<std::uint32_t>(x);
                    }
                }
            } else if (kv(tok, "sockets", &v)) {
                parse_sockets(v, &it.sockets);
            }
        }
        if (has_index) out->push_back(std::move(it));
    }
    return true;
}

bool inventory_export(const mem::Reader& reader,
                      std::vector<InvItemSnap>* out) {
    if (out == nullptr) return false;
    out->clear();

    const std::uintptr_t comp = inventory_component();
    if (comp == 0) return false;

    std::vector<InventoryContainer> conts;
    if (!read_inventory_containers(reader, comp, &conts)) return false;

    for (const auto& c : conts) {
        std::vector<InventoryRecord> recs;
        if (!read_inventory_records(reader, c, &recs)) continue;
        for (const auto& rec : recs) {
            InvItemSnap it;
            it.kind = c.kind;
            it.slot = rec.slot;
            it.index = rec.index;
            it.count = rec.count;
            it.temper = static_cast<std::uint16_t>(rec.temper);
            it.sharpness = static_cast<std::uint16_t>(rec.sharpness);
            it.endurance = rec.endurance;

            std::vector<InventorySocket> socks;
            if (read_inventory_sockets(reader, rec, &socks)) {
                for (const auto& s : socks) {
                    InvSocketSnap ss;
                    ss.slot = s.slot;
                    ss.locked = (s.raw[kSockIndex] == 0xFF);
                    ss.gem = (s.index == 0xFFFF)
                                 ? -1
                                 : static_cast<int>(s.index);
                    it.sockets.push_back(ss);
                }
            }
            out->push_back(std::move(it));
        }
    }
    return true;
}

bool inventory_import(const mem::Reader& reader,
                      const std::vector<InvItemSnap>& items,
                      ImportResult* result) {
    ImportResult r;
    const std::uintptr_t comp = inventory_component();
    if (comp == 0) {
        if (result != nullptr) *result = r;
        return false;
    }

    // 현재 인벤토리를 (종류,순번) -> 레코드 큐(슬롯 순) 로 모은다.
    std::vector<InventoryContainer> conts;
    if (!read_inventory_containers(reader, comp, &conts)) {
        if (result != nullptr) *result = r;
        return false;
    }
    std::map<std::pair<std::uint16_t, std::uint32_t>,
             std::deque<InventoryRecord>>
        pool;
    for (const auto& c : conts) {
        std::vector<InventoryRecord> recs;
        if (!read_inventory_records(reader, c, &recs)) continue;
        for (const auto& rec : recs) {
            pool[{c.kind, rec.index}].push_back(rec);
        }
    }

    for (const auto& it : items) {
        auto pit = pool.find({it.kind, it.index});
        if (pit == pool.end() || pit->second.empty()) {
            ++r.not_found;
            continue;
        }
        const InventoryRecord rec = pit->second.front();
        pit->second.pop_front();
        ++r.matched;

        bool changed = false;
        if (set_u16(reader, rec.address + kRecTemper, it.temper, &changed)) {
            if (changed) ++r.temper_set;
        } else {
            ++r.write_failed;
        }
        if (set_u16(reader, rec.address + kRecSharpness, it.sharpness,
                    &changed)) {
            if (changed) ++r.sharp_set;
        } else {
            ++r.write_failed;
        }

        // 소켓: 파일이 채워 두라는 칸만, 그것도 현재 열린 칸에만 쓴다.
        if (rec.sockets != 0 && rec.socket_count > 0 &&
            rec.socket_count <= kMaxSockets) {
            for (const auto& fs : it.sockets) {
                if (fs.gem < 0) continue;   // 빈/잠긴 칸은 채울 것이 없다
                if (fs.slot >= rec.socket_count) continue;
                const std::uintptr_t sp =
                    rec.sockets +
                    static_cast<std::uintptr_t>(fs.slot) * kSocketStride;
                // 현재 이 칸이 잠겼으면 절대 쓰지 않는다(언락 위조 금지).
                if (rd8(reader, sp + kSockIndex) == 0xFF) {
                    ++r.locked_skipped;
                    continue;
                }
                const std::uint16_t gem = static_cast<std::uint16_t>(fs.gem);
                if (rd16(reader, sp + kSockGem) == gem) continue;  // 이미 같음
                if (!wr16(sp + kSockGem, gem) ||
                    !wr16(sp + kSockMarker, 0xFFFF) ||
                    rd16(reader, sp + kSockGem) != gem) {
                    ++r.write_failed;
                    continue;
                }
                ++r.gems_set;
            }
        }
    }

    log::infof(
        "인벤토리 import: 짝 {}개, 담금질 {}·연마 {}·보석 {}칸, 잠김 {}·없음 "
        "{}·실패 {}",
        r.matched, r.temper_set, r.sharp_set, r.gems_set, r.locked_skipped,
        r.not_found, r.write_failed);
    if (result != nullptr) *result = r;
    return true;
}

}  // namespace cdtb::game
