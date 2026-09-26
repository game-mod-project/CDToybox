// 코드 패치의 **창 계산·끼워 넣기·상태 기계**. 여기서 한 칸만 어긋나면 게임의 남의
// 명령을 부순다 - 읽기·쓰기를 밖에서 받게 해 두어 상태 기계 전체를 가짜 메모리 위에서
// 태울 수 있다.
//
// 실측 자리(2026-09-13, 실행 파일에서 직접 확인 - 검토가 독립으로 재확인):
//   RVA 0x0E808870  48 89 5C 24 18 66 89 54   함수 진입점 -> B0 01 C3 (mov al,1; ret)
//   RVA 0x02140CF9  0F 9D 44 24 40            setge byte [rsp+0x40] -> C6 44 24 40 01
//                   (창은 0x02140CF8, 앞 바이트 6A 는 앞 명령의 변위)
//
// 위는 **exe 1.0.0.2944** 자리다(2026-09-18 재도출). 2850 은 0x0E0AB860 ·
// 0x0208B639 였고 **창 8바이트는 둘 다 그대로**다 - 코드가 아니라 자리만 밀렸다.
#include <cstdint>
#include <cstring>

#include "game/skillgate.h"
#include "harness.h"

using cdtb::game::gate_apply;
using cdtb::game::gate_def;
using cdtb::game::GateDef;
using cdtb::game::GateIo;
using cdtb::game::gate_probe;
using cdtb::game::GateSlot;
using cdtb::game::patch_splice;
using cdtb::game::patch_window;

using cdtb::game::kStepAlready;
using cdtb::game::kStepForeignNow;
using cdtb::game::kStepForeignOriginal;
using cdtb::game::kStepOk;
using cdtb::game::kStepOutOfImage;
using cdtb::game::kStepReadFailed;
using cdtb::game::kStepWriteFailed;

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

// --------------------------------------------------------------- 상태 기계
//
// 위험한 절반은 여기다. 원본이 다를 때 거부하는가, 끄기가 남의 값을 거부하는가,
// 재진입이 원본 사본을 덮어쓰지 않는가 - 이 넷은 순수 함수 시험으로는 안 걸린다.

namespace {

constexpr std::uintptr_t kModBase = 0x140000000ULL;
constexpr std::size_t kModSize = 0x1000;

// 창 하나를 담는 가짜 메모리. origin 은 창의 시작 주소다.
struct FakeMem {
    std::uintptr_t origin = kModBase + 0x100;
    std::uint8_t buf[8] = {};
    bool read_fail = false;
    bool write_fail = false;
    int writes = 0;
};

bool fake_read(void* ctx, std::uintptr_t base, std::uint8_t out[8]) {
    auto* m = static_cast<FakeMem*>(ctx);
    if (m->read_fail || base != m->origin) return false;
    std::memcpy(out, m->buf, 8);
    return true;
}

bool fake_write(void* ctx, std::uintptr_t base, std::uint64_t v) {
    auto* m = static_cast<FakeMem*>(ctx);
    if (m->write_fail || base != m->origin) return false;
    std::memcpy(m->buf, &v, 8);
    ++m->writes;
    return true;
}

GateIo fake_io(FakeMem* m) {
    GateIo io;
    io.read8 = &fake_read;
    io.write8 = &fake_write;
    io.ctx = m;
    return io;
}

// 실제 관문1 과 같은 모양: 창 0x100, off 1, 5바이트 쓰기.
const GateDef& test_gate() {
    static const GateDef d = {
        "시험 관문", "",
        0x101,
        {0x6A, 0x0F, 0x9D, 0x44, 0x24, 0x40, 0x41, 0x8B},
        {0xC6, 0x44, 0x24, 0x40, 0x01},
        5};
    return d;
}

FakeMem fresh() {
    FakeMem m;
    std::memcpy(m.buf, test_gate().want, 8);
    return m;
}

}  // namespace

