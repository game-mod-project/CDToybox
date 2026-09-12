#include <cstdint>

#include "game/specguard_tail.h"
#include "harness.h"

using cdtb::game::specguard_tail_is_safe;

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
