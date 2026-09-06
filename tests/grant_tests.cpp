#include <cstdint>
#include <cstring>
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

// 세션 표는 지워지지 않는다. 접속이 다시 맺어지면 옛 세션이 누적
// 호출 1위로 남는데 그 메모리는 이미 풀렸다 - 그것으로 구동하면
// 게임 안에서 0xC0000005 로 죽는다(실측 2026-09-06, 네 번 반복한 뒤
// 클라이언트가 메인 화면으로 떨어졌다). 그래서 최근에 본 것만 고른다.
TEST(best_live_session_skips_the_stale_winner) {
    const std::uint32_t hits[2] = {17020, 12};
    const bool server[2] = {true, true};
    const std::uint64_t last[2] = {1000, 99000};  // 0번은 옛 접속
    CHECK_EQ(cdtb::game::best_live_session_index(hits, server, last, 2, 100000,
                                                 3000),
             1);
}

TEST(best_live_session_takes_the_busiest_among_fresh) {
    const std::uint32_t hits[3] = {5, 900, 40};
    const bool server[3] = {true, true, true};
    const std::uint64_t last[3] = {99500, 99800, 99900};
    CHECK_EQ(cdtb::game::best_live_session_index(hits, server, last, 3, 100000,
                                                 3000),
             1);
}

TEST(best_live_session_ignores_never_seen) {
    // 시각 0 은 "한 번도 못 봤다" 다. 시각을 안 남긴 자리를 살아
    // 있다고 보면 안 된다.
    const std::uint32_t hits[2] = {900, 3};
    const bool server[2] = {true, true};
    const std::uint64_t last[2] = {0, 99900};
    CHECK_EQ(cdtb::game::best_live_session_index(hits, server, last, 2, 100000,
                                                 3000),
             1);
}

TEST(best_live_session_returns_none_when_all_stale) {
    const std::uint32_t hits[2] = {900, 30};
    const bool server[2] = {true, true};
    const std::uint64_t last[2] = {10, 20};
    CHECK_EQ(cdtb::game::best_live_session_index(hits, server, last, 2, 100000,
                                                 3000),
             -1);
}

TEST(best_live_session_still_skips_client_side) {
    const std::uint32_t hits[2] = {99999, 4};
    const bool server[2] = {false, true};
    const std::uint64_t last[2] = {99900, 99900};
    CHECK_EQ(cdtb::game::best_live_session_index(hits, server, last, 2, 100000,
                                                 3000),
             1);
}

