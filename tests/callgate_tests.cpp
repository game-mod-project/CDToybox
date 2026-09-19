// 호출 위치 관문(실내·지붕·지역)의 **정의 자체**를 태운다.
//
// 여기서 한 칸만 어긋나면 게임의 남의 명령을 부순다. 그래서 네 관문의 창·오프셋·
// 끼워 넣기 결과를 전부 못박아 둔다 - 실행 파일에서 뜬 값 그대로다
// (**exe 1.0.0.2944**, 2026-09-18):
//
//   실내 RVA 0x009DE7DC  창 0x009DE7D8 +4   03 03 84 C0 **74** 0E 8B 05
//   지붕 RVA 0x009DE839  창 0x009DE838 +1   C0 **74** 0A 8B 05 DB 92 31
//   지역 RVA 0x009DD899  창 0x009DD898 +1   C0 **74** 75 8B 05 7F A2 31
//   위치 RVA 0x009DD6C4  창 0x009DD6C0 +4   06 00 84 C0 **75** 0A 8B 05
//
// 2850 자리는 실내 0x9635FC · 지붕 0x963659 · 지역 0x9626B9 · 위치 0x9624E4
// 였다(넷 다 +0x7B1E0 밀렸다). 갱신 때 이 시험이 먼저 빨개져야 한다 - 그것이
// 이 파일의 일이다.
//
// 넷 다 바꾸는 것은 **조건 점프 한 바이트** -> `EB`(jmp) 다. 거부로 가는 분기를
// 무조건 건너뛰게 만들어 오류 대입을 지나친다. 위치 관문만 `75`(jne)인데,
// 거기서는 통과 쪽이 아래로 붙어 있어서다.
#include <cstdint>
#include <cstring>

#include "game/callgate.h"
#include "game/skillgate.h"
#include "harness.h"