TEST(gate_apply_installs_and_restores_the_exact_bytes) {
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);

    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) == kStepOk);
    CHECK(s.on);
    CHECK(s.site == kModBase + 0x101);
    CHECK(m.writes == 1);
    // off 1 에 5바이트가 들어가고 **앞뒤는 그대로**여야 한다.
    CHECK(m.buf[0] == 0x6A);   // 앞 명령의 변위 - 건드리면 앞 명령이 깨진다
    CHECK(m.buf[1] == 0xC6);
    CHECK(m.buf[2] == 0x44);
    CHECK(m.buf[3] == 0x24);
    CHECK(m.buf[4] == 0x40);
    CHECK(m.buf[5] == 0x01);
    CHECK(m.buf[6] == 0x41);   // 다음 명령의 머리
    CHECK(m.buf[7] == 0x8B);

    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, false, io) == kStepOk);
    CHECK(!s.on);
    CHECK(m.writes == 2);
    CHECK(std::memcmp(m.buf, test_gate().want, 8) == 0);
}

TEST(gate_apply_refuses_when_a_byte_outside_the_write_span_differs) {
    // **치명 1 의 회귀 시험.** 예전에는 쓰기 폭(관문0 은 3바이트)만 비교해서,
    // 게임 갱신 뒤 우연히 같은 머리를 가진 **남의 함수**를 덮을 수 있었다.
    // 여기서 다른 것은 창의 마지막 바이트 - 쓰기 폭 밖이다.
    FakeMem m = fresh();
    m.buf[7] = 0x99;
    GateSlot s;
    const GateIo io = fake_io(&m);

    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) ==
          kStepForeignOriginal);
    CHECK(!s.on);
    CHECK(m.writes == 0);      // 한 바이트도 안 썼다
    CHECK(m.buf[7] == 0x99);
}

TEST(gate_apply_refuses_to_restore_bytes_that_are_not_ours) {
    // 다른 모드가 같은 창에 자기 트램펄린을 달았다 - 우리가 떠 둔 옛 8바이트로
    // 덮으면 그쪽이 반쪽만 남아 죽는다. 되돌리지 않는 쪽이 낫다.
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) == kStepOk);

    m.buf[6] = 0xE9;   // 남이 끼어들었다
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, false, io) ==
          kStepForeignNow);
    CHECK(s.on);            // 우리 상태는 "걸려 있다" 그대로다
    CHECK(m.writes == 1);   // 되돌리기 쓰기는 안 일어났다
    CHECK(m.buf[6] == 0xE9);
}

TEST(gate_apply_does_nothing_when_already_in_that_state) {
    // 두 번째 켜기가 **패치된 값을 원본으로 저장하면** 되돌리기가 게임을 부순다.
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) == kStepOk);
    const std::uint64_t saved = s.orig;

    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) ==
          kStepAlready);
    CHECK(m.writes == 1);
    CHECK(s.orig == saved);

    // 끄기가 여전히 진짜 원본으로 되돌린다.
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, false, io) == kStepOk);
    CHECK(std::memcmp(m.buf, test_gate().want, 8) == 0);
    // 이미 꺼진 것을 또 끄지 않는다.
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, false, io) ==
          kStepAlready);
    CHECK(m.writes == 2);
}

TEST(gate_can_be_installed_again_after_a_restore) {
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) == kStepOk);
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, false, io) == kStepOk);
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) == kStepOk);
    CHECK(s.on);
    CHECK(m.buf[1] == 0xC6);
    CHECK(m.writes == 3);
}

TEST(gate_apply_refuses_an_address_outside_the_image) {
    // 갱신으로 이미지가 줄면 이 주소가 밖이다. 생 포인터로 읽었다면 렌더 스레드가
    // 그 자리에서 죽었을 자리다.
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);
    CHECK(gate_apply(test_gate(), &s, kModBase, 0x100, true, io) ==
          kStepOutOfImage);
    CHECK(gate_apply(test_gate(), &s, 0, kModSize, true, io) == kStepOutOfImage);
    CHECK(gate_apply(test_gate(), &s, kModBase, 0, true, io) == kStepOutOfImage);
    CHECK(!s.on);
    CHECK(m.writes == 0);
}

TEST(gate_apply_reports_failures_without_claiming_success) {
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);

    m.read_fail = true;
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) ==
          kStepReadFailed);
    CHECK(!s.probed);   // 못 읽은 것은 판정이 아니다 - 다음에 다시 본다
    CHECK(!s.on);

    m.read_fail = false;
    m.write_fail = true;
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, io) ==
          kStepWriteFailed);
    CHECK(!s.on);       // 켜진 척하지 않는다
    CHECK(m.writes == 0);
    CHECK(std::memcmp(m.buf, test_gate().want, 8) == 0);

    // 널 io 는 거부한다(시험이 아니라 부르는 쪽 실수를 막는 그물).
    GateIo empty;
    CHECK(gate_apply(test_gate(), &s, kModBase, kModSize, true, empty) !=
          kStepOk);
    CHECK(gate_apply(test_gate(), nullptr, kModBase, kModSize, true, io) !=
          kStepOk);
}