TEST(best_live_session_handles_null) {
    CHECK_EQ(cdtb::game::best_live_session_index(nullptr, nullptr, nullptr, 0,
                                                 0, 3000),
             -1);
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

// 2026-09-04 업데이트: 컴파일러가 성공코드 0 을 즉시값 대신 미리 xor
// 로 0 을 만든 레지스터로 저장하기 시작했다. `mov [rbx], esi` 꼴도
// 처리기 저장으로 알아봐야 한다 - esi 가 앞서 xor 로 0 이 됐을 때만.
TEST(find_handler_call_reads_register_zero_store) {
    // 33 F6 = xor esi,esi ; E8 10.. = call ; 89 33 = mov [rbx],esi
    std::vector<std::uint8_t> body = {0x33, 0xF6,
                                      0xE8, 0x10, 0x00, 0x00, 0x00,
                                      0x89, 0x33,
                                      0x90, 0x90, 0x90};   // n>=11 패딩
    std::uint64_t handler = 0;
    CHECK(cdtb::game::find_handler_call(body.data(), body.size(), 0x1000,
                                        &handler));
    // 호출 0x1002, 다음 명령 0x1007, 대상 0x1017
    CHECK_EQ(handler, std::uint64_t{0x1017});
}

// esi 가 0 이 된 적이 없으면 성공 저장이 아니다 - 임의의
// `mov [reg], reg` 를 처리기로 오인하면 안 된다.
TEST(find_handler_call_ignores_nonzero_register_store) {
    std::vector<std::uint8_t> body = {0xE8, 0x10, 0x00, 0x00, 0x00,
                                      0x89, 0x33,            // mov [rbx],esi
                                      0x90, 0x90, 0x90, 0x90};
    std::uint64_t handler = 0;
    CHECK(!cdtb::game::find_handler_call(body.data(), body.size(), 0x1000,
                                         &handler));
}

// give 처럼 처리기 호출이 둘일 때(빠른 경로 3인자 · 스트림 경로
// 5인자), 5번째 인자를 스택으로 넘기는(`mov [rsp+0x20],reg`) 스트림
// 경로를 빼고 3인자 경로를 고른다.
TEST(find_handler_call_prefers_three_arg_over_stack_arg_path) {
    std::vector<std::uint8_t> body = {
        // 3인자 빠른 경로: 그냥 call + mov [rbx],0
        0xE8, 0x10, 0x00, 0x00, 0x00,
        0xC7, 0x03, 0x00, 0x00, 0x00, 0x00,
        // 5인자 스트림 경로: mov [rsp+0x20],rax 뒤에 call + mov [rbx],0
        0x48, 0x89, 0x44, 0x24, 0x20,
        0xE8, 0x20, 0x00, 0x00, 0x00,
        0xC7, 0x03, 0x00, 0x00, 0x00, 0x00};
    std::uint64_t handler = 0;
    CHECK(cdtb::game::find_handler_call(body.data(), body.size(), 0x1000,
                                        &handler));
    // 빠른 경로 호출 0x1000, 다음 0x1005, 대상 0x1015
    CHECK_EQ(handler, std::uint64_t{0x1015});
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

// 인벤토리 직행은 TrItemValue 구조를 넘긴다. 작업 함수(0x26A2600)가
// 검사하는 칸이 코드에 그대로 있다.
//   mov eax, [r8+8];  test eax,eax; je 실패      아이템 키
//   cmp qword [r8+0x10], 0; jle 실패             개수
TEST(item_value_puts_key_at_8_and_count_at_10) {
    std::uint8_t buf[0x200]{};
    cdtb::game::fill_item_value(buf, sizeof(buf), 50001, 7);
    std::uint32_t key = 0;
    std::int64_t count = 0;
    std::memcpy(&key, buf + 0x08, sizeof(key));
    std::memcpy(&count, buf + 0x10, sizeof(count));
    CHECK_EQ(key, std::uint32_t{50001});
    CHECK_EQ(count, std::int64_t{7});
}

// 생성자가 +0x1B4 까지 쓴다. 그보다 작게 잡았다가 스택을 넘겨 써
// 게임이 죽었다 - 버퍼가 그 아래로 내려가지 않게 못박는다.
TEST(item_value_buffer_covers_what_the_ctor_writes) {
    CHECK(cdtb::game::kItemValueSize >= cdtb::game::kItemValueMinSize);
    CHECK_EQ(cdtb::game::kItemValueMinSize, std::size_t{0x1B4});
}

TEST(item_value_refuses_a_small_buffer) {
    std::uint8_t buf[8]{};
    CHECK(!cdtb::game::fill_item_value(buf, sizeof(buf), 50001, 1));
}

// 게임 함수를 후킹 안에서 부르면 그 자리가 락을 쥐고 있을 때
// 교착한다 - 실측에서 게임 조작이 통째로 멈췄다. 안전한 자리는
// 스레드의 작업 디스패처 진입점이다. 호출 스택을 떠서 찾았다.
//
//   sub rsp,0x28
//   mov rax,[rcx+0x78]
//   mov rdx,[rax+8]
//   test rdx,rdx / je / call rdx     <- 작업 콜백
//
// 그 앞은 아직 아무 작업도 시작하지 않은 자리다.
TEST(find_task_dispatcher_returns_its_offset) {
    const std::uint8_t body[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B,
                                 0x41, 0x78, 0x48, 0x8B, 0x50, 0x08};
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(body); ++i) img[0x200 + i] = body[i];
    std::uint64_t rva = 0;
    CHECK(cdtb::game::find_task_dispatcher_rva(img, &rva));
    CHECK_EQ(rva, 0x200u);
}

TEST(find_task_dispatcher_fails_when_absent) {
    const std::vector<std::uint8_t> img(4096, 0xCC);
    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_task_dispatcher_rva(img, &rva));
}

