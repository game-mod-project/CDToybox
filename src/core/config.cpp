#include "core/config.h"

#include <charconv>
#include <fstream>
#include <string_view>

namespace cdtb::config {
namespace {

// 화면 상한. `inventory.h` 의 표와 같은 값인데 core 는 game 을 모르므로 여기
// 적는다 - ini 를 손으로 고쳐 엔진 천장(732)을 넘기면 세이브가 깨진다.
constexpr int kBagScreenCap = 700;

// 구동 자리는 많아야 몇 곳이다. 목록이 길어지는 것은 사람이 로그를 통째로
// 붙여 넣은 것이므로 거기서 끊는다 - grant 쪽 저장 칸도 이 크기다.
constexpr std::size_t kMaxDriveSites = 8;


std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// 10진과 0x 접두 16진을 모두 받는다. 실패 시 fallback.
int to_int(std::string_view v, int fallback) {
    int base = 10;
    if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) {
        base = 16;
        v.remove_prefix(2);
    }
    int out = 0;
    const char* first = v.data();
    const char* last = v.data() + v.size();
    const auto res = std::from_chars(first, last, out, base);
    if (res.ec != std::errc{} || res.ptr != last) return fallback;
    return out;
}

}  // namespace

// `3:5=5,24:4=5` 를 부위 목록으로. 한 항목이라도 꼴이 안 맞으면 그
// 항목만 버린다 - 파일 하나가 어긋났다고 나머지 설정까지 잃을 이유가 없다.
// 값 범위(0..5)를 넘는 것도 버린다. 게임 표에 이상한 값을 쓰느니.
std::vector<Config::SocketCapPart> parse_socket_cap_parts(std::string_view v) {
    std::vector<Config::SocketCapPart> out;
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        std::string_view item = trim(v.substr(0, comma));
        v = (comma == std::string_view::npos) ? std::string_view{}
                                              : v.substr(comma + 1);
        if (item.empty()) continue;

        const std::size_t colon = item.find(':');
        const std::size_t eq = item.find('=');
        if (colon == std::string_view::npos || eq == std::string_view::npos ||
            eq < colon) {
            continue;
        }
        const int cat = to_int(trim(item.substr(0, colon)), -1);
        const int et = to_int(trim(item.substr(colon + 1, eq - colon - 1)), -1);
        const int want = to_int(trim(item.substr(eq + 1)), -1);
        if (cat < 0 || cat > 255) continue;
        if (et < 0 || et > 0xFFFF) continue;
        if (want < 0 || want > 5) continue;
        if (want == 0) continue;   // 0 은 "안 건드림" 이라 적을 이유가 없다
        out.push_back(Config::SocketCapPart{cat, et, want});
    }
    return out;
}

// `4883:1,4884:1` 을 지식 목록으로. 번호는 지식 표의 색인이고 레벨은 1 부터다.
// 레벨 0 은 "안 건다" 라 적을 이유가 없으니 버린다.
std::vector<Config::KnowKeep> parse_knowledge_keep(std::string_view v) {
    std::vector<Config::KnowKeep> out;
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        std::string_view item = trim(v.substr(0, comma));
        v = (comma == std::string_view::npos) ? std::string_view{}
                                              : v.substr(comma + 1);
        if (item.empty()) continue;

        const std::size_t colon = item.find(':');
        if (colon == std::string_view::npos) continue;
        const int num = to_int(trim(item.substr(0, colon)), -1);
        const int lv = to_int(trim(item.substr(colon + 1)), -1);
        if (num < 0 || num > 65535) continue;
        if (lv < 1 || lv > 99) continue;
        out.push_back(Config::KnowKeep{num, lv});
    }
    return out;
}

std::vector<Config::BagKeep> parse_bag_keep(std::string_view v) {
    std::vector<Config::BagKeep> out;
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        std::string_view item = trim(v.substr(0, comma));
        v = (comma == std::string_view::npos) ? std::string_view{}
                                              : v.substr(comma + 1);
        if (item.empty()) continue;

        const std::size_t colon = item.find(':');
        if (colon == std::string_view::npos) continue;
        const int kind = to_int(trim(item.substr(0, colon)), -1);
        const int target = to_int(trim(item.substr(colon + 1)), -1);
        if (kind < 0 || kind > 65535) continue;
        // 0 은 "이 종류는 안 건드린다" 라 유효하다. 위쪽은 화면 상한을 넘지
        // 않게 막는다 - ini 를 손으로 고쳐 엔진 천장을 넘기면 세이브가 깨진다.
        if (target < 0 || target > kBagScreenCap) continue;
        out.push_back(Config::BagKeep{kind, target});
    }
    return out;
}

