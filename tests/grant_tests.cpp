#include <cstdint>
#include <vector>

#include "game/grant.h"
#include "fake_memory.h"
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
TEST(note_actor_adds_a_new_value) {
    std::uintptr_t slots[4]{};
    std::uint32_t hits[4]{};
    CHECK_EQ(cdtb::game::note_actor(slots, hits, 0, 4, 0x1234u), 1);
    CHECK_EQ(slots[0], std::uintptr_t{0x1234u});
    CHECK_EQ(hits[0], std::uint32_t{1});
}

// 플레이어 것은 게임플레이 코드가 계속 부르고 NPC 것은 드물게
// 부른다. 횟수가 곧 누가 플레이어인지를 말해 준다.
TEST(note_actor_counts_a_repeat) {
    std::uintptr_t slots[4]{0x1234u, 0, 0, 0};
    std::uint32_t hits[4]{1, 0, 0, 0};
    CHECK_EQ(cdtb::game::note_actor(slots, hits, 1, 4, 0x1234u), 1);
    CHECK_EQ(hits[0], std::uint32_t{2});
}

TEST(note_actor_stops_when_full) {
    std::uintptr_t slots[2]{0x11u, 0x22u};
    std::uint32_t hits[2]{1, 1};
    CHECK_EQ(cdtb::game::note_actor(slots, hits, 2, 2, 0x33u), 2);
}

// 게임이 함수 앞머리에서 하는 검사와 같은 것을 우리도 먼저 한다.
// 코드에 그대로 있다: 키가 0이면 실패, 개수가 0 이하면 실패.
// 조회 함수는 서버 쪽과 클라이언트 쪽을 다 돌려주고, 서버 쪽만
// 해도 여럿이다(NPC·상자). 실측에서 플레이어 것은 17020회, 다음이
// 1110회, 나머지는 1~3회였다. 서버 쪽 중 가장 많이 불린 것을 고른다.
TEST(best_actor_picks_the_busiest_server) {
    const std::uint32_t hits[4] = {1110, 17020, 2, 3};
    const bool server[4] = {true, true, false, true};
    CHECK_EQ(cdtb::game::best_actor_index(hits, server, 4), 1);
}

TEST(best_actor_ignores_client_side) {
    // 클라이언트 쪽이 더 많이 불려도 고르지 않는다. 치트는 Req 다.
    const std::uint32_t hits[3] = {99999, 5, 7};
    const bool server[3] = {false, true, true};
    CHECK_EQ(cdtb::game::best_actor_index(hits, server, 3), 2);
}

TEST(best_actor_returns_none_without_a_server) {
    const std::uint32_t hits[2] = {10, 20};
    const bool server[2] = {false, false};
    CHECK_EQ(cdtb::game::best_actor_index(hits, server, 2), -1);
}

TEST(best_actor_returns_none_when_empty) {
    CHECK_EQ(cdtb::game::best_actor_index(nullptr, nullptr, 0), -1);
}

// 처리기는 바이트 패턴으로 찍을 수 없다 - 거의 같은 함수가 하나 더
// 있어서 40바이트까지 가야 갈리고, 그 40번째가 점프 변위라 패치에
// 밀린다. 대신 역직렬화 함수 본문에서 호출 자리를 찾는다. 파싱을
// 마치고 성공했을 때만 부르므로 "call rel32" 뒤에 "mov [rbx],0"
// 이 온다. 실측에서 본문 안에 딱 한 번 나왔다.
TEST(find_handler_call_reads_the_relative_target) {
    // E8 10 00 00 00  = call +0x10 (다음 명령 기준)
    // C7 03 00 00 00 00 = mov dword ptr [rbx], 0
    const std::uint8_t body[] = {0x90, 0x90,
                                 0xE8, 0x10, 0x00, 0x00, 0x00,
                                 0xC7, 0x03, 0x00, 0x00, 0x00, 0x00};
    std::uint64_t handler = 0;
    CHECK(cdtb::game::find_handler_call(body, sizeof(body), 0x1000, &handler));
    // 호출은 0x1002, 다음 명령은 0x1007, 대상은 0x1017
    CHECK_EQ(handler, std::uint64_t{0x1017});
}

