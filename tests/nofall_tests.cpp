#include <cstdint>
#include <cstring>
#include <vector>

#include "game/nofall_cave.h"
#include "harness.h"

using cdtb::game::kNofallCaveSize;
using cdtb::game::kNofallLastAtk;
using cdtb::game::kNofallLastDelta;
using cdtb::game::kNofallLastSrc;
using cdtb::game::kNofallLastVt;
using cdtb::game::kNofallLetThrough;
using cdtb::game::kNofallOrigSize;
using cdtb::game::kNofallOwner;
using cdtb::game::kNofallOwner2;
using cdtb::game::kNofallRule;
using cdtb::game::kNofallSrcBad;
using cdtb::game::kNofallSrcLow;
using cdtb::game::kNofallVarsSize;
using cdtb::game::kNofallZeroed;
using cdtb::game::nofall_build_cave;
using cdtb::game::NofallCave;

namespace {

// 사이트 첫 명령 `mov qword ptr [rsp+8], rbx` - 정확히 5바이트다.
constexpr std::uint8_t kOrig[kNofallOrigSize] = {0x48, 0x89, 0x5C, 0x24, 0x08};
constexpr std::uintptr_t kVars = 0x0000020000001000ULL;
constexpr std::uintptr_t kSite = 0x0000000141719850ULL;

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

int count_bytes(const std::vector<std::uint8_t>& b,
                std::initializer_list<std::uint8_t> pat) {
    const std::vector<std::uint8_t> p(pat);
    int n = 0;
    for (std::size_t i = 0; i + p.size() <= b.size(); ++i) {
        if (std::memcmp(b.data() + i, p.data(), p.size()) == 0) ++n;
    }
    return n;
}

std::uint64_t qword_at(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint64_t v = 0;
    std::memcpy(&v, b.data() + at, 8);
    return v;
}

}  // namespace

TEST(nofall_cave_builds_and_fits) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() <= kNofallCaveSize);
    // 노출한 오프셋이 실제 자리와 맞아야 한다 - 설치 쪽이 VEH 주소를 여기서 뽑는다.
    CHECK(c.vt_at > 0);
    CHECK(c.deref_at > c.vt_at);
    CHECK(c.done_at > c.deref_at);
    CHECK(c.done_at < c.code.size());
    CHECK(c.code[c.vt_at] == 0x48 && c.code[c.vt_at + 1] == 0x8B &&
          c.code[c.vt_at + 2] == 0x00);            // mov rax,[rax]
    CHECK(c.code[c.deref_at] == 0x48 && c.code[c.deref_at + 1] == 0x8B &&
          c.code[c.deref_at + 2] == 0x40 && c.code[c.deref_at + 3] == 0x68);
    CHECK(c.code[c.done_at] == 0x58);              // pop rax
    CHECK(c.code[c.done_at + 1] == 0x9D);          // popfq
}

TEST(nofall_cave_rejects_null_and_zero_addresses) {
    CHECK(!nofall_build_cave(nullptr, kVars, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, 0, kSite).ok);
    CHECK(!nofall_build_cave(kOrig, kVars, 0).ok);
    CHECK(nofall_build_cave(nullptr, kVars, kSite).why[0] != '\0');
    const NofallCave bad = nofall_build_cave(kOrig, 0, kSite);
    CHECK(bad.code.empty());
    CHECK(bad.done_at == 0 && bad.vt_at == 0 && bad.deref_at == 0);
}

TEST(nofall_cave_rejects_a_cave_that_would_not_fit) {
    // 상한 가드를 실제로 태운다. 기본 상한으로는 도달할 수 없어 시험만 작게 준다.
    const NofallCave tight = nofall_build_cave(kOrig, kVars, kSite, 64);
    CHECK(!tight.ok);
    CHECK(tight.why[0] != '\0');
    CHECK(tight.code.empty());
    CHECK(tight.done_at == 0 && tight.vt_at == 0 && tight.deref_at == 0);
    const NofallCave big = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(big.ok);
    const NofallCave exact =
        nofall_build_cave(kOrig, kVars, kSite, big.code.size());
    CHECK(exact.ok);
}

TEST(nofall_cave_saves_and_restores_rax_and_flags) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code[0] == 0x9C);   // pushfq
    CHECK(c.code[1] == 0x50);   // push rax
    // 날머리는 pop rax / popfq 가 원본 바이트 **직전**에 한 번만.
    CHECK(count_bytes(c.code, {0x58, 0x9D}) == 1);
    CHECK(std::memcmp(c.code.data() + c.done_at + 2, kOrig, kNofallOrigSize) == 0);
}