// `0x2AD0A7D,0x2A2BBBF` 를 구동 자리 목록으로.
//
// 값은 **모듈 RVA** 다. 0 은 "모르는 자리" 를 뜻하는 내부 표식이라 받지 않고,
// 모듈 크기를 넘는 값도 받지 않는다 - 오타로 엉뚱한 자리를 안전하다고 선언하면
// 지급 처리기가 게임 코드 한복판에서 재진입해 게임이 멈춘다.
std::vector<std::uint64_t> parse_drive_sites(std::string_view v) {
    std::vector<std::uint64_t> out;
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        std::string_view item = trim(v.substr(0, comma));
        v = (comma == std::string_view::npos) ? std::string_view{}
                                              : v.substr(comma + 1);
        if (item.empty()) continue;
        const int rva = to_int(item, -1);
        if (rva <= 0) continue;   // 0 과 못 읽은 것은 버린다
        if (out.size() >= kMaxDriveSites) break;
        out.push_back(static_cast<std::uint64_t>(rva));
    }
    return out;
}

Config load(const std::wstring& path) {
    Config c;
    std::ifstream in(path);
    if (!in.good()) return c;

    std::string line;
    while (std::getline(in, line)) {
        const std::string_view sv = trim(line);
        if (sv.empty() || sv.front() == ';' || sv.front() == '#' ||
            sv.front() == '[') {
            continue;
        }
        const std::size_t eq = sv.find('=');
        if (eq == std::string_view::npos) continue;

        const std::string_view key = trim(sv.substr(0, eq));
        const std::string_view val = trim(sv.substr(eq + 1));
        if (val.empty()) continue;

        if (key == "toggle_key") {
            c.toggle_key = to_int(val, c.toggle_key);
        } else if (key == "unload_key") {
            c.unload_key = to_int(val, c.unload_key);
        } else if (key == "show_diagnostics") {
            c.show_diagnostics = (to_int(val, 1) != 0);
        } else if (key == "socket_cap") {
            // 소켓 벡터가 다섯 칸이라 그 밖의 값은 뜻이 없다. 파일이
            // 이상하면 끈 것으로 본다 - 게임 표를 이상한 값으로 쓰느니.
            const int v = to_int(val, 0);
            c.socket_cap = (v < 0 || v > 5) ? 0 : v;
        } else if (key == "socket_cap_parts") {
            c.socket_cap_parts = parse_socket_cap_parts(val);
        } else if (key == "vehicle_wheel_extend") {
            c.vehicle_wheel_extend = (to_int(val, 0) != 0);
        } else if (key == "summon_diag") {
            c.summon_diag = (to_int(val, 0) != 0);
        } else if (key == "wheel_fill") {
            c.wheel_fill = (to_int(val, 0) != 0);
        } else if (key == "wheel_swap_slot") {
            c.wheel_swap_slot = (to_int(val, 1) != 0);
        } else if (key == "knowledge_keep") {
            c.knowledge_keep = parse_knowledge_keep(val);
        } else if (key == "bag_keep") {
            c.bag_keep = parse_bag_keep(val);
        } else if (key == "drive_sites") {
            c.drive_sites = parse_drive_sites(val);
        } else if (key == "equip_character_row") {
            // 캐릭터 행은 u16 이고 0xFFFF 는 "자동" 이다. 그 밖은 자동으로 본다.
            const int v = to_int(val, -1);
            c.equip_character_row = (v < 0 || v > 0xFFFE) ? -1 : v;
        }
    }
    return c;
}