// 메시지 펌프. 디스패처 자리는 TLS 가 서 있지 않아 요청이 실행되지
// 않았고, 액터 조회 자리는 게임 코드 한복판이라 교착 위험이 있다.
// 펌프가 돌아온 자리는 그 틱의 메시지를 전부 처리한 뒤이고 작업
// 컨텍스트(TLS+0x250)는 아직 서 있다. 앞머리 40바이트로 찾는다.
namespace {
const std::uint8_t kPumpHead[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68,
    0x18, 0x48, 0x89, 0x70, 0x20, 0x48, 0x89, 0x48, 0x08, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x50, 0x4D, 0x8B, 0xF9, 0x4D, 0x8B, 0xE0, 0x4C, 0x8B};
}  // namespace

TEST(find_message_pump_returns_its_offset) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(kPumpHead); ++i) {
        img[0x300 + i] = kPumpHead[i];
    }
    std::uint64_t rva = 0;
    CHECK(cdtb::game::find_message_pump_rva(img, &rva));
    CHECK_EQ(rva, 0x300u);
}

// 24바이트 프롤로그는 이미지에 10곳 있다. 그 길이만 같은 것은 잡지
// 않아야 한다 - 두 곳이 맞으면 실패로 다룬다.
TEST(find_message_pump_rejects_a_second_match) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(kPumpHead); ++i) {
        img[0x300 + i] = kPumpHead[i];
        img[0x800 + i] = kPumpHead[i];
    }
    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_message_pump_rva(img, &rva));
}

TEST(find_message_pump_fails_when_absent) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    // 앞 24바이트만 같은 다른 함수.
    for (std::size_t i = 0; i < 24; ++i) img[0x300 + i] = kPumpHead[i];
    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_message_pump_rva(img, &rva));
}

// 작업 실행 래퍼. 앞 24바이트(흔한 프롤로그 + gs:[0x58])는 이미지에
// 15곳이라, 함수 고유 바이트(mov rbx,rcx; mov [rcx+0x70],1; mov
// rsi,[rax])까지 34바이트로 유일하다.
namespace {
const std::uint8_t kTaskRunHead[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00,
    0x00, 0x00, 0x48, 0x89, 0xCB, 0xC6, 0x41, 0x70, 0x01, 0x48, 0x8B,
    0x30};
}  // namespace

TEST(find_task_run_returns_its_offset) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(kTaskRunHead); ++i) {
        img[0x400 + i] = kTaskRunHead[i];
    }
    std::uint64_t rva = 0;
    CHECK(cdtb::game::find_task_run_rva(img, &rva));
    CHECK_EQ(rva, 0x400u);
}

// 앞 24바이트만 같은 함수가 여럿 있어도(실제 이미지에 15곳) 그것만으로
// 잡지 않아야 한다.
TEST(find_task_run_ignores_the_common_prologue) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(kTaskRunHead); ++i) {
        img[0x400 + i] = kTaskRunHead[i];
    }
    // 앞 24바이트만 같은 다른 함수.
    for (std::size_t i = 0; i < 24; ++i) img[0x900 + i] = kTaskRunHead[i];
    std::uint64_t rva = 0;
    CHECK(cdtb::game::find_task_run_rva(img, &rva));
    CHECK_EQ(rva, 0x400u);
}

