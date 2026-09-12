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
    // 두 realm 의 root 를 나란히 두고 둘 다 맞춰 본다(nofall_cave.h 의 설명 참고).
    // 꺼져 있으면 두 칸이 0 이고, rcx 는 0 일 수 없으므로 어느 쪽도 맞지 않아 빠진다.
    std::vector<Fixup> to_mine;
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallOwner));  // mov rax, &owners
    add({0x48, 0x3B, 0x08});        // cmp  rcx, [rax]      realm 1
    jrel8(0x74, &to_mine);          // je   mine
    add({0x48, 0x3B, 0x48, 0x08});  // cmp  rcx, [rax+8]    realm 2
    jrel8(0x75, &to_done);          // jne  done   내가 맞은 게 아니다
    const std::size_t mine_at = b.size();   // mine:
    add({0x48, 0x8B, 0x44, 0x24, 0x38});        // mov rax, [rsp+0x38] sourceCtx
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});  // cmp rax, 0x10000
    jrel8(0x72, &to_zero);          // jb   zero   출처가 아예 없다
    add({0x48, 0xC1, 0xE8, 0x2F});  // shr  rax, 47
    jrel8(0x75, &to_done);          // jne  done   말이 안 되는 포인터는 안 건드린다
    add({0x48, 0x8B, 0x44, 0x24, 0x38});        // mov rax, [rsp+0x38]
    // 케이브에서 게임 메모리를 만지는 **유일한** 명령이다. 앞의 두 검사는 "매핑돼
    // 있음" 을 보장하지 못하므로(센티널·해제 직후 포인터), 여기서 액세스 위반이 날
    // 수 있다. nofall.cpp 가 이 주소를 VEH 로 지켜 폴트 시 done 으로 떨군다
    // (= 판정 불가는 그대로 통과. 안전한 쪽).
    out.deref_at = b.size();
    add({0x48, 0x8B, 0x40, 0x68});              // mov rax, [rax+0x68] 가해자
    add({0x48, 0x3D, 0x00, 0x00, 0x01, 0x00});  // cmp rax, 0x10000
    jrel8(0x72, &to_zero);          // jb   zero   뒤에 아무도 없다
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallLetThrough));
    // lock 이 붙어야 한다 - 전투 코드 39곳이 여러 스레드에서 들어오고, 이 두 숫자는
    // 판별식이 맞는지 보는 **유일한** 장치라 증가가 하나라도 유실되면 안 된다.
    add({0xF0, 0x48, 0xFF, 0x00});  // lock inc qword [rax]
    jrel8(0xEB, &to_done);          // jmp  done   진짜 가해자다 - 통과시킨다

    out.zero_at = b.size();
    add({0x45, 0x33, 0xC9});        // zero: xor r9d, r9d   (데미지 0)
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallZeroed));
    add({0xF0, 0x48, 0xFF, 0x00});  // lock inc qword [rax]

    out.done_at = b.size();
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
    if (!patch(to_zero, out.zero_at) || !patch(to_done, out.done_at) ||
        !patch(to_mine, mine_at)) {
        out.why = "rel8 분기가 사거리를 벗어난다";
        out.zero_at = out.done_at = out.deref_at = 0;
        return out;
    }
    if (b.size() > cave_max) {
        out.why = "케이브가 상한을 넘는다";
        out.zero_at = out.done_at = out.deref_at = 0;
        return out;
    }

    out.code = std::move(b);
    out.ok = true;
    return out;
}

NofallCave nofall_build_observe_cave(const std::uint8_t* orig,
                                     std::uintptr_t vars, std::uintptr_t site,
                                     std::size_t cave_max) {
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
    std::vector<Fixup> to_dx;
    std::vector<Fixup> to_hit;

    auto add = [&](std::initializer_list<std::uint8_t> xs) {
        for (auto x : xs) b.push_back(x);
    };
    auto addq = [&](std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    auto jrel8 = [&](std::uint8_t op, std::vector<Fixup>* where) {
        b.push_back(op);
        b.push_back(0);
        where->push_back(Fixup{b.size() - 1});
    };
    // movabs rax, <vars+off> 뒤에 3바이트짜리 한 명령을 붙이는 짧은 도우미.
    auto at_var = [&](std::size_t off, std::initializer_list<std::uint8_t> ins) {
        add({0x48, 0xB8});
        addq(static_cast<std::uint64_t>(vars + off));
        for (auto x : ins) b.push_back(x);
    };

    add({0x9C, 0x50});        // pushfq / push rax
    add({0x4D, 0x85, 0xC9});  // test r9, r9
    jrel8(0x79, &to_done);    // jns done   피해가 아니다(델타 >= 0)

    // 여기부터는 "플레이어든 아니든 피해 이벤트" 다. 무엇이 지나가는지 적는다.
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallEvents));
    add({0xF0, 0x48, 0xFF, 0x00});           // lock inc qword [rax]
    at_var(kNofallLastRcx, {0x48, 0x89, 0x08});  // mov [rax], rcx
    at_var(kNofallLastRdx, {0x48, 0x89, 0x10});  // mov [rax], rdx
    at_var(kNofallLastR9, {0x4C, 0x89, 0x08});   // mov [rax], r9

    // rcx 가 내 root 인가?
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallOwner));  // mov rax, &owners
    add({0x48, 0x3B, 0x08});                   // cmp rcx, [rax]     realm 1
    jrel8(0x74, &to_hit);                      // je  hit
    add({0x48, 0x3B, 0x48, 0x08});             // cmp rcx, [rax+8]   realm 2
    jrel8(0x75, &to_dx);                       // jne chk_dx
    const std::size_t hit_at = b.size();       // hit:
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallRcxHit));
    add({0xF0, 0x48, 0xFF, 0x00});             // lock inc qword [rax]

    const std::size_t dx_at = b.size();        // chk_dx:
    add({0x66, 0x83, 0xFA, 0x00});             // cmp dx, 0
    jrel8(0x75, &to_done);                     // jne done
    add({0x48, 0xB8});
    addq(static_cast<std::uint64_t>(vars + kNofallDxZero));
    add({0xF0, 0x48, 0xFF, 0x00});             // lock inc qword [rax]

    const std::size_t done_at = b.size();      // done:
    add({0x58, 0x9D});                         // pop rax / popfq
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
    if (!patch(to_dx, dx_at) || !patch(to_done, done_at) ||
        !patch(to_hit, hit_at)) {
        out.why = "rel8 분기가 사거리를 벗어난다";
        return out;
    }
    if (b.size() > cave_max) {
        out.why = "케이브가 상한을 넘는다";
        return out;
    }

    out.zero_at = dx_at;    // 관찰 모드에는 zero 갈래가 없다 - chk_dx 를 담아 둔다
    out.done_at = done_at;
    out.deref_at = 0;       // 게임 포인터를 따라가지 않으므로 폴트 가드가 필요 없다
    out.code = std::move(b);
    out.ok = true;
    return out;
}

}  // namespace cdtb::game
