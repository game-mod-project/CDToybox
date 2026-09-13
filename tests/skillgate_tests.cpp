// 코드 패치의 **창 계산과 끼워 넣기**. 여기서 한 칸만 어긋나도 게임의 남의 명령을
// 부순다 - 순수 부분이라 시험이 그대로 덮을 수 있다.
//
// 실측 자리(2026-09-13, 실행 파일에서 직접 확인):
//   RVA 0x0E0AB860  48 89 5C 24 18 ...   함수 프롤로그 -> B0 01 C3 (mov al,1; ret)
//   RVA 0x0208B639  0F 9D 44 24 40       setge byte [rsp+0x40] -> C6 44 24 40 01
#include <cstdint>
#include <cstring>

#include "game/skillgate.h"
#include "harness.h"

using cdtb::game::patch_splice;
using cdtb::game::patch_window;

TEST(patch_window_finds_the_aligned_eight_byte_window) {
    std::uintptr_t base = 0;
    std::size_t off = 0;

    // 정렬된 자리(0x...860): 창이 그 자리에서 시작한다.
    CHECK(patch_window(0x140E0AB860ULL, 3, &base, &off));
    CHECK(base == 0x140E0AB860ULL);
    CHECK(off == 0);

    // **정렬이 안 맞는 자리(0x...639).** 창은 0x...638 이고 5바이트가 1..5 에 들어간다.
    // 이 계산이 틀리면 남의 바이트를 덮는다.
    CHECK(patch_window(0x14208B639ULL, 5, &base, &off));
    CHECK(base == 0x14208B638ULL);
    CHECK(off == 1);
    CHECK(off + 5 <= 8);
}

TEST(patch_window_refuses_what_it_cannot_write_atomically) {
    std::uintptr_t base = 0;
    std::size_t off = 0;
    // 창 두 개에 걸치면 원자 쓰기가 안 된다 - 반쯤 바뀐 명령을 실행할 창이 생긴다.
    CHECK(!patch_window(0x1000005ULL, 5, &base, &off));   // off 5 + 5 > 8
    CHECK(!patch_window(0x1000007ULL, 2, &base, &off));   // off 7 + 2 > 8
    // 길이가 말이 안 되는 것도 막는다.
    CHECK(!patch_window(0x1000000ULL, 0, &base, &off));
    CHECK(!patch_window(0x1000000ULL, 9, &base, &off));
    CHECK(!patch_window(0x1000000ULL, 3, nullptr, &off));
    CHECK(!patch_window(0x1000000ULL, 3, &base, nullptr));
    // 딱 맞는 경계는 받는다.
    CHECK(patch_window(0x1000003ULL, 5, &base, &off));
    CHECK(off == 3);
    CHECK(patch_window(0x1000000ULL, 8, &base, &off));
    CHECK(off == 0);
}

TEST(patch_splice_replaces_only_the_bytes_it_should) {
    // 리틀엔디언에서 qword 의 바이트 0 이 가장 낮은 주소다.
    const std::uint64_t orig = 0x8877665544332211ULL;
    const std::uint8_t with[3] = {0xB0, 0x01, 0xC3};

    // off 0: 낮은 세 바이트만 바뀐다. **16진 리터럴로 적지 않는다** - 자릿수를
    // 세다 틀리기 쉽고(이 시험을 쓰면서 실제로 틀렸다), 틀려도 시험이 통과하는
    // 방향으로 틀리면 더 나쁘다. 바이트로 본다.
    const std::uint64_t r0 = patch_splice(orig, 0, with, 3);
    std::uint8_t b0[8];
    std::memcpy(b0, &r0, 8);
    CHECK(b0[0] == 0xB0);
    CHECK(b0[1] == 0x01);
    CHECK(b0[2] == 0xC3);
    CHECK(b0[3] == 0x44);   // 건드리지 않은 뒤
    CHECK(b0[7] == 0x88);
    // off 1: 한 칸 밀린다. **앞뒤가 보존돼야 한다** - 낮은 0x11 과 위 네 바이트.
    // 16진 리터럴로 적으면 자릿수를 세다 틀리기 쉬우니 바이트로 확인한다.
    const std::uint64_t r1 = patch_splice(orig, 1, with, 3);
    std::uint8_t b[8];
    std::memcpy(b, &r1, 8);
    CHECK(b[0] == 0x11);   // 건드리지 않은 앞
    CHECK(b[1] == 0xB0);
    CHECK(b[2] == 0x01);
    CHECK(b[3] == 0xC3);
    CHECK(b[4] == 0x55);   // 건드리지 않은 뒤
    CHECK(b[7] == 0x88);
}

TEST(patch_splice_is_a_no_op_on_bad_input) {
    const std::uint64_t orig = 0x8877665544332211ULL;
    const std::uint8_t with[3] = {0xB0, 0x01, 0xC3};
    CHECK(patch_splice(orig, 0, nullptr, 3) == orig);
    CHECK(patch_splice(orig, 0, with, 0) == orig);
    CHECK(patch_splice(orig, 6, with, 3) == orig);   // 창을 넘는다
    CHECK(patch_splice(orig, 9, with, 3) == orig);
}

TEST(patch_splice_round_trips_with_the_original) {
    // **되돌리기의 근거.** 켤 때 창 8바이트를 통째로 떠 두므로, 되돌릴 때는 그것을
    // 그대로 쓰면 된다 - 끼워 넣기가 그 밖을 안 건드린다는 것이 그 전제다.
    const std::uint64_t orig = 0x1122334455667788ULL;
    const std::uint8_t with[5] = {0xC6, 0x44, 0x24, 0x40, 0x01};
    const std::uint64_t patched = patch_splice(orig, 1, with, 5);
    CHECK(patched != orig);
    std::uint8_t a[8], c[8];
    std::memcpy(a, &orig, 8);
    std::memcpy(c, &patched, 8);
    CHECK(a[0] == c[0]);   // 앞은 그대로
    CHECK(a[6] == c[6]);   // 뒤도 그대로
    CHECK(a[7] == c[7]);
}