TEST(find_task_run_rejects_a_second_full_match) {
    std::vector<std::uint8_t> img(4096, 0xCC);
    for (std::size_t i = 0; i < sizeof(kTaskRunHead); ++i) {
        img[0x400 + i] = kTaskRunHead[i];
        img[0x900 + i] = kTaskRunHead[i];
    }
    std::uint64_t rva = 0;
    CHECK(!cdtb::game::find_task_run_rva(img, &rva));
}

// 능력치·처치 치트는 대상 엔티티 ID 를 페이로드로 받는다. 그 ID 를
// 엔티티로 바꾸는 함수는 앞머리가 세 곳에서 겹쳐 바이트 패턴으로
// 찍을 수 없다. 대신 처리기 본문에서 호출 자리를 찾는다.
//
//   44 8B 03           mov r8d, dword ptr [rbx]   대상 ID
//   48 8D 54 24 60     lea rdx, [rsp+0x60]
//   48 8B 08           mov rcx, qword ptr [rax]
//   E8 rel32           call 엔티티 조회
//
// 실측에서 처리기 본문(1964바이트) 안에 딱 한 곳이었다.
TEST(find_entity_lookup_reads_the_relative_target) {
    std::vector<std::uint8_t> body = {
        0x90, 0x90,
        0x44, 0x8B, 0x03, 0x48, 0x8D, 0x54, 0x24, 0x60, 0x48, 0x8B, 0x08,
        0xE8, 0x10, 0x00, 0x00, 0x00};
    std::uint64_t fn = 0;
    CHECK(cdtb::game::find_entity_lookup(body.data(), body.size(), 0x1000,
                                         &fn));
    // call 은 0x100D, 다음 명령은 0x1012, 대상은 0x1022
    CHECK_EQ(fn, std::uint64_t{0x1022});
}

TEST(find_entity_lookup_fails_without_the_anchor) {
    const std::vector<std::uint8_t> body(64, 0x90);
    std::uint64_t fn = 0;
    CHECK(!cdtb::game::find_entity_lookup(body.data(), body.size(), 0x1000,
                                          &fn));
}

// 세션이 없으면 요청 자체를 걸지 않는다.
TEST(request_refuses_without_a_session) {
    const float pos[3] = {0.0f, 0.0f, 0.0f};
    CHECK(!cdtb::game::request_spawn(0, 50001, 1, pos));
    CHECK(!cdtb::game::spawn_pending());
}

// 렌더 스레드에서 직접 부르면 죽는다 - 작업 함수 안쪽이 TLS 를 쓰는데
// 그 블록이 없다. 그래서 부르기 전에 이 검사를 한다.
//
// **반환값을 못 박지 않는다.** 검사는 TEB 슬롯 0 에서 `+0x250` 을
// 따라가는데, 테스트 프로세스의 CRT 블록이 그 자리에 우연히 값을
// 들고 있으면 true 가 나온다. 실제로 그렇게 뒤집혔다 - 번역 단위를
// 하나 더한 것만으로 false 에서 true 가 됐다. "테스트 스레드에는
// 당연히 없다" 는 전제가 틀렸다.
//
// 여기서 지킬 것은 둘이다. 아무 스레드에서 불러도 죽지 않을 것
// (safe_deref 가 널을 걸러야 한다), 그리고 읽기만 하므로 두 번 불러
// 같은 답일 것.
TEST(thread_ready_for_spawn_is_safe_and_stable_on_any_thread) {
    const bool first = cdtb::game::thread_ready_for_spawn();
    CHECK_EQ(cdtb::game::thread_ready_for_spawn(), first);
}

TEST(no_actor_before_the_hook_sees_one) {
    CHECK_EQ(cdtb::game::last_actor(), std::uintptr_t{0});
}

}  // namespace

// -------------------------------------------------------- 담금질 싣기

