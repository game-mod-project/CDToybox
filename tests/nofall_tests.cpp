#include <cstdint>
#include <cstring>
#include <vector>

#include "game/nofall_cave.h"
#include "harness.h"

using cdtb::game::kNofallCaveSize;
using cdtb::game::kNofallLetThrough;
using cdtb::game::kNofallOrigSize;
using cdtb::game::kNofallOwner;
using cdtb::game::kNofallVarsSize;
using cdtb::game::kNofallZeroed;
using cdtb::game::nofall_build_cave;
using cdtb::game::NofallCave;

namespace {

// 사이트 첫 명령 `mov qword ptr [rsp+8], rbx` - 정확히 5바이트다.
constexpr std::uint8_t kOrig[kNofallOrigSize] = {0x48, 0x89, 0x5C, 0x24, 0x08};
constexpr std::uintptr_t kVars = 0x0000020000001000ULL;
constexpr std::uintptr_t kSite = 0x0000000141719850ULL;

// b 안에서 pat 이 처음 나오는 자리. 없으면 -1.
std::ptrdiff_t find_bytes(const std::vector<std::uint8_t>& b,
                          std::initializer_list<std::uint8_t> pat) {
    const std::vector<std::uint8_t> p(pat);
    if (p.empty() || b.size() < p.size()) return -1;
    for (std::size_t i = 0; i + p.size() <= b.size(); ++i) {
        if (std::memcmp(b.data() + i, p.data(), p.size()) == 0) {
            return static_cast<std::ptrdiff_t>(i);
        }
    }
    return -1;
}

std::uint64_t qword_at(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint64_t v = 0;
    std::memcpy(&v, b.data() + at, 8);
    return v;
}

// rel8 분기를 따라간 도착지. at 은 opcode 의 색인이다.
std::size_t rel8_target(const std::vector<std::uint8_t>& b, std::size_t at) {
    const auto disp = static_cast<std::int8_t>(b[at + 1]);
    return static_cast<std::size_t>(static_cast<std::ptrdiff_t>(at + 2) + disp);
}

}  // namespace

TEST(nofall_cave_builds_and_fits) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() <= kNofallCaveSize);
    // 실제 조립 길이는 124바이트다. 상한 256 에 여유가 있어야 한다.
    CHECK(c.code.size() == 124);
}

TEST(nofall_cave_rejects_null_and_zero_addresses) {
    CHECK(!nofall_build_cave(nullptr, kVars, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, 0, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, kVars, 0).ok);
    // 실패는 이유를 남겨야 한다(설치 거부 로그에 쓴다).
    CHECK(nofall_build_cave(nullptr, kVars, kSite).why[0] != '\0');
}

TEST(nofall_cave_saves_and_restores_rax_and_flags) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 들머리: pushfq(9C) / push rax(50).
    CHECK(c.code[0] == 0x9C);
    CHECK(c.code[1] == 0x50);
    // 날머리: pop rax(58) / popfq(9D) 가 원본 바이트 **직전**에 있어야 한다.
    const std::ptrdiff_t tail = find_bytes(c.code, {0x58, 0x9D});
    CHECK(tail > 0);
    CHECK(std::memcmp(c.code.data() + tail + 2, kOrig, kNofallOrigSize) == 0);
}

TEST(nofall_cave_ends_with_original_then_absolute_jump_back) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 꼬리: <원본 5바이트> FF 25 00000000 <qword site+5>
    const std::size_t n = c.code.size();
    CHECK(n > kNofallOrigSize + 6 + 8);
    const std::size_t jmp_at = n - 14;
    CHECK(std::memcmp(c.code.data() + jmp_at - kNofallOrigSize, kOrig,
                      kNofallOrigSize) == 0);
    CHECK(c.code[jmp_at] == 0xFF);
    CHECK(c.code[jmp_at + 1] == 0x25);
    CHECK(qword_at(c.code, jmp_at + 6) == kSite + kNofallOrigSize);
}

TEST(nofall_cave_reads_owner_and_counters_from_vars) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // mov rax, imm64 (48 B8) 가 세 번 - owner / letThrough / zeroed.
    std::vector<std::uint64_t> imms;
    for (std::size_t i = 0; i + 10 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xB8) {
            imms.push_back(qword_at(c.code, i + 2));
        }
    }
    CHECK(imms.size() == 3);
    CHECK(imms[0] == kVars + kNofallOwner);
    CHECK(imms[1] == kVars + kNofallLetThrough);
    CHECK(imms[2] == kVars + kNofallZeroed);
    CHECK(kNofallVarsSize >= kNofallLetThrough + 8);
}

