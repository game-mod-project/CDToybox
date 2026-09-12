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
using cdtb::game::nofall_build_observe_cave;
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
    // (두 realm 비교가 옛 한 realm 비교보다 2바이트 짧다 - nofall_cave.h 참고.)
    CHECK(c.code.size() == 124);
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
    for (std::size_t i = 1; i + 3 <= c.code.size(); ++i) {
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

TEST(nofall_cave_golden_bytes) {
    // 골든 바이트열. 조립이 조금이라도 달라지면 여기서 잡힌다 - 케이브는 손으로
    // 검증한(capstone 디스어셈블) 산물이라, 의도치 않은 변경은 전부 회귀다.
    // 값을 고칠 때는 반드시 디스어셈블을 다시 돌려 의도한 변경인지 확인할 것.
    static const std::uint8_t kGolden[] = {
        0x9C, 0x50, 0x4D, 0x85, 0xC9, 0x79, 0x60, 0x66, 0x83, 0xFA, 0x00,
        0x75, 0x5A, 0x48, 0xB8, 0x00, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0x48, 0x3B, 0x08, 0x74, 0x06, 0x48, 0x3B, 0x48, 0x08, 0x75,
        0x45, 0x48, 0x8B, 0x44, 0x24, 0x38, 0x48, 0x3D, 0x00, 0x00, 0x01,
        0x00, 0x72, 0x27, 0x48, 0xC1, 0xE8, 0x2F, 0x75, 0x32, 0x48, 0x8B,
        0x44, 0x24, 0x38, 0x48, 0x8B, 0x40, 0x68, 0x48, 0x3D, 0x00, 0x00,
        0x01, 0x00, 0x72, 0x10, 0x48, 0xB8, 0x18, 0x10, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00, 0xEB, 0x11, 0x45, 0x33,
        0xC9, 0x48, 0xB8, 0x10, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0xF0, 0x48, 0xFF, 0x00, 0x58, 0x9D, 0x48, 0x89, 0x5C, 0x24, 0x08,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x55, 0x98, 0x71, 0x41, 0x01,
        0x00, 0x00, 0x00,
    };
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() == sizeof(kGolden));
    CHECK(std::memcmp(c.code.data(), kGolden, sizeof(kGolden)) == 0);
}