TEST(gate_probe_looks_without_writing) {
    FakeMem m = fresh();
    GateSlot s;
    const GateIo io = fake_io(&m);
    CHECK(gate_probe(test_gate(), &s, kModBase, kModSize, io) == kStepOk);
    CHECK(s.probed);
    CHECK(m.writes == 0);
    CHECK(!s.on);
    CHECK(s.site == kModBase + 0x101);

    FakeMem other = fresh();
    other.buf[5] = 0x00;   // 쓰기 폭 안이지만 값이 다르다
    GateSlot s2;
    const GateIo io2 = fake_io(&other);
    CHECK(gate_probe(test_gate(), &s2, kModBase, kModSize, io2) ==
          kStepForeignOriginal);
    CHECK(s2.probed);
    CHECK(other.writes == 0);
}

TEST(the_shipped_gate_table_matches_what_the_executable_had) {
    // 표가 바뀌면 여기서 걸린다. 이 숫자들은 실행 파일을 직접 읽어 확인한 것이고
    // (2026-09-13), 검토가 독립으로 한 번 더 확인했다.
    // **exe 1.0.0.2944 로 다시 짚었다**(2026-09-18) - 자리만 밀렸고 창 8바이트는
    // 둘 다 그대로다. 2850 자리는 관문0 0x0E0AB860 · 관문1 0x0208B639 였다.
    // **exe 1.0.0.2949 로 또 짚었다**(2026-09-22) - 같은 경로(슬롯 0x6CF7BF0 을 읽는 유일한
    // 곳 -> 직전 썽크 -> jmp 목적지 / 같은 함수의 setge)로 내려갔고 창은 또 그대로다.
    // 2944 자리는 관문0 0x0E808870 · 관문1 0x02140CF9 였다.
    // **exe 1.0.0.2976 으로 또 짚었다**(2026-09-26) - 같은 경로로 내려갔고 창은
    // 또 그대로다. 2949 자리는 관문0 0x0E3F9650 · 관문1 0x02140D09 였다.
    const GateDef* g0 = gate_def(cdtb::game::kGateFromType);
    CHECK(g0 != nullptr);
    CHECK(g0->rva == 0x0E49B3E0);
    CHECK(g0->len == 3);
    const std::uint8_t w0[8] = {0x48, 0x89, 0x5C, 0x24, 0x18, 0x66, 0x89, 0x54};
    CHECK(std::memcmp(g0->want, w0, 8) == 0);
    CHECK(g0->with[0] == 0xB0 && g0->with[1] == 0x01 && g0->with[2] == 0xC3);
    // 진입점이라 창이 그 자리에서 시작한다.
    std::uintptr_t base = 0;
    std::size_t off = 0;
    CHECK(patch_window(0x140000000ULL + g0->rva, g0->len, &base, &off));
    CHECK(off == 0);

    const GateDef* g1 = gate_def(cdtb::game::kGateCost);
    CHECK(g1 != nullptr);
    CHECK(g1->rva == 0x02140D59);
    CHECK(g1->len == 5);
    // **창 8바이트 전체**여야 한다 - 앞 바이트 0x6A 는 앞 명령의 변위다.
    const std::uint8_t w1[8] = {0x6A, 0x0F, 0x9D, 0x44, 0x24, 0x40, 0x41, 0x8B};
    CHECK(std::memcmp(g1->want, w1, 8) == 0);
    const std::uint8_t p1[5] = {0xC6, 0x44, 0x24, 0x40, 0x01};
    CHECK(std::memcmp(g1->with, p1, 5) == 0);
    CHECK(patch_window(0x140000000ULL + g1->rva, g1->len, &base, &off));
    CHECK(off == 1);
    CHECK(off + g1->len <= 8);

    CHECK(gate_def(-1) == nullptr);
    CHECK(gate_def(cdtb::game::kGateCount) == nullptr);
}