// TrItemValue +0x0C 가 u16 담금질이다. 변환 함수(RVA 0x2094050)가
// `movzx eax,word [r14+0x0C]; mov [rdi+0x0A],ax` 로 레코드에 옮긴다
// (docs/superpowers/specs/2026-09-02-inventory.md).
//
// 지금까지 지급이 담금질 0 인 장비만 준 것은 이 칸을 안 채웠기
// 때문이다 - 생성자는 +0x08 만 0 으로 만들고 +0x0C 는 건드리지 않는데
// 버퍼가 0 으로 초기화된다.

TEST(fill_item_value_writes_the_temper_at_0x0C) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.temper = 3;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    std::uint16_t temper = 0;
    std::memcpy(&temper, buf + 0x0C, sizeof(temper));
    CHECK_EQ(temper, std::uint16_t{3});
}

TEST(fill_item_value_leaves_the_temper_zero_by_default) {
    // 옛 호출자가 그대로 동작해야 한다.
    std::uint8_t buf[0x200]{};
    std::memset(buf, 0xAB, sizeof(buf));
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 50001, 7));
    std::uint16_t temper = 0xFFFF;
    std::memcpy(&temper, buf + 0x0C, sizeof(temper));
    CHECK_EQ(temper, std::uint16_t{0});
}

TEST(fill_item_value_keeps_the_key_and_count) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.temper = 2;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 5, ex));
    std::uint32_t key = 0;
    std::int64_t count = 0;
    std::memcpy(&key, buf + 0x08, sizeof(key));
    std::memcpy(&count, buf + 0x10, sizeof(count));
    CHECK_EQ(key, std::uint32_t{200914});
    CHECK_EQ(count, std::int64_t{5});
}

TEST(fill_item_value_refuses_a_buffer_too_small_for_the_sockets) {
    // 소켓 개수 칸이 +0x5E 다. 지금은 예리도(+0x1AE)까지 쓰므로
    // 하한이 더 높지만, 이 크기들은 그 아래라 여전히 거절한다.
    std::uint8_t buf[0x10]{};
    CHECK(!cdtb::game::fill_item_value(buf, sizeof(buf), 50001, 1));
    std::uint8_t half[0x40]{};
    CHECK(!cdtb::game::fill_item_value(half, sizeof(half), 50001, 1));
}

// --- 소켓 (RVA 0x2094324) ----------------------------------------------
//
// 변환 함수의 복사 루프가 TrItemValue +0x40 부터 6바이트씩 다섯 칸을
// 레코드 소켓 배열로 그대로 옮기고, 개수는 +0x5E (u8) 에서 읽는다.
//
//   movzx ebx, byte [r14+0x5E]        개수
//   mov   byte [rdi+0x70], bl         레코드 +0x70 에 그대로
//   ecx = dword [r14+0x40+r9]         6바이트를 그대로
//   dx  = word  [r14+0x44+r9]
//   byte [rax+r9+4] = r8b             다섯 번째 바이트만 슬롯 번호로 덮어쓴다
//
// 지금까지 지급분에 소켓이 하나도 없던 것은 +0x5E 가 0 이라 루프가
// 한 칸도 안 돌았기 때문이다.

TEST(fill_item_value_writes_the_socket_count_at_0x5E) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.socket_count = 2;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    CHECK_EQ(buf[0x5E], std::uint8_t{2});
}

TEST(fill_item_value_copies_socket_bytes_verbatim_from_0x40) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.socket_count = 2;
    const std::uint8_t a[6] = {0x24, 0x0D, 0xFF, 0xFF, 0x00, 0xFF};
    const std::uint8_t b[6] = {0x8E, 0x0C, 0xFF, 0xFF, 0x01, 0xFF};
    std::memcpy(ex.sockets[0].raw, a, 6);
    std::memcpy(ex.sockets[1].raw, b, 6);
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    CHECK(std::memcmp(buf + 0x40, a, 6) == 0);
    CHECK(std::memcmp(buf + 0x46, b, 6) == 0);
}