TEST(nofall_cave_body_uses_only_the_allowed_encodings) {
    // **계약 시험.** 케이브 본문을 명령 하나씩 앞에서부터 해독해, 아래 목록에 없는
    // 인코딩이 나오면 실패한다. 목록은 "케이브가 내도 되는 명령" 전부이고, 전부
    // rax·플래그·r9 만 건드린다 - 그러므로 이 시험을 통과하면 rcx/rdx/r8/rbx 를
    // 건드리지 않는다는 계약이 따라온다.
    //
    // 바이트 단위로 훑지 않고 **선형 전진 해독**을 하는 이유: 목적지 레지스터만
    // 틀린 오타(`mov rcx,[rsp+0x38]` = 48 8B 4C 24 38)는 ModRM 의 mod 가 같아서
    // 바이트 훑기 필터를 통째로 빠져나간다. 길이를 따라 전진하면 그런 것이
    // "모르는 인코딩" 으로 걸린다.
    struct Op {
        const char* what;
        std::uint8_t pat[6];
        std::size_t pat_len;   // 비교할 앞부분
        std::size_t len;       // 명령 전체 길이
    };
    static const Op kAllowed[] = {
        {"pushfq",                {0x9C}, 1, 1},
        {"push rax",              {0x50}, 1, 1},
        {"pop rax",               {0x58}, 1, 1},
        {"popfq",                 {0x9D}, 1, 1},
        {"test r9,r9",            {0x4D, 0x85, 0xC9}, 3, 3},
        {"cmp dx,0",              {0x66, 0x83, 0xFA, 0x00}, 4, 4},
        {"movabs rax,imm64",      {0x48, 0xB8}, 2, 10},
        {"cmp rcx,[rax]",         {0x48, 0x3B, 0x08}, 3, 3},
        {"cmp rcx,[rax+8]",       {0x48, 0x3B, 0x48, 0x08}, 4, 4},
        {"mov rax,[rsp+0x38]",    {0x48, 0x8B, 0x44, 0x24, 0x38}, 5, 5},
        {"cmp rax,imm32",         {0x48, 0x3D}, 2, 6},
        {"shr rax,47",            {0x48, 0xC1, 0xE8, 0x2F}, 4, 4},
        {"mov rax,[rax+0x68]",    {0x48, 0x8B, 0x40, 0x68}, 4, 4},
        {"lock inc qword [rax]",  {0xF0, 0x48, 0xFF, 0x00}, 4, 4},
        {"xor r9d,r9d",           {0x45, 0x33, 0xC9}, 3, 3},
        {"jns rel8",              {0x79}, 1, 2},
        {"jne rel8",              {0x75}, 1, 2},
        {"je rel8",               {0x74}, 1, 2},
        {"jb rel8",               {0x72}, 1, 2},
        {"jmp rel8",              {0xEB}, 1, 2},
    };

    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    // 본문 = 처음부터 done 의 `pop rax / popfq` 까지. 그 뒤는 원본 바이트와
    // 꼬리 점프(데이터 포함)라 따로 본다.
    const std::size_t body = c.done_at + 2;
    std::size_t at = 0;
    int decoded = 0;
    while (at < body) {
        const Op* hit = nullptr;
        for (const Op& op : kAllowed) {
            if (at + op.len > body) continue;
            if (std::memcmp(c.code.data() + at, op.pat, op.pat_len) == 0) {
                hit = &op;
                break;
            }
        }
        CHECK(hit != nullptr);   // 모르는 인코딩 = 계약 위반
        if (hit == nullptr) break;
        at += hit->len;
        ++decoded;
    }
    // 빈틈없이 딱 떨어져야 한다 - 어긋나면 길이 표가 틀린 것이다.
    CHECK(at == body);
    // pushfq/push rax + 조기탈출 + realm 2개 비교 + sourceCtx 2번 읽기
    // + 카운터 2쌍 + xor r9d + pop rax/popfq = 28개.
    CHECK(decoded == 28);
    // 꼬리는 원본 5바이트 + jmp [rip+0] + qword 다.
    CHECK(std::memcmp(c.code.data() + body, kOrig, kNofallOrigSize) == 0);
}

TEST(nofall_cave_rejects_a_cave_that_would_not_fit) {
    // 상한 가드를 실제로 태운다. 기본 상한(256)으로는 도달할 수 없어, 시험만
    // 작은 상한을 넘긴다 - 그러지 않으면 이 가드는 한 번도 실행되지 않는다.
    const NofallCave tight = nofall_build_cave(kOrig, kVars, kSite, 64);
    CHECK(!tight.ok);
    CHECK(tight.why[0] != '\0');
    CHECK(tight.code.empty());
    // 실패하면 오프셋도 비워야 한다 - 설치 쪽이 잘못된 VEH 주소를 잡지 않게.
    CHECK(tight.zero_at == 0 && tight.done_at == 0 && tight.deref_at == 0);
    // 딱 맞는 상한은 통과해야 한다(경계).
    const NofallCave exact = nofall_build_cave(kOrig, kVars, kSite, 124);
    CHECK(exact.ok);
    CHECK(exact.code.size() == 124);
}

