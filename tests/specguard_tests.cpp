#include <cstddef>
#include <cstdint>
#include <iterator>

#include "game/specguard_sites.h"
#include "game/specguard_tail.h"
#include "harness.h"

using cdtb::game::kSpecguardDivMem;
using cdtb::game::kSpecguardDivReg;
using cdtb::game::kSpecguardSiteTotal;
using cdtb::game::SpecguardSite;
using cdtb::game::specguard_tail_is_safe;

// 표에 그 RVA 가 그 patch_len 으로 들어 있는가.
template <std::size_t N>
static bool has_site(const SpecguardSite (&t)[N], std::uint64_t rva,
                     int patch_len) {
    for (std::size_t i = 0; i < N; ++i) {
        if (t[i].rva == rva) return t[i].patch_len == patch_len;
    }
    return false;
}

// 2850 의 실제 꼬리 다섯 개 - 전부 위치 독립이라 통과해야 한다.
TEST(specguard_tail_accepts_the_2850_tails) {
    const std::uint8_t lea[] = {0x8D, 0x48, 0x01};       // lea ecx,[rax+1]  (0x21DB8D8)
    const std::uint8_t mov_mem[] = {0x8B, 0x17};         // mov edx,[rdi]    (0x235021D)
    const std::uint8_t cmp[] = {0x3B, 0x47, 0x04};       // cmp eax,[rdi+4]  (0xF01981B)
    const std::uint8_t mov_reg[] = {0x8B, 0xCF};         // mov ecx,edi      (0x235003B)
    const std::uint8_t mov_r8d[] = {0x41, 0x8B, 0xC8};   // mov ecx,r8d      (0x2350454)
    CHECK(specguard_tail_is_safe(lea, sizeof(lea)));
    CHECK(specguard_tail_is_safe(mov_mem, sizeof(mov_mem)));
    CHECK(specguard_tail_is_safe(cmp, sizeof(cmp)));
    CHECK(specguard_tail_is_safe(mov_reg, sizeof(mov_reg)));
    CHECK(specguard_tail_is_safe(mov_r8d, sizeof(mov_r8d)));
    CHECK(specguard_tail_is_safe(nullptr, 0));   // 꼬리 없음(div 만으로 5바이트)
}

TEST(specguard_tail_rejects_relative_branches) {
    const std::uint8_t call[] = {0xE8, 0x10, 0x00, 0x00, 0x00};
    const std::uint8_t jmp[] = {0xE9, 0x10, 0x00, 0x00, 0x00};
    const std::uint8_t jmp_short[] = {0xEB, 0x10};
    const std::uint8_t jae[] = {0x73, 0x05};
    const std::uint8_t je32[] = {0x0F, 0x84, 0x10, 0x00, 0x00, 0x00};
    const std::uint8_t loop[] = {0xE2, 0xF0};
    const std::uint8_t hinted[] = {0x2E, 0x74, 0x05};   // 분기 힌트 접두 + je
    CHECK(!specguard_tail_is_safe(call, sizeof(call)));
    CHECK(!specguard_tail_is_safe(jmp, sizeof(jmp)));
    CHECK(!specguard_tail_is_safe(jmp_short, sizeof(jmp_short)));
    CHECK(!specguard_tail_is_safe(jae, sizeof(jae)));
    CHECK(!specguard_tail_is_safe(je32, sizeof(je32)));
    CHECK(!specguard_tail_is_safe(loop, sizeof(loop)));
    CHECK(!specguard_tail_is_safe(hinted, sizeof(hinted)));
}

TEST(specguard_tail_rejects_rip_relative_operands) {
    const std::uint8_t mov_rip[] = {0x8B, 0x05, 0x10, 0x00, 0x00, 0x00};         // mov eax,[rip+0x10]
    const std::uint8_t lea_rip[] = {0x48, 0x8D, 0x05, 0x10, 0x00, 0x00, 0x00};   // lea rax,[rip+0x10]
    const std::uint8_t call_rip[] = {0xFF, 0x15, 0x10, 0x00, 0x00, 0x00};        // call [rip+0x10]
    const std::uint8_t cmp_rip[] = {0x48, 0x3B, 0x0D, 0x10, 0x00, 0x00, 0x00};   // cmp rcx,[rip+0x10]
    const std::uint8_t movzx_rip[] = {0x0F, 0xB6, 0x05, 0x10, 0x00, 0x00, 0x00}; // movzx eax,byte [rip+0x10]
    CHECK(!specguard_tail_is_safe(mov_rip, sizeof(mov_rip)));
    CHECK(!specguard_tail_is_safe(lea_rip, sizeof(lea_rip)));
    CHECK(!specguard_tail_is_safe(call_rip, sizeof(call_rip)));
    CHECK(!specguard_tail_is_safe(cmp_rip, sizeof(cmp_rip)));
    CHECK(!specguard_tail_is_safe(movzx_rip, sizeof(movzx_rip)));
    // 같은 opcode 라도 레지스터·변위 인코딩이면 통과
    const std::uint8_t movzx_reg[] = {0x0F, 0xB6, 0x47, 0x04};   // movzx eax,byte [rdi+4]
    CHECK(specguard_tail_is_safe(movzx_reg, sizeof(movzx_reg)));
}

