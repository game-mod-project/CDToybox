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
    // 실제 조립 길이는 126바이트다. 상한 256 에 여유가 있어야 한다.
    CHECK(c.code.size() == 126);
    // 노출한 오프셋이 실제 자리와 맞아야 한다 - 설치 쪽이 VEH 주소를 여기서 뽑는다.
    CHECK(c.zero_at < c.done_at);
    CHECK(c.done_at < c.code.size());
    CHECK(c.deref_at < c.zero_at);
    CHECK(c.code[c.zero_at] == 0x45);      // xor r9d, r9d
    CHECK(c.code[c.done_at] == 0x58);      // pop rax
    CHECK(c.code[c.deref_at] == 0x48 && c.code[c.deref_at + 1] == 0x8B &&
          c.code[c.deref_at + 2] == 0x40 && c.code[c.deref_at + 3] == 0x68);
}

TEST(nofall_cave_rejects_null_and_zero_addresses) {
    CHECK(!nofall_build_cave(nullptr, kVars, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, 0, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, kVars, 0).ok);
    // 실패는 이유를 남겨야 한다(설치 거부 로그에 쓴다).
    CHECK(nofall_build_cave(nullptr, kVars, kSite).why[0] != '\0');
    // 실패했으면 오프셋도 비어 있어야 한다 - 설치 쪽이 잘못된 VEH 주소를
    // 잡는 일이 없게.
    const NofallCave bad = nofall_build_cave(kOrig, 0, kSite);
    CHECK(bad.code.empty());
    CHECK(bad.zero_at == 0 && bad.done_at == 0 && bad.deref_at == 0);
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
    CHECK(c.code[at + 13] == 0xF0 && c.code[at + 14] == 0x48 &&
          c.code[at + 15] == 0xFF && c.code[at + 16] == 0x00);
                                     // lock inc qword [rax]
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
    CHECK(dx > 0);
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

TEST(nofall_cave_counters_are_locked) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 카운터는 판별식 검증의 유일한 장치다. 전투 코드 39곳이 여러 스레드에서
    // 들어오므로 증가가 유실되면 안 된다 - `inc` 둘 다 lock 이어야 한다.
    int locked = 0, bare = 0;
    for (std::size_t i = 0; i + 4 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xFF && c.code[i + 2] == 0x00) {
            if (i > 0 && c.code[i - 1] == 0xF0) {
                ++locked;
            } else {
                ++bare;
            }
        }
    }
    CHECK(locked == 2);
    CHECK(bare == 0);
}

TEST(nofall_cave_touches_only_rax_flags_and_r9) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 계약: 케이브는 rax·플래그·(zero 갈래의) r9 말고는 아무 레지스터도 바꾸지
    // 않는다. rcx/rdx/r8 은 디스패처가 그대로 쓸 인자다. 원본 5바이트(rbx 저장)
    // 뒤는 꼬리라 세지 않는다.
    const std::size_t body = c.done_at + 2;  // pop rax / popfq 까지
    // 목적지가 rcx(001)/rdx(010)/r8 인 mov·xor·add 류가 없어야 한다. 여기서는
    // 실제로 쓰이는 인코딩만 확인한다: REX.W 있는 89/8B/33/31 의 ModRM 목적지.
    for (std::size_t i = 0; i + 3 <= body; ++i) {
        const std::uint8_t rex = c.code[i];
        if ((rex & 0xF0) != 0x40) continue;
        const std::uint8_t op = c.code[i + 1];
        if (op != 0x89 && op != 0x8B && op != 0x33 && op != 0x31) continue;
        const std::uint8_t modrm = c.code[i + 2];
        if ((modrm & 0xC0) != 0xC0) continue;  // 레지스터 목적지만 본다
        const int dst = (op == 0x8B || op == 0x33) ? ((modrm >> 3) & 7)
                                                   : (modrm & 7);
        const bool wide_dst = (rex & 0x04) != 0;   // REX.R
        const bool wide_rm = (rex & 0x01) != 0;    // REX.B
        const bool ext = (op == 0x8B || op == 0x33) ? wide_dst : wide_rm;
        // rax(000, 확장 아님) 과 r9(001, 확장) 만 허용한다.
        const bool is_rax = (dst == 0 && !ext);
        const bool is_r9 = (dst == 1 && ext);
        CHECK(is_rax || is_r9);
    }
}

TEST(nofall_cave_golden_bytes) {
    // 골든 바이트열. 조립이 조금이라도 달라지면 여기서 잡힌다 - 케이브는 손으로
    // 검증한(capstone 디스어셈블) 산물이라, 의도치 않은 변경은 전부 회귀다.
    // 값을 고칠 때는 반드시 디스어셈블을 다시 돌려 의도한 변경인지 확인할 것.
    static const std::uint8_t kGolden[] = {
        0x9C, 0x50, 0x4D, 0x85, 0xC9, 0x79, 0x62, 0x66, 0x83, 0xFA, 0x00, 0x75,
        0x5C, 0x48, 0xB8, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48,
        0x8B, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x4A, 0x48, 0x39, 0xC1, 0x75, 0x45,
        0x48, 0x8B, 0x44, 0x24, 0x38, 0x48, 0x3D, 0x00, 0x00, 0x01, 0x00, 0x72,
        0x27, 0x48, 0xC1, 0xE8, 0x2F, 0x75, 0x32, 0x48, 0x8B, 0x44, 0x24, 0x38,
        0x48, 0x8B, 0x40, 0x68, 0x48, 0x3D, 0x00, 0x00, 0x01, 0x00, 0x72, 0x10,
        0x48, 0xB8, 0x10, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF0, 0x48,
        0xFF, 0x00, 0xEB, 0x11, 0x45, 0x33, 0xC9, 0x48, 0xB8, 0x08, 0x10, 0x00,
        0x00, 0x00, 0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00, 0x58, 0x9D, 0x48,
        0x89, 0x5C, 0x24, 0x08, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x55, 0x98,
        0x71, 0x41, 0x01, 0x00, 0x00, 0x00,
    };
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() == sizeof(kGolden));
    CHECK(std::memcmp(c.code.data(), kGolden, sizeof(kGolden)) == 0);
}
