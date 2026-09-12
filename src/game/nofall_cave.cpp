#include "game/nofall_cave.h"

#include <utility>

namespace cdtb::game {
namespace {

// 나중에 채울 변위 자리. rel32 는 4바이트, rel8 은 1바이트다.
struct Fixup {
    std::size_t at;
};

}  // namespace

NofallCave nofall_build_cave(const std::uint8_t* orig, std::uintptr_t vars,
                             std::uintptr_t site, std::size_t cave_max) {
    NofallCave out;
    if (orig == nullptr) {
        out.why = "원본 바이트가 없다";
        return out;
    }
    if (vars == 0 || site == 0) {
        out.why = "vars 나 site 주소가 0 이다";
        return out;
    }

    std::vector<std::uint8_t> b;
    std::vector<Fixup> to_done, to_zero, to_low, to_bad, to_hit, to_dx;

    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    auto addq = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    // 조건부 rel32(`0F 8x`) 또는 무조건 rel32(`E9`). op2 == 0 이면 jmp 다.
    auto j32 = [&](std::uint8_t op2, std::vector<Fixup>* where) {
        if (op2 == 0) {
            b.push_back(0xE9);
        } else {
            b.push_back(0x0F);
            b.push_back(op2);
        }
        for (int i = 0; i < 4; ++i) b.push_back(0);
        where->push_back(Fixup{b.size() - 4});
    };
    auto j8 = [&](std::uint8_t op, std::vector<Fixup>* where) {
        b.push_back(op);
        b.push_back(0);
        where->push_back(Fixup{b.size() - 1});
    };
    // mov [절대주소], rax  (48 A3 imm64) - 두 번째 레지스터가 필요 없다.
    auto store_abs = [&](std::size_t off) {
        add({0x48, 0xA3});
        addq(static_cast<std::uint64_t>(vars + off));
    };
    // movabs rax, &vars+off ; lock inc qword [rax]
    auto bump = [&](std::size_t off) {
        add({0x48, 0xB8});
        addq(static_cast<std::uint64_t>(vars + off));
        add({0xF0, 0x48, 0xFF, 0x00});
    };

    add({0x9C, 0x50});              // pushfq / push rax  (rsp 가 0x10 밀린다)
    add({0x4D, 0x85, 0xC9});        // test r9, r9
    j32(0x89, &to_done);            // jns done   델타 >= 0 -> 데미지가 아니다
    add({0x66, 0x83, 0xFA, 0x00});  // cmp dx, 0
    j32(0x85, &to_done);            // jne done   Health 가 아니다

    // rcx 가 두 realm root 중 하나인가. 꺼져 있으면 두 칸이 0 이고 rcx 는 0 일
    // 수 없으므로 어느 쪽도 맞지 않아 빠진다.
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallOwner));
    add({0x48, 0x3B, 0x08});        // cmp rcx, [rax]      realm 1
    j8(0x74, &to_hit);              // je  mine
    add({0x48, 0x3B, 0x48, 0x08});  // cmp rcx, [rax+8]    realm 2
    j32(0x85, &to_done);            // jne done   내가 맞은 게 아니다
    const std::size_t mine_at = b.size();

    // 여기부터는 **내 생명 피해**다. 출처의 정체를 남긴다(밖에서 눈으로 보려고).
    add({0x4C, 0x89, 0xC8});        // mov rax, r9
    store_abs(kNofallLastDelta);
    add({0x48, 0x8B, 0x44, 0x24, 0x38});   // mov rax, [rsp+0x38]  sourceCtx
    store_abs(kNofallLastSrc);

    // 규칙이 "무조건 취소" 면 출처를 안 보고 바로 지운다(시험용).
    add({0x48, 0xA1});
    addq(static_cast<std::uint64_t>(vars + kNofallRule));   // mov rax, [abs rule]
    add({0x48, 0x83, 0xF8, kRuleAlways});   // cmp rax, 3
    j32(0x84, &to_zero);                    // je  zero   무조건(시험용)

    add({0x48, 0x8B, 0x44, 0x24, 0x38});    // mov rax, [rsp+0x38]
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});   // cmp rax, 0x10000
    j32(0x82, &to_low);                     // jb  src_low   출처가 아예 없다
    add({0x48, 0xC1, 0xE8, 0x2F});          // shr rax, 47
    j32(0x85, &to_bad);                     // jne src_bad   말이 안 되는 포인터

    add({0x48, 0x8B, 0x44, 0x24, 0x38});    // mov rax, [rsp+0x38]
    out.vt_at = b.size();
    add({0x48, 0x8B, 0x00});                // mov rax, [rax]   vtable
    store_abs(kNofallLastVt);
    add({0x48, 0x8B, 0x44, 0x24, 0x38});    // mov rax, [rsp+0x38]
    out.deref_at = b.size();
    add({0x48, 0x8B, 0x40, 0x68});          // mov rax, [rax+0x68]   가해자
    store_abs(kNofallLastAtk);

    // 규칙별로 가른다. 여기까지 왔다는 것은 출처가 **있다**는 뜻이므로 규칙 2
    // (출처 없음)는 통과다.
    std::vector<Fixup> to_pass;
    add({0x48, 0xA1});
    addq(static_cast<std::uint64_t>(vars + kNofallRule));
    add({0x48, 0x85, 0xC0});                // test rax, rax
    std::vector<Fixup> to_rule1;
    j32(0x85, &to_rule1);                   // jne rule1   규칙 0 이 아니다

    // 규칙 0: **출처가 나 자신인가.** 낙하·환경 피해는 내 char 가 출처다
    // (2026-09-12 실측). 적의 타격은 그 적의 char 라 여기서 갈린다.
    // `mov rax,[abs]` + `cmp rax,[rsp+0x38]` 이라 두 번째 레지스터가 필요 없다.
    add({0x48, 0xA1});
    addq(static_cast<std::uint64_t>(vars + kNofallSelf));
    add({0x48, 0x3B, 0x44, 0x24, 0x38});    // cmp rax, [rsp+0x38]
    j32(0x84, &to_zero);                    // je  zero
    add({0x48, 0xA1});
    addq(static_cast<std::uint64_t>(vars + kNofallSelf2));
    add({0x48, 0x3B, 0x44, 0x24, 0x38});    // cmp rax, [rsp+0x38]
    j32(0x84, &to_zero);                    // je  zero
    j32(0, &to_pass);                       // jmp pass   남이 때렸다

    const std::size_t rule1_at = b.size();  // rule1:
    add({0x48, 0x83, 0xF8, kRuleNoAttacker});   // cmp rax, 1
    j32(0x85, &to_pass);                    // jne pass   규칙 2 는 통과
    // 규칙 1(CT 원안): 방금 적어 둔 가해자 값을 다시 읽어 판단한다.
    add({0x48, 0xA1});
    addq(static_cast<std::uint64_t>(vars + kNofallLastAtk));
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});   // cmp rax, 0x10000
    j32(0x82, &to_zero);                    // jb  zero   가해자가 없다
    const std::size_t pass_at = b.size();   // pass:
    bump(kNofallLetThrough);
    j32(0, &to_done);                       // jmp done

    const std::size_t low_at = b.size();    // src_low: 출처가 아예 없다
    bump(kNofallSrcLow);
    j32(0, &to_zero);                       // jmp zero   규칙과 무관하게 취소

    const std::size_t bad_at = b.size();    // src_bad: 판정 불가 -> 통과
    bump(kNofallSrcBad);
    j32(0, &to_done);

    const std::size_t zero_at = b.size();   // zero:
    add({0x45, 0x33, 0xC9});                // xor r9d, r9d
    bump(kNofallZeroed);

    out.done_at = b.size();                 // done:
    add({0x58, 0x9D});                      // pop rax / popfq
    for (std::size_t i = 0; i < kNofallOrigSize; ++i) b.push_back(orig[i]);
    add({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});   // jmp [rip+0]
    addq(static_cast<std::uint64_t>(site + kNofallOrigSize));

    auto patch32 = [&](const std::vector<Fixup>& fs, std::size_t target) {
        for (const auto& f : fs) {
            const std::int64_t rel = static_cast<std::int64_t>(target) -
                                     static_cast<std::int64_t>(f.at + 4);
            const auto v = static_cast<std::uint32_t>(rel);
            for (int i = 0; i < 4; ++i) {
                b[f.at + i] = static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF);
            }
        }
    };
    auto patch8 = [&](const std::vector<Fixup>& fs, std::size_t target) {
        for (const auto& f : fs) {
            const std::ptrdiff_t rel = static_cast<std::ptrdiff_t>(target) -
                                       static_cast<std::ptrdiff_t>(f.at + 1);
            if (rel < -128 || rel > 127) return false;
            b[f.at] = static_cast<std::uint8_t>(static_cast<std::int8_t>(rel));
        }
        return true;
    };
    patch32(to_done, out.done_at);
    patch32(to_zero, zero_at);
    patch32(to_low, low_at);
    patch32(to_bad, bad_at);
    patch32(to_pass, pass_at);
    patch32(to_rule1, rule1_at);
    if (!patch8(to_hit, mine_at) || !patch8(to_dx, mine_at)) {
        out.why = "rel8 분기가 사거리를 벗어난다";
        out.done_at = out.vt_at = out.deref_at = 0;
        return out;
    }
    if (b.size() > cave_max) {
        out.why = "케이브가 상한을 넘는다";
        out.done_at = out.vt_at = out.deref_at = 0;
        return out;
    }

    out.code = std::move(b);
    out.ok = true;
    return out;
}

}  // namespace cdtb::game