TEST(nofall_cave_ends_with_original_then_absolute_jump_back) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    const std::size_t jmp_at = c.code.size() - 14;
    CHECK(std::memcmp(c.code.data() + jmp_at - kNofallOrigSize, kOrig,
                      kNofallOrigSize) == 0);
    CHECK(c.code[jmp_at] == 0xFF);
    CHECK(c.code[jmp_at + 1] == 0x25);
    CHECK(qword_at(c.code, jmp_at + 6) == kSite + kNofallOrigSize);
}

TEST(nofall_cave_zeroes_r9_exactly_once) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // xor r9d,r9d 는 한 번뿐이어야 한다 - 다른 갈래는 델타를 건드리지 않는다.
    CHECK(count_bytes(c.code, {0x45, 0x33, 0xC9}) == 1);
    const std::ptrdiff_t at = find_bytes(c.code, {0x45, 0x33, 0xC9});
    // 바로 뒤가 취소함 카운터 증가여야 한다.
    CHECK(c.code[at + 3] == 0x48 && c.code[at + 4] == 0xB8);
    CHECK(qword_at(c.code, static_cast<std::size_t>(at) + 5) ==
          kVars + kNofallZeroed);
    CHECK(c.code[at + 13] == 0xF0);   // lock
}

TEST(nofall_cave_compares_both_realm_roots) {
    // 같은 캐릭터가 클라·서버 두 realm 으로 존재하고 디스패처는 서버 root 를
    // rcx 로 넘긴다(2026-09-12 실측). 한 칸만 보면 훅이 조용히 안 물린다.
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    const std::ptrdiff_t first = find_bytes(c.code, {0x48, 0x3B, 0x08});
    const std::ptrdiff_t second = find_bytes(c.code, {0x48, 0x3B, 0x48, 0x08});
    CHECK(first > 0);
    CHECK(second > first);
    CHECK(qword_at(c.code, static_cast<std::size_t>(first) - 8) ==
          kVars + kNofallOwner);
    // 두 칸이 나란해야 [rax] / [rax+8] 로 읽을 수 있다.
    CHECK(kNofallOwner2 == kNofallOwner + 8);
    CHECK(c.code[first + 3] == 0x74);   // je  mine
}

TEST(nofall_cave_records_the_source_identity) {
    // 낙하와 타격이 어떻게 다른지 밖에서 보려면 출처의 정체가 남아야 한다.
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // mov [절대주소], rax (48 A3) 로 남긴다. 두 번째 레지스터를 쓰지 않으려는
    // 것이다 - rcx/rdx/r8 은 디스패처가 그대로 쓸 인자다.
    std::vector<std::uint64_t> stores;
    for (std::size_t i = 0; i + 10 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xA3) {
            stores.push_back(qword_at(c.code, i + 2));
        }
    }
    CHECK(stores.size() == 4);
    CHECK(stores[0] == kVars + kNofallLastDelta);
    CHECK(stores[1] == kVars + kNofallLastSrc);
    CHECK(stores[2] == kVars + kNofallLastVt);
    CHECK(stores[3] == kVars + kNofallLastAtk);
    CHECK(kNofallVarsSize >= kNofallSrcBad + 8);
}

TEST(nofall_cave_counts_into_the_right_slots) {
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // movabs rax, <vars+off> 뒤에 lock inc 가 오는 자리만 센다(카운터).
    std::vector<std::uint64_t> counters;
    for (std::size_t i = 0; i + 14 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xB8 &&
            c.code[i + 10] == 0xF0 && c.code[i + 11] == 0x48 &&
            c.code[i + 12] == 0xFF && c.code[i + 13] == 0x00) {
            counters.push_back(qword_at(c.code, i + 2));
        }
    }
    CHECK(counters.size() == 4);
    CHECK(counters[0] == kVars + kNofallLetThrough);
    CHECK(counters[1] == kVars + kNofallSrcLow);
    CHECK(counters[2] == kVars + kNofallSrcBad);
    CHECK(counters[3] == kVars + kNofallZeroed);
}

TEST(nofall_cave_reads_the_rule_at_runtime) {
    // 규칙은 vars 에서 읽어야 한다 - 상수로 굳으면 게임을 껐다 켜야 바뀐다.
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // mov rax, [절대주소] (48 A1) 로 규칙을 읽는다. 두 갈래에서 본다.
    int reads = 0;
    for (std::size_t i = 0; i + 10 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xA1 &&
            qword_at(c.code, i + 2) == kVars + kNofallRule) {
            ++reads;
        }
    }
    CHECK(reads == 2);
}

