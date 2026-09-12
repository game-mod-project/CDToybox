#include "game/nofall_cave.h"

#include <utility>

namespace cdtb::game {
namespace {

// 앞으로 뛰는 rel8 분기 하나. disp 는 변위 바이트의 색인이다(나중에 채운다).
struct Fixup {
    std::size_t disp;
};

}  // namespace

NofallCave nofall_build_cave(const std::uint8_t* orig, std::uintptr_t vars,
                             std::uintptr_t site) {
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
    std::vector<Fixup> to_done;
    std::vector<Fixup> to_zero;

    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    auto addq = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    // rel8 분기(조건부 Jcc 든 EB 든 모양이 같다). 변위는 0 으로 두고 뒤에 채운다.
    auto jrel8 = [&](std::uint8_t op, std::vector<Fixup>* where) {
        b.push_back(op);
        b.push_back(0);
        where->push_back(Fixup{b.size() - 1});
    };

    add({0x9C, 0x50});              // pushfq / push rax   (rsp 가 0x10 밀린다)
    add({0x4D, 0x85, 0xC9});        // test r9, r9
    jrel8(0x79, &to_done);          // jns  done   델타 >= 0 -> 데미지가 아니다
    add({0x66, 0x83, 0xFA, 0x00});  // cmp  dx, 0
    jrel8(0x75, &to_done);          // jne  done   Health 가 아니다
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallOwner));  // mov rax, &owner
    add({0x48, 0x8B, 0x00});        // mov  rax, [rax]
    add({0x48, 0x85, 0xC0});        // test rax, rax
    jrel8(0x74, &to_done);          // je   done   꺼졌거나 아직 root 를 모른다
    add({0x48, 0x39, 0xC1});        // cmp  rcx, rax
    jrel8(0x75, &to_done);          // jne  done   내가 맞은 게 아니다
    add({0x48, 0x8B, 0x44, 0x24, 0x38});        // mov rax, [rsp+0x38] sourceCtx
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});  // cmp rax, 0x10000
    jrel8(0x72, &to_zero);          // jb   zero   출처가 없다 -> 세상이 때렸다
    add({0x48, 0xC1, 0xE8, 0x2F});  // shr  rax, 47
    jrel8(0x75, &to_done);          // jne  done   말이 안 되는 포인터는 안 건드린다
    add({0x48, 0x8B, 0x44, 0x24, 0x38});        // mov rax, [rsp+0x38]
    add({0x48, 0x8B, 0x40, 0x68});              // mov rax, [rax+0x68] 가해자
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});  // cmp rax, 0x10000
    jrel8(0x72, &to_zero);          // jb   zero   뒤에 아무도 없다 -> 낙하다
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallLetThrough));
    add({0x48, 0xFF, 0x00});        // inc  qword [rax]
    jrel8(0xEB, &to_done);          // jmp  done   진짜 가해자다 - 통과시킨다

    const std::size_t zero_at = b.size();
    add({0x45, 0x33, 0xC9});        // zero: xor r9d, r9d   (데미지 0)
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallZeroed));
    add({0x48, 0xFF, 0x00});        // inc  qword [rax]

    const std::size_t done_at = b.size();
    add({0x58, 0x9D});              // done: pop rax / popfq
    for (std::size_t i = 0; i < kNofallOrigSize; ++i) b.push_back(orig[i]);
    add({0xFF, 0x25, 0x00, 0x00, 0x00, 0x00});  // jmp [rip+0]
    addq(static_cast<std::uint64_t>(site + kNofallOrigSize));

    auto patch = [&](const std::vector<Fixup>& fs, std::size_t target) {
        for (const auto& f : fs) {
            const std::ptrdiff_t rel =
                static_cast<std::ptrdiff_t>(target) -
                static_cast<std::ptrdiff_t>(f.disp + 1);
            if (rel < -128 || rel > 127) return false;
            b[f.disp] = static_cast<std::uint8_t>(static_cast<std::int8_t>(rel));
        }
        return true;
    };
    if (!patch(to_zero, zero_at) || !patch(to_done, done_at)) {
        out.why = "rel8 분기가 사거리를 벗어난다";
        return out;
    }
    if (b.size() > kNofallCaveSize) {
        out.why = "케이브가 상한을 넘는다";
        return out;
    }

    out.code = std::move(b);
    out.ok = true;
    return out;
}

}  // namespace cdtb::game
