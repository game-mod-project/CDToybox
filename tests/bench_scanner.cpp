// 스캐너 처리량 벤치마크.
//
// 게임 실측치는 313MB 실행 섹션에 10바이트 패턴 한 개가 407ms였다.
// 여기서는 같은 성격의 부하를 합성 버퍼로 재현해 최적화 전후를 비교한다.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "mem/scanner.h"

using namespace cdtb::mem;

namespace {

constexpr std::size_t kBufSize = 64ull * 1024 * 1024;   // 64 MB

// 결정적 난수. 실행마다 같은 버퍼를 만들어야 비교가 의미를 가진다.
std::vector<std::uint8_t> make_buffer() {
    std::vector<std::uint8_t> buf(kBufSize);
    std::uint64_t s = 0x2545F4914F6CDD1Dull;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        buf[i] = static_cast<std::uint8_t>(s >> 24);
    }
    return buf;
}

struct Result {
    double ms;
    std::size_t hits;
};

Result time_scan(Range r, const PatternBytes& p) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto hits = find_all(r, p, 1000000);
    const auto t1 = std::chrono::steady_clock::now();
    return {std::chrono::duration<double, std::milli>(t1 - t0).count(),
            hits.size()};
}

void report(const char* label, const Result& r) {
    const double mb = static_cast<double>(kBufSize) / (1024.0 * 1024.0);
    std::printf("  %-32s %8.1f ms  %8.0f MB/s  (%zu hits)\n", label, r.ms,
                mb / (r.ms / 1000.0), r.hits);
}

}  // namespace

int main() {
    std::printf("스캐너 벤치마크 — 버퍼 %zu MB\n\n",
                kBufSize / (1024 * 1024));
    auto buf = make_buffer();

    // 실제 히트가 생기도록 알려진 바이트열을 몇 군데 심는다.
    const std::uint8_t planted[] = {0x48, 0x89, 0x5C, 0x24, 0x08,
                                    0x57, 0x48, 0x83, 0xEC, 0x20};
    const std::size_t spots[] = {1000, kBufSize / 3, kBufSize / 2,
                                 kBufSize - 4096};
    for (std::size_t s : spots) {
        for (std::size_t k = 0; k < sizeof(planted); ++k) buf[s + k] = planted[k];
    }

    const Range r{buf.data(), buf.size()};

    // 1) 게임에서 실제로 쓴 프롤로그 패턴 (선두가 구체 바이트)
    auto p1 = parse_pattern("48 89 5C 24 08 57 48 83 EC 20");
    report("프롤로그 (선두 구체)", time_scan(r, *p1));

    // 2) 선두가 와일드카드 — 앵커를 뒤에서 잡아야 하는 경우
    auto p2 = parse_pattern("?? ?? 5C 24 08 57 48 83 EC 20");
    report("선두 와일드카드 2개", time_scan(r, *p2));

    // 3) 흔한 바이트로 시작 — 후보가 많아 최악에 가깝다
    auto p3 = parse_pattern("00 11 22 33 44 55 66 77");
    report("흔한 선두 바이트", time_scan(r, *p3));

    // 4) 짧은 패턴 — 오버헤드 비중이 커진다
    auto p4 = parse_pattern("48 89 5C");
    report("짧은 패턴 (3바이트)", time_scan(r, *p4));

    std::printf("\n참고: 게임 실행 섹션은 313 MB다.\n");
    return 0;
}