TEST(find_handler_call_fails_without_the_marker) {
    const std::uint8_t body[] = {0xE8, 0x10, 0x00, 0x00, 0x00, 0x90, 0x90};
    std::uint64_t handler = 0;
    CHECK(!cdtb::game::find_handler_call(body, sizeof(body), 0x1000, &handler));
}

// 함수 끝을 넘어가면 옆 함수에서도 맞는다 - 실측에서 0x600 을
// 훑었더니 두 곳이 잡혀 해석이 통째로 실패했다. MSVC 는 함수 사이를
// int3 로 채우므로 그 자리에서 멈춘다.
TEST(find_handler_call_stops_at_function_padding) {
    std::vector<std::uint8_t> body = {
        0xE8, 0x10, 0x00, 0x00, 0x00,
        0xC7, 0x03, 0x00, 0x00, 0x00, 0x00,
        0xCC, 0xCC, 0xCC, 0xCC,              // 함수 끝
        0xE8, 0x20, 0x00, 0x00, 0x00,        // 옆 함수의 같은 모양
        0xC7, 0x03, 0x00, 0x00, 0x00, 0x00};
    std::uint64_t handler = 0;
    CHECK(cdtb::game::find_handler_call(body.data(), body.size(), 0x1000,
                                        &handler));
    CHECK_EQ(handler, std::uint64_t{0x1015});
}

// 두 곳에서 맞으면 고를 수 없다. 못 찾은 것으로 다룬다.
TEST(find_handler_call_fails_when_ambiguous) {
    const std::uint8_t one[] = {0xE8, 0x10, 0x00, 0x00, 0x00,
                                0xC7, 0x03, 0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> body(one, one + sizeof(one));
    body.insert(body.end(), one, one + sizeof(one));
    std::uint64_t handler = 0;
    CHECK(!cdtb::game::find_handler_call(body.data(), body.size(), 0x1000,
                                         &handler));
}

// 35개 치트가 전부 같은 문 하나를 지난다. 처리기 앞머리가 세션에서
// 이 사슬로 객체를 꺼내 가상 함수를 불러 보고, 거짓이면 조용히
// 반환한다. 그 객체를 알아야 문을 열 수 있다.
//
//   세션 -> [+0xA0] -> [+0x68] -> [+0x130]
TEST(gate_object_walks_the_chain) {
    cdtb::tests::FakeMemory m;
    m.heap.assign(0x400, 0);
    const auto session = m.heap_addr(0x000);
    const auto a = m.heap_addr(0x100);
    const auto b = m.heap_addr(0x200);
    const auto gate = m.heap_addr(0x300);
    m.put_u64(0x000 + 0xA0, a);
    m.put_u64(0x100 + 0x68, b);
    m.put_u64(0x200 + 0x130, gate);

    std::uintptr_t out = 0;
    CHECK(cdtb::game::gate_object(m, session, &out));
    CHECK_EQ(out, gate);
}

TEST(gate_object_fails_on_a_broken_chain) {
    cdtb::tests::FakeMemory m;
    m.heap.assign(0x400, 0);          // 전부 0 - 첫 칸에서 끊긴다
    std::uintptr_t out = 0;
    CHECK(!cdtb::game::gate_object(m, m.heap_addr(0), &out));
}

TEST(gate_object_fails_without_a_session) {
    cdtb::tests::FakeMemory m;
    std::uintptr_t out = 0;
    CHECK(!cdtb::game::gate_object(m, 0, &out));
}

TEST(spawn_args_reject_zero_key) {
    CHECK(!cdtb::game::spawn_args_ok(0, 1));
}

TEST(spawn_args_reject_non_positive_count) {
    CHECK(!cdtb::game::spawn_args_ok(50001, 0));
    CHECK(!cdtb::game::spawn_args_ok(50001, -1));
}

TEST(spawn_args_accept_a_real_item) {
    CHECK(cdtb::game::spawn_args_ok(50001, 1));
}

// 액터를 아직 못 봤으면 아무것도 부르지 않는다.
TEST(spawn_refuses_without_an_actor) {
    cdtb::game::SpawnOutcome out;
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    CHECK(!cdtb::game::spawn_item_to_ground(0, 50001, 1, pos, &out));
    CHECK(!out.called);
}

TEST(no_actor_before_the_hook_sees_one) {
    CHECK_EQ(cdtb::game::last_actor(), std::uintptr_t{0});
}

}  // namespace