TEST(specguard_tail_rejects_truncated_and_accepts_immediates) {
    const std::uint8_t rex_only[] = {0x48};
    const std::uint8_t op_only[] = {0x8B};      // modrm 이 잘림
    const std::uint8_t two_byte[] = {0x0F};     // 두 번째 opcode 잘림
    CHECK(!specguard_tail_is_safe(rex_only, sizeof(rex_only)));
    CHECK(!specguard_tail_is_safe(op_only, sizeof(op_only)));
    CHECK(!specguard_tail_is_safe(two_byte, sizeof(two_byte)));
    CHECK(!specguard_tail_is_safe(nullptr, 3));
    // 즉시값·레지스터 인코딩은 위치 독립
    const std::uint8_t mov_imm[] = {0xB8, 0x01, 0x00, 0x00, 0x00};   // mov eax,1
    const std::uint8_t xor_reg[] = {0x33, 0xC0};                     // xor eax,eax
    const std::uint8_t nop[] = {0x90};
    const std::uint8_t push[] = {0x41, 0x56};                        // push r14
    CHECK(specguard_tail_is_safe(mov_imm, sizeof(mov_imm)));
    CHECK(specguard_tail_is_safe(xor_reg, sizeof(xor_reg)));
    CHECK(specguard_tail_is_safe(nop, sizeof(nop)));
    CHECK(specguard_tail_is_safe(push, sizeof(push)));
}

// ---------------------------------------------------------------------------
// 사이트 표 (2026-09-20 조사)
//
// 표의 RVA 자체는 시험이 검증할 수 없다 - 저장소에 게임 exe 가 없다. 여기서
// 못박는 것은 **표가 무엇을 들고 있는가**다: 자리를 지우거나 patch_len 을
// 흘리면 시험이 잡는다. RVA 의 근거는 `specguard_sites.h` 의 주석과
// `specs/2026-09-20-specguard-div-family.md` 에 있다.
// ---------------------------------------------------------------------------

// 0xF83F65B 와 0xF83F6B4 는 **같은 함수**(0xF83F5D0..0xF83F734) 안의 두 div 다.
// 둘 다 `[rdi+8]` 로 나누고 0 갈래가 div 로 샌다. 앞의 것만 걸면 가드가
// 몫 0 을 내고 `cmp eax,[rdi+4] / jae` 가 **안 뛰어** 그대로 뒤의 div 로
// 흘러가 거기서 죽는다 - 크래시를 89바이트 뒤로 옮길 뿐이다(실측 2026-09-20).
TEST(specguard_reg_table_has_both_divs_of_the_bag_render_function) {
    CHECK(has_site(kSpecguardDivReg, 0xF83F65BULL, 6));
    CHECK(has_site(kSpecguardDivReg, 0xF83F6B4ULL, 6));
}

// 2944 실측 자리 셋(kDivReg)과 하나(kDivMem[3])는 그대로 있어야 한다.
TEST(specguard_table_keeps_the_2944_measured_sites) {
    CHECK(has_site(kSpecguardDivReg, 0x240919BULL, 5));
    CHECK(has_site(kSpecguardDivReg, 0x24095B4ULL, 6));
    CHECK(has_site(kSpecguardDivMem, 0x240937DULL, 6));
}

// 2850 값으로 남겨 둔 셋은 **지우지 않는다.** install 이 opcode 를 보고
// 거부하므로 안전하고, 남아 있어야 kSiteTotal 이 실제 자리 수를 세어
// `specguard_unsupported()` 가 참이 되고 지급 창이 경고한다. 지우면
// "전부 설치" 라는 거짓 보고가 된다.
TEST(specguard_table_keeps_the_stale_2850_entries) {
    CHECK(has_site(kSpecguardDivMem, 0xEB26B4ULL, 7));
    CHECK(has_site(kSpecguardDivMem, 0xEA920FULL, 5));
    CHECK(has_site(kSpecguardDivMem, 0x21DB8D8ULL, 7));
}

TEST(specguard_site_total_counts_both_tables) {
    CHECK(kSpecguardSiteTotal ==
          static_cast<int>(std::size(kSpecguardDivMem) +
                           std::size(kSpecguardDivReg)));
    CHECK(kSpecguardSiteTotal == 8);
}

// 0xF83F6B4 의 꼬리. `48 85 D2` = test rdx,rdx - REX 접두를 건너뛰면 0x85 이고
// modrm 0xD2 는 mod==3 이라 RIP 상대가 아니다. 케이브로 옮겨도 안전하다.
TEST(specguard_tail_accepts_the_2944_twin_tail) {
    const std::uint8_t test_rdx[] = {0x48, 0x85, 0xD2};
    CHECK(specguard_tail_is_safe(test_rdx, sizeof(test_rdx)));
}