TEST(nofall_cave_zeroes_r9_only_on_the_zero_path) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // xor r9d,r9d 는 한 번뿐이어야 한다 - 다른 갈래는 델타를 건드리지 않는다.
    int n = 0;
    std::ptrdiff_t at = -1;
    for (std::size_t i = 0; i + 3 <= c.code.size(); ++i) {
        if (c.code[i] == 0x45 && c.code[i + 1] == 0x33 && c.code[i + 2] == 0xC9) {
            ++n;
            at = static_cast<std::ptrdiff_t>(i);
        }
    }
    CHECK(n == 1);
    // 그 자리가 곧 zero: 라벨이고, 바로 뒤가 취소함 카운터 증가여야 한다.
    CHECK(c.code[at + 3] == 0x48 && c.code[at + 4] == 0xB8);
    CHECK(qword_at(c.code, static_cast<std::size_t>(at) + 5) ==
          kVars + kNofallZeroed);
    CHECK(c.code[at + 13] == 0x48 && c.code[at + 14] == 0xFF &&
          c.code[at + 15] == 0x00);  // inc qword [rax]
}

TEST(nofall_cave_early_exits_jump_past_the_zero_path) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    const std::ptrdiff_t tail = find_bytes(c.code, {0x58, 0x9D});
    CHECK(tail > 0);
    const auto done = static_cast<std::size_t>(tail);
    // 첫 조기 탈출: test r9,r9 (4D 85 C9) 뒤의 jns(79) 가 done 으로 가야 한다.
    CHECK(c.code[2] == 0x4D && c.code[3] == 0x85 && c.code[4] == 0xC9);
    CHECK(c.code[5] == 0x79);
    CHECK(rel8_target(c.code, 5) == done);
    // Health 게이트: cmp dx,0 (66 83 FA 00) 뒤의 jne(75) 도 done 으로.
    const std::ptrdiff_t dx = find_bytes(c.code, {0x66, 0x83, 0xFA, 0x00});
    CHECK(dx == 7);
    CHECK(c.code[dx + 4] == 0x75);
    CHECK(rel8_target(c.code, static_cast<std::size_t>(dx) + 4) == done);
}

TEST(nofall_cave_rel8_branches_stay_in_range) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 조립이 성공했다면 모든 rel8 도착지가 코드 안이어야 한다. 두 라벨(zero/done)
    // 말고 다른 곳으로 튀면 안 된다.
    const std::ptrdiff_t tail = find_bytes(c.code, {0x58, 0x9D});
    const auto done = static_cast<std::size_t>(tail);
    std::ptrdiff_t zero = -1;
    for (std::size_t i = 0; i + 3 <= c.code.size(); ++i) {
        if (c.code[i] == 0x45 && c.code[i + 1] == 0x33 && c.code[i + 2] == 0xC9) {
            zero = static_cast<std::ptrdiff_t>(i);
        }
    }
    CHECK(zero > 0);
    int branches = 0;
    for (std::size_t i = 0; i + 2 <= c.code.size(); ++i) {
        const std::uint8_t op = c.code[i];
        const bool is_jcc = op == 0x79 || op == 0x75 || op == 0x74 || op == 0x72;
        if (!is_jcc && op != 0xEB) continue;
        // imm64 안의 우연한 바이트를 세지 않도록, 도착지가 두 라벨일 때만 센다.
        const std::size_t t = rel8_target(c.code, i);
        if (t != done && t != static_cast<std::size_t>(zero)) continue;
        CHECK(t < c.code.size());
        ++branches;
    }
    // jns / jne(dx) / je(owner) / jne(rcx) / jb / jne(shr) / jb / jmp = 8
    CHECK(branches == 8);
}

TEST(nofall_cave_uses_rsp_0x38_for_source_context) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 케이브가 0x10 을 더 밀었으므로 인자 5 는 [rsp+0x38] 이다. 두 번 읽는다.
    int n = 0;
    for (std::size_t i = 0; i + 5 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0x8B && c.code[i + 2] == 0x44 &&
            c.code[i + 3] == 0x24 && c.code[i + 4] == 0x38) {
            ++n;
        }
    }
    CHECK(n == 2);
    // 가해자는 sourceCtx+0x68 이다: mov rax,[rax+0x68] = 48 8B 40 68.
    CHECK(find_bytes(c.code, {0x48, 0x8B, 0x40, 0x68}) > 0);
}

TEST(nofall_cave_tracks_site_and_vars_addresses) {
    // 주소가 달라지면 케이브도 따라 달라져야 한다(하드코딩이 없다는 확인).
    const NofallCave a = nofall_build_cave(kOrig, kVars, kSite);
    const NofallCave b = nofall_build_cave(kOrig, kVars + 0x1000, kSite + 0x40);
    CHECK(a.ok && b.ok);
    CHECK(a.code.size() == b.code.size());
    CHECK(a.code != b.code);
    const std::size_t n = b.code.size();
    CHECK(qword_at(b.code, n - 8) == kSite + 0x40 + kNofallOrigSize);
}