TEST(fill_item_value_leaves_unused_socket_slots_alone) {
    // 남은 칸은 게임 생성자가 채운 빈 값(FF FF 00 00 FF)이라야 한다.
    // 우리가 0 으로 밀면 안 된다.
    std::uint8_t buf[0x200]{};
    for (int i = 0; i < 5; ++i) {
        std::uint8_t* e = buf + 0x40 + i * 6;
        e[0] = 0xFF; e[1] = 0xFF; e[2] = 0; e[3] = 0; e[4] = 0xFF; e[5] = 0;
    }
    cdtb::game::GiveExtras ex;
    ex.socket_count = 1;
    ex.sockets[0].raw[0] = 0x24;
    ex.sockets[0].raw[1] = 0x0D;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    const std::uint8_t empty[6] = {0xFF, 0xFF, 0, 0, 0xFF, 0};
    for (int i = 1; i < 5; ++i) {
        CHECK(std::memcmp(buf + 0x40 + i * 6, empty, 6) == 0);
    }
}

TEST(fill_item_value_refuses_more_sockets_than_the_array_holds) {
    // 배열은 다섯 칸이다. 넘겨 보내면 게임이 배열 밖을 읽는다.
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.socket_count = 6;
    CHECK(!cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
}

TEST(fill_item_value_writes_no_sockets_by_default) {
    std::uint8_t buf[0x200]{};
    std::memset(buf, 0xAB, sizeof(buf));
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 50001, 1));
    CHECK_EQ(buf[0x5E], std::uint8_t{0});
    // 소켓 칸은 건드리지 않는다.
    for (std::size_t i = 0x40; i < 0x5E; ++i) {
        CHECK_EQ(buf[i], std::uint8_t{0xAB});
    }
}

// --- 현재 내구도 (변환 함수 0x2094050) ---------------------------------
//
//   movzx eax, word [r14+0x2A]
//   mov   word [rdi+0x40], ax        레코드 +0x40 = 현재 내구도
//
// 안 채우면 0 으로 간다. 실측에서 미로숲의 한손검(최대 30)을 그렇게
// 줬더니 툴팁이 `0/30` 을 빨갛게 내고 공격력에 -11 이 붙었다. 이
// 게임은 아무 아이템도 수리 데이터가 없어(0 / 6,810) 되돌릴 수 없다.

TEST(fill_item_value_writes_the_endurance_at_0x2A) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.endurance = 30;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 1002261, 1, ex));
    std::uint16_t endu = 0;
    std::memcpy(&endu, buf + 0x2A, sizeof(endu));
    CHECK_EQ(endu, std::uint16_t{30});
}

TEST(fill_item_value_leaves_the_endurance_zero_by_default) {
    // 내구도가 없는 아이템은 0 이어야 한다 - 게임이 나중에 0xFFFF 로
    // 채운다. 옛 호출자도 그대로 동작해야 한다.
    std::uint8_t buf[0x200]{};
    std::memset(buf, 0xAB, sizeof(buf));
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1));
    std::uint16_t endu = 0xFFFF;
    std::memcpy(&endu, buf + 0x2A, sizeof(endu));
    CHECK_EQ(endu, std::uint16_t{0});
}

TEST(fill_item_value_keeps_endurance_and_sockets_apart) {
    // +0x2A 와 소켓(+0x40) 은 붙어 있지 않다. 한쪽이 다른 쪽을
    // 덮으면 소켓 첫 칸이 망가진다.
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.endurance = 0x1234;
    ex.socket_count = 1;
    ex.sockets[0].raw[0] = 0x24;
    ex.sockets[0].raw[1] = 0x0D;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    CHECK_EQ(buf[0x2A], std::uint8_t{0x34});
    CHECK_EQ(buf[0x2B], std::uint8_t{0x12});
    CHECK_EQ(buf[0x40], std::uint8_t{0x24});
    CHECK_EQ(buf[0x41], std::uint8_t{0x0D});
}

