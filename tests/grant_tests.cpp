#include <cstdint>
#include <vector>

#include "game/grant.h"
#include "harness.h"

namespace {

using cdtb::game::find_actor_getter_rva;
using cdtb::game::find_spawn_ground_rva;

// 실제 실행 파일에서 뽑은 앞머리들. 둘 다 이미지 안에서 유일했다.
const std::uint8_t kActorGetter[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x68,
    0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x48, 0x20, 0x0F, 0xB7, 0x41};

const std::uint8_t kSpawnGround[] = {
    0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x6B,
    0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
    0x81, 0xEC, 0x50, 0x01};

std::vector<std::uint8_t> image_with(const std::uint8_t* body, std::size_t n,
                                     std::size_t at, std::size_t total = 4096) {
    std::vector<std::uint8_t> img(total, 0xCC);
    for (std::size_t i = 0; i < n; ++i) img[at + i] = body[i];
    return img;
}

TEST(find_actor_getter_returns_its_offset) {
    const auto img = image_with(kActorGetter, sizeof(kActorGetter), 0x400);
    std::uint64_t rva = 0;
    CHECK(find_actor_getter_rva(img, &rva));
    CHECK_EQ(rva, 0x400u);
}

TEST(find_actor_getter_fails_when_absent) {
    const std::vector<std::uint8_t> img(4096, 0xCC);
    std::uint64_t rva = 0;
    CHECK(!find_actor_getter_rva(img, &rva));
}

// 두 곳에서 맞으면 어느 쪽인지 고를 수 없다. 엉뚱한 함수를 후킹하면
// 게임이 죽으므로 찾지 못한 것으로 다룬다.
TEST(find_actor_getter_fails_when_ambiguous) {
    auto img = image_with(kActorGetter, sizeof(kActorGetter), 0x400);
    for (std::size_t i = 0; i < sizeof(kActorGetter); ++i) {
        img[0x800 + i] = kActorGetter[i];
    }
    std::uint64_t rva = 0;
    CHECK(!find_actor_getter_rva(img, &rva));
}

TEST(find_spawn_ground_returns_its_offset) {
    const auto img = image_with(kSpawnGround, sizeof(kSpawnGround), 0x120);
    std::uint64_t rva = 0;
    CHECK(find_spawn_ground_rva(img, &rva));
    CHECK_EQ(rva, 0x120u);
}

TEST(find_spawn_ground_fails_when_absent) {
    const std::vector<std::uint8_t> img(4096, 0xCC);
    std::uint64_t rva = 0;
    CHECK(!find_spawn_ground_rva(img, &rva));
}

// 조회 함수는 타입에 따라 다른 것을 돌려준다 - 클라이언트 쪽과
// 서버 쪽 인벤토리 컴포넌트가 둘 다 살아 있다. 어느 쪽을 주는지
// 알아야 해서 본 것을 전부 모은다.
TEST(remember_distinct_adds_a_new_value) {
    std::uintptr_t slots[4]{};
    CHECK_EQ(cdtb::game::remember_distinct(slots, 0, 4, 0x1234u), 1);
    CHECK_EQ(slots[0], std::uintptr_t{0x1234u});
}

TEST(remember_distinct_ignores_a_repeat) {
    std::uintptr_t slots[4]{0x1234u, 0, 0, 0};
    CHECK_EQ(cdtb::game::remember_distinct(slots, 1, 4, 0x1234u), 1);
}

TEST(remember_distinct_stops_when_full) {
    std::uintptr_t slots[2]{0x11u, 0x22u};
    CHECK_EQ(cdtb::game::remember_distinct(slots, 2, 2, 0x33u), 2);
}

TEST(no_actor_before_the_hook_sees_one) {
    CHECK_EQ(cdtb::game::last_actor(), std::uintptr_t{0});
}

}  // namespace