TEST(nofall_cave_every_rel8_displacement_is_in_range) {
    // rel8 사거리 가드는 현재 코드로는 도달 불가라(케이브가 126바이트) 태울 수
    // 없다. 대신 **조립 결과의 모든 변위가 실제로 사거리 안**임을 계산으로 확인해,
    // 누가 케이브를 늘렸을 때 이 시험이 먼저 깨지게 한다.
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    const std::size_t body = c.done_at + 2;
    std::size_t at = 0;
    int branches = 0;
    // 본문을 다시 전진 해독하며 분기만 골라낸다(바이트 훑기가 아니라 명령 단위).
    while (at < body) {
        const std::uint8_t op = c.code[at];
        std::size_t len = 0;
        bool is_branch = false;
        if (op == 0x9C || op == 0x50 || op == 0x58 || op == 0x9D) {
            len = 1;
        } else if (op == 0x79 || op == 0x75 || op == 0x74 || op == 0x72 ||
                   op == 0xEB) {
            len = 2;
            is_branch = true;
        } else if (op == 0x66) {
            len = 4;
        } else if (op == 0x45) {
            len = 3;
        } else if (op == 0xF0) {
            len = 4;
        } else if (op == 0x4D) {
            len = 3;
        } else if (op == 0x48) {
            const std::uint8_t o2 = c.code[at + 1];
            if (o2 == 0xB8) {
                len = 10;
            } else if (o2 == 0x3D) {
                len = 6;
            } else if (o2 == 0xC1) {
                len = 4;
            } else if (o2 == 0x8B && c.code[at + 2] == 0x44) {
                len = 5;
            } else if (o2 == 0x8B && c.code[at + 2] == 0x40) {
                len = 4;
            } else if (o2 == 0x3B && c.code[at + 2] == 0x48) {
                len = 4;  // cmp rcx,[rax+8]
            } else {
                len = 3;  // cmp rcx,[rax]
            }
        }
        CHECK(len > 0);
        if (len == 0) break;
        if (is_branch) {
            const auto disp = static_cast<std::int8_t>(c.code[at + 1]);
            const std::ptrdiff_t target =
                static_cast<std::ptrdiff_t>(at + 2) + disp;
            // 케이브의 분기는 전부 **앞으로만** 뛰고 done 을 넘지 않는다. 라벨은
            // mine/zero/done 셋인데 mine 은 노출하지 않으므로 범위로 검사한다.
            CHECK(target > static_cast<std::ptrdiff_t>(at));
            CHECK(target <= static_cast<std::ptrdiff_t>(c.done_at));
            ++branches;
        }
        at += len;
    }
    CHECK(at == body);
    // jns / jne(dx) / je(realm1) / jne(realm2) / jb / jne(shr) / jb / jmp = 8
    CHECK(branches == 8);
}

TEST(nofall_observe_cave_never_touches_r9) {
    // **진단 케이브의 핵심 계약.** 관찰 모드는 세기만 하고 피해를 건드리지 않는다 -
    // 그래서 켠 채로 평소처럼 놀아도 게임 동작이 달라지지 않는다. r9 를 쓰는
    // 인코딩(`xor r9d,r9d` = 45 33 C9, `mov r9,...`)이 하나도 없어야 한다.
    const NofallCave c = nofall_build_observe_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    for (std::size_t i = 0; i + 3 <= c.code.size(); ++i) {
        const bool xor_r9 = c.code[i] == 0x45 && c.code[i + 1] == 0x33 &&
                            c.code[i + 2] == 0xC9;
        CHECK(!xor_r9);
    }
    // r9 는 읽기만 한다: test r9,r9 (4D 85 C9) 와 mov [rax],r9 (4C 89 08).
    CHECK(find_bytes(c.code, {0x4D, 0x85, 0xC9}) >= 0);
    CHECK(find_bytes(c.code, {0x4C, 0x89, 0x08}) > 0);
    // 게임 포인터를 따라가지 않으므로 폴트 가드가 필요 없다.
    CHECK(c.deref_at == 0);
}

TEST(nofall_observe_cave_golden_bytes) {
    // 골든 바이트열(capstone 으로 따로 디스어셈블해 확인한 138바이트).
    // 분기는 chk_dx(0x61)·done(0x75) 두 곳으로만 가고, 꼬리는 원본 5바이트 +
    // jmp [rip+0] + site+5 다.
    static const std::uint8_t kGolden[] = {
        0x9C, 0x50, 0x4D, 0x85, 0xC9, 0x79, 0x6C, 0x48, 0xB8, 0x10, 0x10,
        0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00, 0x48,
        0xB8, 0x28, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x48, 0x89,
        0x08, 0x48, 0xB8, 0x30, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
        0x48, 0x89, 0x10, 0x48, 0xB8, 0x38, 0x10, 0x00, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x4C, 0x89, 0x08, 0x48, 0xB8, 0x00, 0x10, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x00, 0x48, 0x3B, 0x08, 0x74, 0x06, 0x48, 0x3B,
        0x48, 0x08, 0x75, 0x0E, 0x48, 0xB8, 0x18, 0x10, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0xF0, 0x48, 0xFF, 0x00, 0x66, 0x83, 0xFA, 0x00,
        0x75, 0x0E, 0x48, 0xB8, 0x20, 0x10, 0x00, 0x00, 0x00, 0x02, 0x00,
        0x00, 0xF0, 0x48, 0xFF, 0x00, 0x58, 0x9D, 0x48, 0x89, 0x5C, 0x24,
        0x08, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x55, 0x98, 0x71, 0x41,
        0x01, 0x00, 0x00, 0x00,
    };
    const NofallCave c = nofall_build_observe_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    CHECK(c.code.size() == sizeof(kGolden));
    CHECK(std::memcmp(c.code.data(), kGolden, sizeof(kGolden)) == 0);
    CHECK(c.zero_at == 0x5F);   // chk_dx
    CHECK(c.done_at == 0x73);
    // 꼬리 점프는 site+5 로 돌아간다.
    CHECK(qword_at(c.code, c.code.size() - 8) == kSite + kNofallOrigSize);
}