bool save(const std::wstring& path, const Config& c) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out.good()) return false;
    out << "[CDToybox]\n";
    out << "; Win32 virtual-key code\n";
    out << "toggle_key = 0x" << std::hex << c.toggle_key << "\n";
    out << "unload_key = 0x" << std::hex << c.unload_key << "\n";
    out << std::dec;
    out << "show_diagnostics = " << (c.show_diagnostics ? 1 : 0) << "\n";
    out << "; (구) 전 부위 일괄. 부위 목록이 비어 있을 때만 쓴다\n";
    out << "socket_cap = " << c.socket_cap << "\n";
    out << "; 메인 탈것 휠에 드래곤·ATAG 카테고리를 얹는다(1 = 켬).\n";
    out << "; 게임이 휠을 만들기 전에 걸어야 하므로 **시작할 때** 걸린다 -\n";
    out << "; 다 만들어진 뒤에 늘리면 특수 탑승물 호출이 먹통이 된다.\n";
    out << "vehicle_wheel_extend = " << (c.vehicle_wheel_extend ? 1 : 0) << "\n";
    out << "; 소환 진단 훅. 조사할 때만 1. 기본 0 (기능이 아니라 조사용이다).\n";
    out << "summon_diag = " << (c.summon_diag ? 1 : 0) << "\n";
    out << "; 휠 칸 등록 채우기. 얹기가 고른 종만 '올려 둔 칸' 을 채운다.\n";
    out << "wheel_fill = " << (c.wheel_fill ? 1 : 0) << "\n";
    out << "; 얹기가 동반자 칸까지 옮길지. 0 이면 장소 제한만 푼다.\n";
    out << "wheel_swap_slot = " << (c.wheel_swap_slot ? 1 : 0) << "\n";
    out << "; 부위별 소켓 칸 수. `분류:장비타입=칸수` 를 쉼표로 잇는다.\n";
    out << "; 부위는 아이템표의 (+0xA3, +0x42) 쌍이다 - 갑옷 3:5, 망토 3:75,\n";
    out << "; 투구 24:4, 장갑 22:6, 신발 9:7, 귀걸이 15:8, 목걸이 34:9, 반지 49:10.\n";
    out << "; 아이템표는 매 실행 다시 읽히므로 세션마다 이 설정으로 다시 걸린다.\n";
    out << "socket_cap_parts = ";
    for (std::size_t i = 0; i < c.socket_cap_parts.size(); ++i) {
        const auto& p = c.socket_cap_parts[i];
        if (i != 0) out << ",";
        out << p.category << ":" << p.equip_type << "=" << p.want;
    }
    out << "\n";
    out << "; 다시 걸어 줄 지식. `번호:레벨` 을 쉼표로 잇는다.\n";
    out << "; 지식 쓰기는 세이브를 못 넘어서, 이 목록이 있어야 게임을 다시 켤 때\n";
    out << "; 사람이 또 누르지 않는다. 화면의 [잊기] 가 이 줄을 비운다.\n";
    out << "knowledge_keep = ";
    for (std::size_t i = 0; i < c.knowledge_keep.size(); ++i) {
        const auto& k = c.knowledge_keep[i];
        if (i != 0) out << ",";
        out << k.number << ":" << k.level;
    }
    out << "\n";
    out << "; 다시 걸어 줄 가방·보관함 용량. `종류:목표` 를 쉼표로 잇는다.\n";
    out << "; 자동 재적용은 프로세스 메모리에만 살아서, 이 줄이 있어야 게임을 다시\n";
    out << "; 켤 때 사람이 또 누르지 않는다. 화면의 [되돌리기] 가 이 줄을 비운다.\n";
    out << "bag_keep = ";
    for (std::size_t i = 0; i < c.bag_keep.size(); ++i) {
        const auto& b = c.bag_keep[i];
        if (i != 0) out << ",";
        out << b.kind << ":" << b.target;
    }
    out << "\n";
    out << "; 지급을 구동해도 되는 자리(모듈 RVA). 게임이 갱신되면 코드에 박힌\n";
    out << "; 목록이 죽는다 - 그때 로그의 `구동 건너뜀: 확인되지 않은 자리 +<RVA>`\n";
    out << "; 를 여기 적으면 다시 빌드하지 않고 살아난다. 멈춘 자리를 적으면\n";
    out << "; 게임이 멈추니, 지급을 눌렀을 때 반복해 찍히는 자리만 적을 것.\n";
    out << "drive_sites = ";
    for (std::size_t i = 0; i < c.drive_sites.size(); ++i) {
        if (i != 0) out << ",";
        out << "0x" << std::hex << c.drive_sites[i] << std::dec;
    }
    out << "\n";
    out << "; 장비 창의 캐릭터 선택(캐릭터 행). -1 = 자동(착용 조각 최다). 클리프 0, 데미안 3, 웅카 5.\n";
    out << "equip_character_row = " << c.equip_character_row << "\n";
    return out.good();
}

}  // namespace cdtb::config