// --- 예리도 (변환 함수 0x2094050) --------------------------------------
//
//   movsx ebx, word [r14+0x1AE]      TrItemValue 의 값
//   call  0x317900                   아이템 표
//   movsx ecx, word [rax+0x2E8]      _SharpnessData 의 상한 (실측 100)
//   cmp   ebx, ecx / cmovl / cmovs   min(값, 상한), 음수면 0
//   mov   word [rdi+0x58], cx        레코드 +0x58
//
// 게임이 상한으로 자르므로 넘겨 보내도 거절하지는 않는다. 그래도
// 화면에서 자른 값과 보내는 값이 갈리지 않게 부르는 쪽에서 자른다.

TEST(fill_item_value_writes_the_sharpness_at_0x1AE) {
    std::uint8_t buf[0x200]{};
    cdtb::game::GiveExtras ex;
    ex.sharpness = 100;
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1, ex));
    std::uint16_t sharp = 0;
    std::memcpy(&sharp, buf + 0x1AE, sizeof(sharp));
    CHECK_EQ(sharp, std::uint16_t{100});
}

TEST(fill_item_value_leaves_the_sharpness_zero_by_default) {
    // 인벤토리 507개가 전부 0 이다. 기본값을 최대치로 줄 근거가 없다.
    std::uint8_t buf[0x200]{};
    std::memset(buf, 0xAB, sizeof(buf));
    CHECK(cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1));
    std::uint16_t sharp = 0xFFFF;
    std::memcpy(&sharp, buf + 0x1AE, sizeof(sharp));
    CHECK_EQ(sharp, std::uint16_t{0});
}

TEST(fill_item_value_refuses_a_buffer_that_stops_before_the_sharpness) {
    // 예리도 칸이 +0x1AE 라 그만큼은 있어야 한다. 소켓만 보고 잡은
    // 옛 하한(0x60)으로는 버퍼 밖에 쓴다.
    std::uint8_t buf[0x100]{};
    CHECK(!cdtb::game::fill_item_value(buf, sizeof(buf), 200914, 1));
}

// --- 개수 자르기 (최대 스택) -------------------------------------------
//
// 아이템 표의 `_maxStackCount` 는 u32 다. int 로 좁혀 견주면 큰 값이
// 음수가 되고, 그러면 개수가 음수로 못박혀 화면에서 고칠 수도 없다.
// 실측: 캠프 목재(키 13)가 3,800,301,568 이고 int 로는 -494,665,728
// 이라 지급 칸이 그 값에서 안 움직였다.

TEST(clamp_count_keeps_a_value_under_the_stack) {
    CHECK_EQ(cdtb::game::clamp_count_to_stack(5, 10), 5);
    CHECK_EQ(cdtb::game::clamp_count_to_stack(10, 10), 10);
}

TEST(clamp_count_cuts_a_value_over_the_stack) {
    CHECK_EQ(cdtb::game::clamp_count_to_stack(20, 10), 10);
}

TEST(clamp_count_survives_a_stack_bigger_than_int) {
    // 이것이 버그였다. 음수가 나오면 안 된다.
    CHECK_EQ(cdtb::game::clamp_count_to_stack(5, 3800301568u), 5);
    CHECK_EQ(cdtb::game::clamp_count_to_stack(2000000000, 3800301568u),
             2000000000);
    CHECK_EQ(cdtb::game::clamp_count_to_stack(1, 0xFFFFFFFFu), 1);
}

TEST(clamp_count_treats_zero_stack_as_no_limit) {
    // 표가 0 인 아이템이 있다. 자를 근거가 없으니 그대로 둔다.
    CHECK_EQ(cdtb::game::clamp_count_to_stack(999, 0), 999);
}

TEST(clamp_count_never_goes_below_one) {
    CHECK_EQ(cdtb::game::clamp_count_to_stack(0, 10), 1);
    CHECK_EQ(cdtb::game::clamp_count_to_stack(-494665728, 3800301568u), 1);
}
