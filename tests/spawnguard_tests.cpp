// 소환 가드가 돌릴 **호출 자리 판정**. 이 판정이 느슨하면 엉뚱한 `call 조회`
// 를 우리 썽크로 돌려, 크래시를 막는 대신 만든다.
//
// 실측 자리(2026-09-20, exe 1.0.0.2944):
//   RVA 0x02B8DD36  E8 D5 83 5B FF   call 0x2146110      (번호 -> 레코드 조회)
//   RVA 0x02B8DD3B  48 83 78 28 FF   cmp qword [rax+0x28], -1   <- 널 검사가 없다
//
// 조회를 부르는 자리는 **273곳**인데 그중 뒤가 이 `cmp` 인 것은 **한 곳뿐**이다
// (파일 397MB 전량에서도 한 곳). 그래서 꼬리까지 봐야 자리가 확정된다 -
// `E8` + 대상만 보면 273곳 중 아무 데나 통과한다.
#include <cstdint>

#include "game/spawnguard_site.h"
#include "harness.h"

using cdtb::game::kSpawnCallSiteRva;
using cdtb::game::kSpawnEmptyRecordRva;
using cdtb::game::kSpawnLookupRva;
using cdtb::game::SpawnSite;
using cdtb::game::spawnguard_check_site;

namespace {

// 모듈 베이스는 판정에 안 쓰인다(자리와 조회가 같은 공간이면 된다). 실측
// 베이스를 그대로 써서 rel32 산술이 진짜와 같게 돈다.
constexpr std::uintptr_t kBase = 0x140000000ULL;
constexpr std::uintptr_t kSite = kBase + kSpawnCallSiteRva;
constexpr std::uintptr_t kLookup = kBase + kSpawnLookupRva;

}  // namespace

// 실측 바이트 그대로. rel32 = 0xFF5B83D5 = -0xA47C2B 이고
// 0x2B8DD3B - 0xA47C2B = 0x2146110 이다.
TEST(spawnguard_site_accepts_the_measured_2944_bytes) {
    const std::uint8_t o[] = {0xE8, 0xD5, 0x83, 0x5B, 0xFF,
                              0x48, 0x83, 0x78, 0x28, 0xFF};
    CHECK(spawnguard_check_site(o, sizeof(o), kSite, kLookup) == SpawnSite::kOk);
}

// 2944 에서 옛 2850 자리(0x2AD7238)에는 `je`(0F 84 ..)가 들어앉아 있다.
// 자리가 낡으면 여기로 떨어져야 한다.
TEST(spawnguard_site_rejects_a_stale_address_that_is_not_a_call) {
    const std::uint8_t o[] = {0x0F, 0x84, 0x4B, 0x01, 0x00,
                              0x00, 0x48, 0x8D, 0x00, 0x00};
    CHECK(spawnguard_check_site(o, sizeof(o), kSite, kLookup) ==
          SpawnSite::kNotCall);
}

// call 이긴 한데 다른 함수를 부르는 경우.
TEST(spawnguard_site_rejects_a_call_to_something_else) {
    std::uint8_t o[] = {0xE8, 0xD5, 0x83, 0x5B, 0xFF,
                        0x48, 0x83, 0x78, 0x28, 0xFF};
    ++o[1];   // rel32 를 1 틀어 대상이 조회 +1 이 되게 한다
    CHECK(spawnguard_check_site(o, sizeof(o), kSite, kLookup) ==
          SpawnSite::kWrongTarget);
}

// **이것이 이 판정의 값어치다.** 게임이 스스로 가드하는 형제 자리
// (`call 조회 ; lea rbx,[빈 레코드] ; test rax,rax ; cmovne`)는 조회를 부르는
// 것까지 같아서 `E8` + 대상만 보면 통과한다. 거기에 우리 썽크를 걸면 이미
// 멀쩡한 자리를 건드리는 것이라 얻는 것 없이 위험만 보탠다.
TEST(spawnguard_site_rejects_the_sibling_that_the_game_already_guards) {
    const std::uint8_t o[] = {0xE8, 0xD5, 0x83, 0x5B, 0xFF,
                              0x48, 0x8D, 0x1D, 0x2E, 0x91};   // lea rbx,[rip+..]
    CHECK(spawnguard_check_site(o, sizeof(o), kSite, kLookup) ==
          SpawnSite::kGuarded);
}

TEST(spawnguard_site_rejects_short_and_null_reads) {
    const std::uint8_t o[] = {0xE8, 0xD5, 0x83, 0x5B, 0xFF};
    CHECK(spawnguard_check_site(o, sizeof(o), kSite, kLookup) ==
          SpawnSite::kShort);
    CHECK(spawnguard_check_site(nullptr, 10, kSite, kLookup) ==
          SpawnSite::kShort);
}

// 표식 셋이 흘러내리지 않게 못박는다. RVA 자체는 시험이 검증할 수 없다 -
// 저장소에 게임 exe 가 없다. 근거는 `spawnguard_site.h` 주석과
// `specs/2026-09-20-spawnguard-callsite.md`.
TEST(spawnguard_site_keeps_the_measured_2944_rvas) {
    CHECK(kSpawnCallSiteRva == 0x2B8DD36ULL);
    CHECK(kSpawnLookupRva == 0x2146110ULL);
    CHECK(kSpawnEmptyRecordRva == 0x6CF2F70ULL);
}
