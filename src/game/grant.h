#pragma once

#include <cstdint>
#include <vector>

#include "mem/reader.h"
#include "mem/rtti.h"

namespace cdtb::game {

// 출시 빌드에 남은 개발자 치트 경로를 쓴다. 인벤토리 구조를 몰라도
// 되고 서버가 자기 경로로 처리하므로 슬롯 배치·저장·UI 갱신이 전부
// 딸려 온다. 자세한 추적 과정은
// docs/superpowers/specs/2026-09-01-cheat-messages.md 참조.

// 세션에서 플레이어 액터를 꺼내는 게임 함수를 찾는다. 이미지 안에서
// 유일하게 맞아야 한다 - 두 곳에서 맞으면 실패로 다룬다. 엉뚱한
// 함수를 후킹하면 게임이 죽는다.
bool find_actor_getter_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out);

// 바닥에 아이템을 떨구는 실제 작업 함수를 찾는다.
bool find_spawn_ground_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out);

// 액터 조회 함수를 후킹한다. 그 함수는 게임 안에서 647곳이 부르므로
// 가만 두어도 곧 값이 들어온다 - 우리가 세션에서 액터를 꺼내는
// 복잡한 경로를 흉내 낼 필요가 없다. 훅은 값을 적어 두기만 하고
// 원본을 그대로 부른다.
bool actor_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
void actor_hook_remove();
bool actor_hook_installed();

// 본 적 있으면 그 자리의 횟수를 늘리고, 처음이면 자리를 잡는다.
// 자리가 없으면 그대로 둔다. 돌려주는 것은 개수다.
int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value);

// 조회 함수가 지금까지 돌려준 서로 다른 값들과 각각의 호출 횟수.
// 클라이언트 쪽과 서버 쪽 인벤토리 컴포넌트가 둘 다 나오고, 서버
// 쪽만 해도 여럿이다(NPC·상자 등). 플레이어 것은 게임플레이 코드가
// 계속 부르므로 횟수가 압도적으로 많다 - 그걸로 가린다.
int seen_actors(std::uintptr_t* out, std::uint32_t* hits_out, int cap);

// 마지막으로 본 액터. 아직 못 봤으면 0.
std::uintptr_t last_actor();

// 조회 함수에 들어간 세션들. 액터가 아니라 이쪽이 필요하다 - 세션을
// 처리기에 넘기면 게임이 자기 경로로 액터를 찾는다. 우리가 인벤토리
// 컴포넌트를 고를 일이 없어진다.
int seen_sessions(std::uintptr_t* out, std::uint32_t* hits_out, int cap);

// 치트 메시지 하나를 해석한다. 주소는 하나도 박지 않는다.
//   1. RTTI 로 클래스의 vtable 을 얻고
//   2. 정적 초기화가 그 vtable 을 넣는 전역이 곧 메시지 서술자이며
//   3. vtable[2] 가 역직렬화 함수이고
//   4. 그 본문의 처리기 호출을 찾는다
struct CheatMessage {
    std::uintptr_t descriptor = 0;
    std::uintptr_t handler = 0;
    std::uint32_t id = 0;
};
bool resolve_cheat_message(const mem::Rtti& rtti, const mem::Reader& reader,
                           const char* class_name, CheatMessage* out);

// 서버 쪽 후보 중 호출이 가장 많은 자리. 없으면 -1.
//
// 치트는 Req(클라이언트->서버) 라 서버 쪽이어야 한다. 서버 쪽만 해도
// NPC·상자 등 여럿이지만, 플레이어 것은 게임플레이 코드가 계속
// 부르므로 횟수가 압도적이다 - 실측에서 17020회 대 1110회 대 1~3회.
int best_actor_index(const std::uint32_t* hits, const bool* is_server, int n);

// 세션마다 게임이 돌려준 액터와 그 클래스. 프레임마다 RTTI 를 푸는
// 것은 비싸므로 분석 스레드가 한 번 붙여 준다.
//
// 세션에 따라 클라이언트 쪽이 나오기도 하고 서버 쪽이 나오기도 한다
// (조회 함수 안에 종류 바이트로 갈리는 분기가 있다). 실제 작업
// 함수는 서버 쪽 코드라 클라이언트 쪽을 넘기면 죽는다 - 실측에서
// 0xC0000005 로 죽었다. 그래서 어느 쪽이 나오는지를 봐야 한다.
std::uintptr_t session_actor(int index);
void set_session_class(int index, const char* name);
const char* session_class(int index);
bool session_is_server(int index);

// 역직렬화 함수 본문에서 처리기 호출 자리를 찾는다. 파싱을 마치고
// 성공했을 때만 부르므로 "call rel32" 뒤에 "mov dword ptr [rbx],0"
// 이 온다. 딱 한 곳에서 맞아야 한다.
bool find_handler_call(const std::uint8_t* body, std::size_t n,
                       std::uint64_t body_rva, std::uint64_t* handler_rva);

// 부르기 전에 게임이 하는 검사를 우리도 한다. 게임 코드에 그대로
// 있다 - 키가 0이거나 개수가 0 이하면 게임이 실패로 돌려준다.
bool spawn_args_ok(std::uint32_t item_key, std::int64_t count);

struct SpawnOutcome {
    bool called = false;        // 게임 함수를 실제로 불렀는가
    bool crashed = false;       // 부르다 예외가 났는가
    bool no_actor = false;      // 세션에서 액터가 안 나왔는가
    std::uintptr_t actor = 0;   // 게임이 그 세션으로 찾아 준 액터
    std::uint32_t seh = 0;      // 예외 코드
    std::uint32_t result = 0;   // 게임이 낸 코드. 0 이면 성공
};

// 아이템을 발밑 바닥에 떨군다. 인벤토리에서 버리기와 같은 루틴이라
// 게임이 평소에도 도는 경로다.
//
// **반드시 게임 스레드에서 불러야 한다.** 렌더 훅이 그 스레드다.
//
// 잘못된 대상으로 부르면 게임 안에서 죽는다 - 실측에서 0xC0000005
// 가 났고 오버레이가 통째로 내려갔다. 예외를 안에서 막고 결과로
// 돌려준다. 돌려주는 값은 "부를 조건이 됐는가" 다.
bool spawn_item_to_ground(std::uintptr_t session, std::uint32_t item_key,
                          std::int64_t count, const float pos[3],
                          SpawnOutcome* out);

// 바닥 스폰 메시지를 해석해 둔다.
bool spawn_resolve_message(const mem::Rtti& rtti, const mem::Reader& reader);
const CheatMessage& spawn_message();

// 실제 작업 함수를 찾아 둔다.
bool spawn_resolve(const mem::Rtti& rtti, const mem::Reader& reader);

// 부를 준비가 됐는가.
bool spawn_ready();

}  // namespace cdtb::game