TEST(nofall_observe_cave_counts_into_the_right_slots) {
    using cdtb::game::kNofallDxZero;
    using cdtb::game::kNofallEvents;
    using cdtb::game::kNofallLastR9;
    using cdtb::game::kNofallLastRcx;
    using cdtb::game::kNofallLastRdx;
    using cdtb::game::kNofallRcxHit;
    const NofallCave c = nofall_build_observe_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    std::vector<std::uint64_t> imms;
    for (std::size_t i = 0; i + 10 <= c.code.size(); ++i) {
        if (c.code[i] == 0x48 && c.code[i + 1] == 0xB8) {
            imms.push_back(qword_at(c.code, i + 2));
        }
    }
    // 피해 / rcx / rdx / r9 / owner / rcx일치 / dx0 = 7개, 이 순서여야 한다.
    CHECK(imms.size() == 7);
    CHECK(imms[0] == kVars + kNofallEvents);
    CHECK(imms[1] == kVars + kNofallLastRcx);
    CHECK(imms[2] == kVars + kNofallLastRdx);
    CHECK(imms[3] == kVars + kNofallLastR9);
    CHECK(imms[4] == kVars + kNofallOwner);
    CHECK(imms[5] == kVars + kNofallRcxHit);
    CHECK(imms[6] == kVars + kNofallDxZero);
    // 칸이 vars 블록 안에 들어가야 한다.
    CHECK(kNofallVarsSize >= kNofallLastR9 + 8);
}

TEST(nofall_cave_compares_both_realm_roots) {
    // 플레이어는 클라·서버 두 realm 으로 존재하고 디스패처는 서버 root 를 rcx 로
    // 넘긴다(2026-09-12 실측). 어느 쪽이 잡힐지 보장되지 않으므로 케이브가 나란한
    // 두 칸을 **둘 다** 맞춰 봐야 한다. 한 칸만 보면 훅이 조용히 안 물린다.
    const NofallCave c = nofall_build_cave(kOrig, kVars, kSite);
    CHECK(c.ok);
    const std::ptrdiff_t first = find_bytes(c.code, {0x48, 0x3B, 0x08});
    const std::ptrdiff_t second = find_bytes(c.code, {0x48, 0x3B, 0x48, 0x08});
    CHECK(first > 0);
    CHECK(second > first);
    // 두 비교는 vars 의 owner 칸을 가리키는 movabs 바로 뒤에 붙어 있어야 한다.
    CHECK(qword_at(c.code, static_cast<std::size_t>(first) - 8) ==
          kVars + kNofallOwner);
    // 두 칸이 나란해야 [rax] / [rax+8] 로 읽을 수 있다.
    CHECK(cdtb::game::kNofallOwner2 == cdtb::game::kNofallOwner + 8);
    // 첫 비교가 맞으면 두 번째를 건너뛰고 본문으로 간다(je), 아니면 두 번째를 본다.
    CHECK(c.code[first + 3] == 0x74);
    CHECK(c.code[second + 4] == 0x75);
    // 관찰 케이브도 같은 판정을 세야 진단이 적용과 일치한다.
    const NofallCave o = nofall_build_observe_cave(kOrig, kVars, kSite);
    CHECK(o.ok);
    CHECK(find_bytes(o.code, {0x48, 0x3B, 0x08}) > 0);
    CHECK(find_bytes(o.code, {0x48, 0x3B, 0x48, 0x08}) > 0);
}