TEST(nofall_cave_tracks_site_and_vars_addresses) {
    // 주소가 달라지면 케이브도 따라 달라져야 한다(하드코딩이 없다는 확인).
    const NofallCave a = nofall_build_cave(kOrig, kVars, kSite);
    const NofallCave b = nofall_build_cave(kOrig, kVars + 0x1000, kSite + 0x40);
    CHECK(a.ok && b.ok);
    CHECK(a.code.size() == b.code.size());
    CHECK(a.code != b.code);
    CHECK(qword_at(b.code, b.code.size() - 8) == kSite + 0x40 + kNofallOrigSize);
}

TEST(nofall_cave_golden_bytes) {
    // 골든 바이트열. 조립이 조금이라도 달라지면 여기서 잡힌다 - 케이브는 손으로
    // 검증한(capstone 디스어셈블) 산물이라 의도치 않은 변경은 전부 회귀다.
    // 값을 고칠 때는 **반드시 디스어셈블을 다시 돌려** 의도한 변경인지 확인할 것.
    // 확인한 배치: mine=0x2E vt=0x7E atk=0x90 pass=0xBD low=0xD0 bad=0xE3
    //              rule1=0xE0 pass=0x100 zero=0x139 done=0x14A, 꼬리 = site+5.
    static const std::uint8_t kGolden[] = {
        0x9C, 0x50, 0x4D, 0x85, 0xC9, 0x0F, 0x89, 0x3F, 0x01, 0x00, 0x00,
        0x66, 0x83, 0xFA, 0x00, 0x0F, 0x85, 0x35, 0x01, 0x00, 0x00, 0x48,
        0xB8, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0x3B,
        0x08, 0x74, 0x0A, 0x48, 0x3B, 0x48, 0x08, 0x0F, 0x85, 0x1C, 0x01,
        0x00, 0x00, 0x4C, 0x89, 0xC8, 0x48, 0xA3, 0x40, 0x10, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x44, 0x24, 0x38, 0x48, 0xA3,
        0x28, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0xA1, 0x20,
        0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0x83, 0xF8, 0x03,
        0x0F, 0x84, 0xDB, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x44, 0x24, 0x38,
        0x48, 0x3D, 0x00, 0x00, 0x01, 0x00, 0x0F, 0x82, 0xA4, 0x00, 0x00,
        0x00, 0x48, 0xC1, 0xE8, 0x2F, 0x0F, 0x85, 0xAD, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x44, 0x24, 0x38, 0x48, 0x8B, 0x00, 0x48, 0xA3, 0x30,
        0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x44, 0x24,
        0x38, 0x48, 0x8B, 0x40, 0x68, 0x48, 0xA3, 0x38, 0x10, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x48, 0xA1, 0x20, 0x10, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x85, 0x2F, 0x00, 0x00,
        0x00, 0x48, 0xA1, 0x58, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x48, 0x3B, 0x44, 0x24, 0x38, 0x0F, 0x84, 0x73, 0x00, 0x00, 0x00,
        0x48, 0xA1, 0x60, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48,
        0x3B, 0x44, 0x24, 0x38, 0x0F, 0x84, 0x5E, 0x00, 0x00, 0x00, 0xE9,
        0x20, 0x00, 0x00, 0x00, 0x48, 0x83, 0xF8, 0x01, 0x0F, 0x85, 0x16,
        0x00, 0x00, 0x00, 0x48, 0xA1, 0x38, 0x10, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x48, 0x3D, 0x00, 0x00, 0x01, 0x00, 0x0F, 0x82, 0x39,
        0x00, 0x00, 0x00, 0x48, 0xB8, 0x18, 0x10, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00, 0xE9, 0x37, 0x00, 0x00, 0x00,
        0x48, 0xB8, 0x48, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF0,
        0x48, 0xFF, 0x00, 0xE9, 0x13, 0x00, 0x00, 0x00, 0x48, 0xB8, 0x50,
        0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00,
        0xE9, 0x11, 0x00, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0xB8, 0x10,
        0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00,
        0x58, 0x9D, 0x48, 0x89, 0x5C, 0x24, 0x08, 0xFF, 0x25, 0x00, 0x00,
        0x00, 0x00, 0x55, 0x98, 0x71, 0x41, 0x01, 0x00, 0x00, 0x00,
    };
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() == sizeof(kGolden));
    CHECK(std::memcmp(c.code.data(), kGolden, sizeof(kGolden)) == 0);
    CHECK(c.vt_at == 0x7E);
    CHECK(c.deref_at == 0x90);
    CHECK(c.done_at == 0x14A);
}
