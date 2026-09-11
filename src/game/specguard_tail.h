#pragma once

#include <cstddef>
#include <cstdint>

namespace cdtb::game {

// 케이브로 옮길 꼬리 명령이 위치 독립인지 본다(첫 명령만). 상대 분기·호출
// (E8/E9/EB/70~7F/E0~E3/0F 80~8F)이나 RIP 상대 modrm(mod==0, rm==5)은 옮기는
// 순간 다른 곳을 가리키므로 거부한다 - 설치 코드가 "건너뜀 + warn" 으로 빠진다.
// 길이만 보고 복사하면 다음 갱신에서 그런 자리에 걸릴 때 로그 없이 죽는다
// (2차 리뷰 관찰 2026-09-11). 접두(REX 40~4F·66·F2·F3·세그먼트)는 건너뛴다.
// 꼬리가 없으면(n==0) 안전. 잘렸거나 모르는 형태는 거부한다 - 설치를 못 하는
// 쪽이 크래시보다 낫다.
inline bool specguard_tail_is_safe(const std::uint8_t* tail, std::size_t n) {
    if (n == 0) return true;
    if (tail == nullptr) return false;
    std::size_t i = 0;
    while (i < n) {
        const std::uint8_t p = tail[i];
        const bool prefix = (p & 0xF0) == 0x40 || p == 0x66 || p == 0xF2 ||
                            p == 0xF3 || p == 0x26 || p == 0x2E || p == 0x36 ||
                            p == 0x3E || p == 0x64 || p == 0x65;
        if (!prefix) break;
        ++i;
    }
    if (i >= n) return false;
    const std::uint8_t op = tail[i];
    if (op == 0xE8 || op == 0xE9 || op == 0xEB || (op >= 0x70 && op <= 0x7F) ||
        (op >= 0xE0 && op <= 0xE3)) {
        return false;   // call/jmp/jcc/loop rel
    }
    std::size_t modrm_at = 0;
    if (op == 0x0F) {
        if (i + 1 >= n) return false;
        const std::uint8_t op2 = tail[i + 1];
        if (op2 >= 0x80 && op2 <= 0x8F) return false;   // jcc rel32
        // 0F xx 는 거의 다 modrm 을 가진다. 없는 것(rdtsc 등)도 보수적으로 본다.
        modrm_at = (op2 == 0x38 || op2 == 0x3A) ? i + 3 : i + 2;
    } else {
        // modrm 을 가지는 1바이트 opcode 만 본다. 나머지(B8+r, 50+r, 90, C3 ...)는
        // 즉시값·레지스터 인코딩이라 위치 독립이다.
        const bool has_modrm =
            (op < 0x40 && (op & 7) < 4) || op == 0x63 || op == 0x69 ||
            op == 0x6B || (op >= 0x80 && op <= 0x8F) || op == 0xC0 ||
            op == 0xC1 || op == 0xC6 || op == 0xC7 ||
            (op >= 0xD0 && op <= 0xD3) || op == 0xF6 || op == 0xF7 ||
            op == 0xFE || op == 0xFF;
        if (!has_modrm) return true;
        modrm_at = i + 1;
    }
    if (modrm_at >= n) return false;   // 잘린 명령 - 판단 불가
    const std::uint8_t modrm = tail[modrm_at];
    if ((modrm >> 6) == 0 && (modrm & 7) == 5) return false;   // [rip+disp32]
    return true;
}

}  // namespace cdtb::game