namespace {

using cdtb::game::callgate_def;
using cdtb::game::kCallGateExclusiveStage;
using cdtb::game::kCallGatePlaceCount;
using cdtb::game::gate_apply;
using cdtb::game::GateDef;
using cdtb::game::GateIo;
using cdtb::game::GateSlot;
using cdtb::game::kCallGateCount;
using cdtb::game::kCallGateIndoor;
using cdtb::game::kCallGatePosition;
using cdtb::game::kCallGateRegion;
using cdtb::game::kCallGateRoof;
using cdtb::game::kStepOk;
using cdtb::game::patch_splice;
using cdtb::game::patch_window;

constexpr std::uint8_t kJe = 0x74;
constexpr std::uint8_t kJne = 0x75;
constexpr std::uint8_t kJmp = 0xEB;

// 창 8바이트를 리틀엔디언 qword 로. 실제 읽기가 그렇게 들어온다.
std::uint64_t as_qword(const std::uint8_t b[8]) {
    std::uint64_t v = 0;
    std::memcpy(&v, b, 8);
    return v;
}

TEST(callgate_table_has_all_gates) {
    // 넷은 "서 있는 자리" 층(문구를 띄우며 거부), 다섯째는 보스룸 층
    // (문구 없이 막는다). 화면이 이 둘을 갈라 그리므로 경계가 못박혀야 한다.
    CHECK_EQ(kCallGatePlaceCount, 4);
    CHECK_EQ(kCallGateCount, 5);
    CHECK_EQ(static_cast<int>(kCallGateExclusiveStage), kCallGatePlaceCount);
    for (int g = 0; g < kCallGateCount; ++g) {
        const GateDef* d = callgate_def(g);
        CHECK(d != nullptr);
        CHECK(d->name[0] != 0);
        CHECK(d->what[0] != 0);
    }
    CHECK(callgate_def(-1) == nullptr);
    CHECK(callgate_def(kCallGateCount) == nullptr);
}

TEST(callgate_sites_match_the_measured_rvas) {
    CHECK_EQ(static_cast<long long>(callgate_def(kCallGateIndoor)->rva),
             0x009DE7DCLL);
    CHECK_EQ(static_cast<long long>(callgate_def(kCallGateRoof)->rva),
             0x009DE839LL);
    CHECK_EQ(static_cast<long long>(callgate_def(kCallGateRegion)->rva),
             0x009DD899LL);
    CHECK_EQ(static_cast<long long>(callgate_def(kCallGatePosition)->rva),
             0x009DD6C4LL);
}

TEST(callgate_gates_all_moved_by_one_delta_in_2944) {
    // 2850 -> 2944 에서 넷이 **같은 폭**으로 밀렸다. 하나만 폭이 다르면 그
    // 관문을 잘못 짚었다는 신호다 - 다음 갱신 때 이 대조가 그것을 잡는다.
    // (영역이 다르면 폭도 달라진다. 같은 영역 안에서만 성립한다.)
    // **자리 층 넷만** 본다. 보스룸 관문은 2944 에서 처음 찾은 것이라
    // 2850 짝이 없고, 애초에 다른 영역이라 폭도 다를 것이다.
    const long long kDelta = 0x7B1E0LL;
    const long long old_rva[kCallGatePlaceCount] = {0x009635FCLL, 0x00963659LL,
                                                    0x009626B9LL, 0x009624E4LL};
    for (int g = 0; g < kCallGatePlaceCount; ++g) {
        CHECK_EQ(static_cast<long long>(callgate_def(g)->rva) - old_rva[g],
                 kDelta);
    }
}

TEST(callgate_patches_exactly_one_byte_and_it_is_a_conditional_jump) {
    // 자리 층 넷만이다. 보스룸 관문은 점프가 아니라 **결과를 고정**한다.
    for (int g = 0; g < kCallGatePlaceCount; ++g) {
        const GateDef* d = callgate_def(g);
        // 쓰기 폭은 한 바이트다. 늘리면 뒤 명령을 먹는다.
        CHECK_EQ(static_cast<long long>(d->len), 1LL);
        CHECK_EQ(static_cast<int>(d->with[0]), static_cast<int>(kJmp));

        std::uintptr_t base = 0;
        std::size_t off = 0;
        // 모듈 베이스는 항상 64KB 정렬이라 창 계산에 영향이 없다.
        CHECK(patch_window(0x140000000ULL + d->rva, d->len, &base, &off));
        CHECK_EQ(static_cast<long long>(base),
                 static_cast<long long>(0x140000000ULL + (d->rva & ~7ULL)));
        CHECK(off + d->len <= 8);
        // **덮는 자리가 정말 조건 점프여야 한다.** 여기가 어긋나면 엉뚱한
        // 바이트를 jmp 로 만든다 - 확인 창이 통과해도 이 검사는 따로 필요하다.
        // 오류로 떨어지는 쪽이 어디 붙었느냐에 따라 je(74)·jne(75) 둘 다 나온다.
        const int op = static_cast<int>(d->want[off]);
        CHECK(op == static_cast<int>(kJe) || op == static_cast<int>(kJne));
    }
}

TEST(callgate_position_uses_jne_not_je) {
    // 실내 창과 모양이 닮아서(…84 C0 7x 0x 8B 05) 한 번 헷갈릴 자리다.
    // 위치 관문은 통과 쪽이 아래로 붙어 있어 **jne** 다.
    const GateDef* d = callgate_def(kCallGatePosition);
    CHECK_EQ(static_cast<int>(d->want[d->rva & 7ULL]),
             static_cast<int>(kJne));
    const GateDef* indoor = callgate_def(kCallGateIndoor);
    CHECK_EQ(static_cast<int>(indoor->want[indoor->rva & 7ULL]),
             static_cast<int>(kJe));
    // 두 창이 실제로 구분된다(앞 두 바이트가 다르다).
    CHECK(std::memcmp(d->want, indoor->want, 2) != 0);
}

TEST(callgate_windows_are_the_measured_bytes) {
    const std::uint8_t indoor[8] = {0x03, 0x03, 0x84, 0xC0, 0x74, 0x0E, 0x8B,
                                    0x05};
    const std::uint8_t roof[8] = {0xC0, 0x74, 0x0A, 0x8B, 0x05, 0xDB, 0x92,
                                  0x31};
    const std::uint8_t region[8] = {0xC0, 0x74, 0x75, 0x8B, 0x05, 0x7F, 0xA2,
                                    0x31};
    const std::uint8_t position[8] = {0x06, 0x00, 0x84, 0xC0, 0x75, 0x0A, 0x8B,
                                      0x05};
    CHECK(std::memcmp(callgate_def(kCallGateIndoor)->want, indoor, 8) == 0);
    CHECK(std::memcmp(callgate_def(kCallGateRoof)->want, roof, 8) == 0);
    CHECK(std::memcmp(callgate_def(kCallGateRegion)->want, region, 8) == 0);
    CHECK(std::memcmp(callgate_def(kCallGatePosition)->want, position, 8) == 0);
}

TEST(callgate_splice_turns_je_into_jmp_and_touches_nothing_else) {
    // je -> jmp 는 **자리 층 넷**의 이야기다. 보스룸 관문은 결과를 고정하므로
    // 따로 본다(exclusive_stage_gate_splice_leaves_the_rest_alone).
    for (int g = 0; g < kCallGatePlaceCount; ++g) {
        const GateDef* d = callgate_def(g);
        std::uintptr_t base = 0;
        std::size_t off = 0;
        CHECK(patch_window(0x140000000ULL + d->rva, d->len, &base, &off));

        const std::uint64_t orig = as_qword(d->want);
        const std::uint64_t patched = patch_splice(orig, off, d->with, d->len);
        std::uint8_t after[8] = {};
        std::memcpy(after, &patched, 8);

        CHECK_EQ(static_cast<int>(after[off]), static_cast<int>(kJmp));
        for (std::size_t i = 0; i < 8; ++i) {
            if (i == off) continue;
            CHECK_EQ(static_cast<int>(after[i]),
                     static_cast<int>(d->want[i]));
        }
    }
}

// ------------------------------------------------ 상태 기계 (가짜 메모리)

constexpr std::uintptr_t kModBase = 0x140000000ULL;
constexpr std::size_t kModSize = 0x20000000ULL;

struct FakeMem {
    std::uintptr_t origin = 0;
    std::uint8_t buf[8] = {};
    int writes = 0;
};

bool fake_read(void* ctx, std::uintptr_t base, std::uint8_t out[8]) {
    auto* m = static_cast<FakeMem*>(ctx);
    if (base != m->origin) return false;
    std::memcpy(out, m->buf, 8);
    return true;
}

bool fake_write(void* ctx, std::uintptr_t base, std::uint64_t v) {
    auto* m = static_cast<FakeMem*>(ctx);
    if (base != m->origin) return false;
    std::memcpy(m->buf, &v, 8);
    ++m->writes;
    return true;
}

GateIo bind(FakeMem* m) {
    GateIo io;
    io.read8 = &fake_read;
    io.write8 = &fake_write;
    io.ctx = m;
    return io;
}

TEST(callgate_round_trip_restores_the_original_window) {
    for (int g = 0; g < kCallGateCount; ++g) {
        const GateDef* d = callgate_def(g);
        FakeMem m;
        m.origin = kModBase + (d->rva & ~7ULL);
        std::memcpy(m.buf, d->want, 8);

        GateSlot s;
        CHECK_EQ(static_cast<int>(
                     gate_apply(*d, &s, kModBase, kModSize, true, bind(&m))),
                 static_cast<int>(kStepOk));
        CHECK(s.on);
        // 켠 뒤에는 그 자리가 **정의가 말한 그 바이트들**이다. 관문마다 쓰는
        // 것이 다르므로(자리 층은 jmp 한 바이트, 보스룸은 mov bl,1;nop 셋)
        // kJmp 로 못박지 않고 정의를 그대로 대조한다.
        const std::size_t off = d->rva & 7ULL;
        for (std::size_t i = 0; i < d->len; ++i) {
            CHECK_EQ(static_cast<int>(m.buf[off + i]),
                     static_cast<int>(d->with[i]));
        }

        CHECK_EQ(static_cast<int>(
                     gate_apply(*d, &s, kModBase, kModSize, false, bind(&m))),
                 static_cast<int>(kStepOk));
        CHECK(!s.on);
        // 끄면 창이 **바이트 단위로** 원래대로여야 한다.
        CHECK(std::memcmp(m.buf, d->want, 8) == 0);
        CHECK_EQ(m.writes, 2);
    }
}

// ------------------------------------------------- 보스룸(전용 스테이지) 관문

TEST(exclusive_stage_gate_forces_the_verdict_not_a_jump) {
    // 이 관문은 분기를 뒤집는 것이 아니라 판정 결과를 늘 "거짓" 으로 만든다.
    //   xor bl,1  (80 F3 01)  ->  mov bl,1 ; nop  (B3 01 90)
    // 반환 규약이 참=0 · 거짓=1 · 판정불가=2 라, bl=1 이 "막히지 않음" 이다.
    const GateDef* d = callgate_def(kCallGateExclusiveStage);
    CHECK(d != nullptr);
    CHECK_EQ(static_cast<long long>(d->rva), 0x022EA84ALL);
    CHECK_EQ(static_cast<long long>(d->len), 3LL);
    CHECK_EQ(static_cast<int>(d->with[0]), 0xB3);   // mov bl, imm8
    CHECK_EQ(static_cast<int>(d->with[1]), 0x01);   // 1 = 거짓
    CHECK_EQ(static_cast<int>(d->with[2]), 0x90);   // nop (xor 의 셋째 바이트)
}

TEST(exclusive_stage_gate_overwrites_exactly_the_xor) {
    // 덮는 자리가 정말 `xor bl,1` 이어야 한다. 한 칸만 밀려도 `lea` 를 먹는다.
    const GateDef* d = callgate_def(kCallGateExclusiveStage);
    std::uintptr_t base = 0;
    std::size_t off = 0;
    CHECK(patch_window(0x140000000ULL + d->rva, d->len, &base, &off));
    CHECK_EQ(static_cast<long long>(off), 2LL);
    CHECK(off + d->len <= 8);
    CHECK_EQ(static_cast<int>(d->want[off]), 0x80);       // xor r/m8, imm8
    CHECK_EQ(static_cast<int>(d->want[off + 1]), 0xF3);   // /6 bl
    CHECK_EQ(static_cast<int>(d->want[off + 2]), 0x01);
}

TEST(exclusive_stage_gate_splice_leaves_the_rest_alone) {
    const GateDef* d = callgate_def(kCallGateExclusiveStage);
    std::uintptr_t base = 0;
    std::size_t off = 0;
    CHECK(patch_window(0x140000000ULL + d->rva, d->len, &base, &off));
    const std::uint64_t orig = as_qword(d->want);
    const std::uint64_t next = patch_splice(orig, off, d->with, d->len);
    std::uint8_t after[8];
    std::memcpy(after, &next, 8);
    CHECK_EQ(static_cast<int>(after[2]), 0xB3);
    CHECK_EQ(static_cast<int>(after[3]), 0x01);
    CHECK_EQ(static_cast<int>(after[4]), 0x90);
    for (std::size_t i = 0; i < 8; ++i) {
        if (i >= off && i < off + d->len) continue;
        CHECK_EQ(static_cast<int>(after[i]), static_cast<int>(d->want[i]));
    }
    // 되돌리면 원본과 같아야 한다.
    CHECK(patch_splice(next, off, d->want + off, d->len) == orig);
}

}  // namespace
