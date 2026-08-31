// CDToybox 외부 분석 도구.
//
// 게임에 붙어 메모리를 읽고 RTTI로 객체를 찾는다. 인게임 UI가 아니라
// 명령줄 도구인 이유는, 이 작업의 조작자가 사람이 아니라 에이전트이기
// 때문이다. 반복 실행할 수 있어야 값을 찾는 일을 자율적으로 한다.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "remote.h"
#include "rtti.h"

using namespace cdtb::probe;

namespace {

void usage() {
    std::printf(
        "사용법: cdtb_probe <명령> [인자]\n"
        "\n"
        "  info                        프로세스와 모듈 정보\n"
        "  types <부분문자열> [최대]   RTTI 클래스 검색\n"
        "  vtable <클래스명>           클래스의 vtable 주소\n"
        "  instances <클래스명> [최대] 살아있는 인스턴스 주소\n"
        "  dump <주소> [바이트]        16진 + float 덤프\n"
        "  floats <주소> [개수]        float 격자 덤프\n"
        "  regions                     메모리 영역 요약\n"
        "\n"
        "주소는 16진(0x 접두 선택)으로 준다.\n");
}

std::uintptr_t parse_addr(const char* s) {
    return static_cast<std::uintptr_t>(std::strtoull(s, nullptr, 16));
}

void cmd_info(const Remote& r) {
    std::printf("PID          %lu\n", r.pid());
    std::printf("모듈 베이스  0x%llX\n",
                static_cast<unsigned long long>(r.module_base()));
    std::printf("모듈 크기    %.1f MB\n",
                static_cast<double>(r.module_size()) / (1024.0 * 1024.0));
}

void cmd_regions(const Remote& r) {
    const auto regs = r.regions();
    std::size_t total = 0, wr = 0, heap = 0;
    for (const auto& x : regs) {
        total += x.size;
        if (x.writable) wr += x.size;
        if (x.writable && !x.is_image) heap += x.size;
    }
    std::printf("영역 %zu개\n", regs.size());
    std::printf("  전체        %.1f MB\n", total / 1048576.0);
    std::printf("  쓰기가능    %.1f MB\n", wr / 1048576.0);
    std::printf("  힙(비이미지) %.1f MB\n", heap / 1048576.0);
}

void cmd_types(Rtti& rt, const char* needle, std::size_t max) {
    const auto found = rt.find_types(needle, max);
    std::printf("일치 %zu개\n", found.size());
    for (const auto& t : found) {
        std::printf("  0x%llX  %s\n",
                    static_cast<unsigned long long>(t.descriptor),
                    t.name.c_str());
    }
}

// 정확한 이름 하나를 고른다. 여러 개면 첫 번째를 쓰고 경고한다.
bool resolve_one(Rtti& rt, const char* name, Rtti::TypeInfo* out) {
    auto found = rt.find_types(name, 64);
    if (found.empty()) {
        std::printf("클래스를 찾지 못했습니다: %s\n", name);
        return false;
    }
    // 완전 일치를 우선한다.
    for (const auto& t : found) {
        if (t.name == name) { *out = t; return true; }
    }
    if (found.size() > 1) {
        std::printf("부분 일치 %zu개 - 첫 번째를 씁니다\n", found.size());
        for (std::size_t i = 0; i < found.size() && i < 8; ++i) {
            std::printf("    %s\n", found[i].name.c_str());
        }
    }
    *out = found[0];
    return true;
}

void cmd_vtable(Rtti& rt, const char* name) {
    Rtti::TypeInfo ti;
    if (!resolve_one(rt, name, &ti)) return;
    std::printf("%s\n  TypeDescriptor 0x%llX\n", ti.name.c_str(),
                static_cast<unsigned long long>(ti.descriptor));

    const auto vts = rt.vtables_for(ti.descriptor);
    if (vts.empty()) {
        std::printf("  vtable을 찾지 못했습니다 (추상 클래스이거나 "
                    "COL 레이아웃이 다릅니다)\n");
        return;
    }
    for (const auto v : vts) {
        std::printf("  vtable 0x%llX\n", static_cast<unsigned long long>(v));
    }
}

void cmd_instances(Rtti& rt, const Remote& r, const char* name,
                   std::size_t max) {
    Rtti::TypeInfo ti;
    if (!resolve_one(rt, name, &ti)) return;
    const auto vts = rt.vtables_for(ti.descriptor);
    if (vts.empty()) {
        std::printf("vtable이 없어 인스턴스를 찾을 수 없습니다\n");
        return;
    }
    std::printf("%s  vtable %zu개\n", ti.name.c_str(), vts.size());
    for (const auto v : vts) {
        const auto inst = rt.instances_of(v, max);
        std::printf("  vtable 0x%llX -> 인스턴스 %zu개\n",
                    static_cast<unsigned long long>(v), inst.size());
        for (std::size_t i = 0; i < inst.size() && i < max; ++i) {
            std::printf("    0x%llX\n",
                        static_cast<unsigned long long>(inst[i]));
        }
    }
    (void)r;
}

void cmd_dump(const Remote& r, std::uintptr_t addr, std::size_t n) {
    std::vector<std::uint8_t> buf(n);
    if (!r.read(addr, buf.data(), n)) {
        std::printf("읽기 실패: 0x%llX (%zu 바이트)\n",
                    static_cast<unsigned long long>(addr), n);
        return;
    }
    for (std::size_t i = 0; i < n; i += 16) {
        std::printf("0x%llX  ", static_cast<unsigned long long>(addr + i));
        for (std::size_t k = 0; k < 16; ++k) {
            if (i + k < n) std::printf("%02X ", buf[i + k]);
            else std::printf("   ");
        }
        std::printf(" |");
        for (std::size_t k = 0; k < 16 && i + k < n; ++k) {
            const auto c = buf[i + k];
            std::printf("%c", (c >= 32 && c < 127) ? static_cast<char>(c) : '.');
        }
        std::printf("|\n");
    }
}

void cmd_floats(const Remote& r, std::uintptr_t addr, std::size_t count) {
    std::vector<float> buf(count);
    if (!r.read(addr, buf.data(), count * sizeof(float))) {
        std::printf("읽기 실패: 0x%llX\n",
                    static_cast<unsigned long long>(addr));
        return;
    }
    for (std::size_t i = 0; i < count; i += 4) {
        std::printf("+0x%03zX  ", i * 4);
        for (std::size_t k = 0; k < 4 && i + k < count; ++k) {
            std::printf("%14.4f", buf[i + k]);
        }
        std::printf("\n");
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    Remote r;
    if (!r.attach(L"CrimsonDesert.exe")) {
        std::printf("붙기 실패: %s\n", r.last_error().c_str());
        return 2;
    }

    const std::string cmd = argv[1];

    if (cmd == "info") { cmd_info(r); return 0; }
    if (cmd == "regions") { cmd_regions(r); return 0; }

    if (cmd == "dump") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 128;
        cmd_dump(r, parse_addr(argv[2]), n);
        return 0;
    }
    if (cmd == "floats") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t n = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                         : 32;
        cmd_floats(r, parse_addr(argv[2]), n);
        return 0;
    }

    // 아래 명령들은 모듈 이미지가 필요하다.
    Rtti rt(r);
    std::printf("모듈 이미지 로드 중 (%.1f MB)...\n",
                static_cast<double>(r.module_size()) / 1048576.0);
    if (!rt.load_image()) {
        std::printf("이미지 로드 실패\n");
        return 3;
    }

    if (cmd == "types") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 40;
        cmd_types(rt, argv[2], max);
        return 0;
    }
    if (cmd == "vtable") {
        if (argc < 3) { usage(); return 1; }
        cmd_vtable(rt, argv[2]);
        return 0;
    }
    if (cmd == "instances") {
        if (argc < 3) { usage(); return 1; }
        const std::size_t max = (argc > 3) ? std::strtoull(argv[3], nullptr, 10)
                                           : 20;
        cmd_instances(rt, r, argv[2], max);
        return 0;
    }

    usage();
    return 1;
}
