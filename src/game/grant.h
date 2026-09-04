#pragma once

#include <cstddef>
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

// 스레드의 작업 디스패처. 여기 진입점이 안전한 실행 지점이다.
//
// 호출 스택을 떠서 찾았다 - 스레드 본체가 이 함수를 부르고, 이
// 함수가 작업 콜백을 부른다. 그 앞은 스택이 얕고 아직 아무 작업도
// 시작하지 않아 락을 쥐고 있지 않다.
bool find_task_dispatcher_rva(const std::vector<std::uint8_t>& image,
                              std::uint64_t* rva_out);

// 작업 디스패처를 후킹해 걸어 둔 요청을 거기서 실행한다.
bool tick_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
void tick_hook_remove();
bool tick_hook_installed();

// 메시지 펌프. 게임 로직 작업의 콜백이 부르는 함수로, 세션 큐에서
// 메시지를 하나씩 꺼내 역직렬화·처리기까지 돌린다(호출 스택
// [10]~[6]). 이 함수가 **돌아온 자리**가 프레임 경계다 - 그 틱의
// 메시지를 전부 처리했고, 처리기가 쥐던 락은 놓였으며, 작업
// 실행기가 세운 TLS+0x250(현재 작업 컨텍스트)은 아직 서 있다.
// 디스패처 자리와 달리 TLS 가 서 있고, 액터 조회 자리와 달리 게임
// 코드 한복판이 아니다.
//
// 앞머리 40바이트로 찾는다. 24바이트는 10곳, 32바이트는 2곳이
// 겹치고 40바이트부터 유일하다.
bool find_message_pump_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out);
bool pump_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
void pump_hook_remove();
bool pump_hook_installed();

// 작업 실행 래퍼 (RVA 0xB1D2DE0). 작업 본체 직전에 불려 TLS+0x250 에
// 현재 작업 컨텍스트를 세운다(0xB1D2E26). 펌프·디스패처와 달리 특정
// 메시지가 아니라 **모든 작업**을 감싸므로 매 프레임 후보다. 실행
// 지점으로 쓰기 전에 이 계측으로 호출 빈도·스레드 분포부터 잰다 -
// 펌프처럼 안 도는 함수인지 먼저 확인한다(2026-09-04 규율).
//
// 앞머리 24바이트는 이미지에 15곳이라 부족하다. 함수 고유 바이트까지
// 34바이트로 유일하다.
bool find_task_run_rva(const std::vector<std::uint8_t>& image,
                       std::uint64_t* rva_out);
bool taskrun_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
void taskrun_hook_remove();
bool taskrun_hook_installed();

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

// 치트의 문. 35개 처리기가 전부 세션에서 이 사슬로 같은 객체를
// 꺼내 가상 함수를 불러 보고, 거짓이면 조용히 반환한다.
//
//   세션 -> [+0xA0] -> [+0x68] -> [+0x130]
//
// 무리마다 슬롯이 다르다 - 대부분 +0xD0, 아이템 계열 +0x140,
// 내구도·AI +0x160. 이 문 하나를 열면 전부 열린다.
bool gate_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out);

// 문 객체의 클래스와 문제의 가상 함수들을 로그로 남긴다. 읽기만 한다.
void log_gate(const mem::Rtti& rtti, const mem::Reader& reader,
              std::uintptr_t session);

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

// 능력치·처치 치트는 대상 엔티티 ID 를 페이로드로 받는다. 그 ID 를
// 엔티티로 바꾸는 함수는 앞머리가 세 곳에서 겹쳐 바이트 패턴으로
// 찍을 수 없다. 처리기 본문에서 호출 자리를 찾는다.
//
//   44 8B 03 / 48 8D 54 24 60 / 48 8B 08 / E8 rel32
bool find_entity_lookup(const std::uint8_t* body, std::size_t n,
                        std::uint64_t body_rva, std::uint64_t* fn_rva);

// 엔티티 조회를 후킹해 지나가는 ID 를 모은다. 플레이어 것은 게임이
// 계속 조회하므로 횟수로 가린다 - 세션 때와 같은 수법이다.
bool entity_hook_install(const mem::Rtti& rtti, const mem::Reader& reader);
int seen_entities(std::uint32_t* out, std::uint32_t* hits_out, int cap);

// 표 조회 함수를 후킹해 "이 키를 어느 표에서 찾는지" 를 잡는다.
//
// 인벤토리 레코드 +0x08 의 값(5915 등)은 아이템 표에도 현지화에도
// 없다. 별도 표가 있다는 뜻인데, 같은 조회 함수를 쓰는 표가 82개라
// 눈으로 고를 수 없다. UI 가 이름을 그릴 때 반드시 조회하므로
// 그 순간을 잡는다.
//
// 찾는 키가 들어올 때만 남긴다 - 이 함수는 아주 자주 불린다.
bool table_probe_install(const mem::Rtti& rtti, const mem::Reader& reader,
                         std::uint32_t watch_key);

// 부르기 전에 게임이 하는 검사를 우리도 한다. 게임 코드에 그대로
// 있다 - 키가 0이거나 개수가 0 이하면 게임이 실패로 돌려준다.
bool spawn_args_ok(std::uint32_t item_key, std::int64_t count);

// 지급 칸의 개수를 아이템의 최대 스택으로 자른다. 1 아래로는 안
// 내려간다.
//
// `max_stack` 은 아이템 표의 u32 다. **int 로 좁혀 견주면 안 된다** -
// 실측에서 캠프 목재(키 13)가 3,800,301,568 이라 int 로는
// -494,665,728 이 되고, 개수가 그 음수로 못박혀 화면에서 고칠 수도
// 없었다. 넘겨서 자를 일이 있으려면 max_stack 이 개수(int)보다
// 작아야 하므로, 그때의 형변환은 안전하다.
//
// `max_stack` 이 0 인 아이템이 있다. 자를 근거가 없으니 그대로 둔다.
int clamp_count_to_stack(int count, std::uint32_t max_stack);

struct SpawnOutcome {
    bool called = false;        // 게임 함수를 실제로 불렀는가
    bool crashed = false;       // 부르다 예외가 났는가
    bool no_actor = false;      // 세션에서 액터가 안 나왔는가
    std::uintptr_t actor = 0;   // 게임이 그 세션으로 찾아 준 액터
    std::uint32_t seh = 0;      // 예외 코드
    std::uintptr_t fault = 0;   // 터진 주소
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
// 지금 이 스레드가 그 작업을 할 수 있는가.
//
// 작업 함수 안쪽이 TLS 를 쓴다 - gs:[0x58] 의 배열에서 슬롯을 꺼내
// 거기에 쓴다. 렌더 스레드에는 그 블록이 없어서 널을 참조하고 죽는다.
// 실측에서 RVA 0x25493D2 가 정확히 그 자리였다.
bool thread_ready_for_spawn();

// 지급에 실을 것들. 전부 `TrItemValue` 의 칸이다.
//
// 변환 함수(RVA 0x2094050)가 이 구조를 인벤토리 레코드로 옮긴다.
// 무엇이 실리고 무엇이 안 실리는지는 그 함수에 그대로 있다 -
// docs/superpowers/specs/2026-09-03-static-analysis.md 에 표로 옮겼다.
inline constexpr std::size_t kGiveSocketBytes = 6;
inline constexpr int kGiveMaxSockets = 5;

// 소켓 한 칸. 인벤토리에서 읽은 6바이트를 그대로 넘긴다.
//
// 게임의 복사 루프(0x2094324)가 `TrItemValue +0x40` 부터 6바이트씩
// 다섯 칸을 레코드 소켓 배열로 **그대로** 옮긴다. 다섯 번째 바이트만
// 슬롯 번호로 덮어쓰므로 우리가 맞출 필요가 없다.
//
// 첫 u16 은 아이템 표에서의 **순번**이지 아이템 키가 아니다 - 복사
// 루프는 키->순번 변환을 하지 않는다. 그래서 게임이 갱신되면
// 어긋난다. 보관함 파일은 키도 함께 들고 있으므로 나중에 대응표를
// 물리면 고칠 수 있다.
struct GiveSocket {
    std::uint8_t raw[kGiveSocketBytes]{};
};

struct GiveExtras {
    std::uint16_t temper = 0;    // +0x0C  -> 레코드 +0x0A

    // +0x2A (u16) -> 레코드 +0x40 = **현재 내구도**.
    //
    // 안 채우면 0 으로 간다. 내구도가 있는 아이템(표 `_maxEndurance`
    // != 0xFFFF)을 그렇게 주면 툴팁에 `0/30` 이 빨갛게 뜨고 공격력에
    // 벌점이 붙는다 - 실측에서 미로숲의 한손검이 `0/30`, 공격력
    // -11 이었다. 게다가 이 게임은 아무 아이템도 수리 데이터가 없어
    // (0 / 6,810) 되돌릴 방법이 없다.
    //
    // 내구도가 없는 아이템은 0 으로 두면 된다. 게임이 저장 · 재시작을
    // 거치며 0xFFFF 로 채운다.
    std::uint16_t endurance = 0;

    // +0x1AE (i16) -> 레코드 +0x58 = **장비 연마**. 게임이 아이템 표의
    // `_SharpnessData`(+0x2E8, 실측 100) 로 자른다.
    //
    // 기본값은 0 이다. 인벤토리 507개가 전부 0 이라 최대치를 줄
    // 근거가 없다 - 내구도와 다르다.
    std::uint16_t sharpness = 0;

    // +0x5E (u8) -> 레코드 +0x70. 개수가 0 이면 복사 루프가 한 칸도
    // 안 돈다 - 지금까지 지급분에 소켓이 하나도 없던 이유가 이것이다.
    //
    // **상한을 넘기면 게임이 조용히 거절한다.** 장비는 아이템 표의
    // `max_sockets` 이하, 겹치는 아이템은 0 이어야 한다. 부르는 쪽에서
    // 미리 잘라야 큐가 멈추지 않는다.
    std::uint8_t socket_count = 0;
    GiveSocket sockets[kGiveMaxSockets]{};   // +0x40 .. +0x5D
};

// TrItemValue 의 칸을 채운다. 나머지는 게임 생성자가 채우므로
// 여기서는 건드리지 않는다.
//
// 작업 함수(0x26A2600)가 검사하는 자리가 코드에 그대로 있다.
//   mov eax, [r8+8]; test eax,eax; je 실패        아이템 키
//   cmp qword [r8+0x10], 0; jle 실패              개수
//   cmp byte [r8+0x5e], 0 / 표 +0x238 과 비교      소켓 개수
//
// 담금질은 `+0x0C` 의 u16 이다. 변환 함수(0x2094050)가
// `movzx eax,word [r14+0x0C]; mov [rdi+0x0A],ax` 로 레코드에 옮긴다.
// 지급이 담금질 0 인 장비만 준 것은 이 칸을 안 채웠기 때문이다 -
// 생성자는 `+0x08` 만 0 으로 만들고 `+0x0C` 는 건드리지 않는데
// 버퍼가 0 으로 초기화된다. 소켓 개수도 같은 이유로 0 이었다.
//
// **상한을 넘으면 게임이 조용히 거절한다.** 담금질은
// `ItemCatalogEntry::max_temper`, 소켓은 `max_sockets` 로 미리
// 걸러야 한다.
bool fill_item_value(void* buf, std::size_t n, std::uint32_t item_key,
                     std::int64_t count, const GiveExtras& extras = {});

// TrItemValue 를 담을 버퍼 크기.
//
// 생성자(RVA 0x20CE900)가 실제로 어디까지 쓰는지 디스어셈블해서
// 쟀다 - +0x1A4 에 8바이트, +0x1B0 에 4바이트까지 쓴다. 즉 최소
// 0x1B4 가 필요하다.
//
// 처음에 0x100 으로 잡았다가 생성자가 스택을 180바이트 넘겨 써서
// 게임이 나중에 죽었다. 여유를 두고 잡는다.
constexpr std::size_t kItemValueMinSize = 0x1B4;
constexpr std::size_t kItemValueSize = 0x400;

// 바닥이 아니라 인벤토리로 바로 넣는다.
// (CreateItemFromTrItemValueCheatReq, ID 2944)
//
// `extras` 는 담금질과 소켓이다. 아이템 표의 상한을 넘으면 게임이
// 조용히 거절하므로 부르는 쪽에서 먼저 걸러야 한다.
bool request_give(std::uintptr_t session, std::uint32_t item_key,
                  std::int64_t count, const GiveExtras& extras = {});

// 인벤토리 직행을 쓸 수 있는가.
bool give_ready();

// 아이템 내구도를 바꾼다 (VaryEnduranceItemByCheatReq, ID 2736).
//
// 남은 치트 중 인자가 가장 단순하다 - u16 둘뿐이고, 문도 관리자
// 사슬이 아니라 세션 자체의 vtable +0x160 이다.
//
//   rcx 서술자  rdx 패킷  r8 &u16  r9 &u16
//
// 두 칸의 뜻은 아직 모른다. 값을 바꿔 가며 화면으로 확인해야 한다.
bool request_endurance(std::uintptr_t session, std::uint16_t a,
                       std::uint16_t b);
bool endurance_ready();

// 요청을 걸어 둔다. 실제 호출은 TLS 가 준비된 게임 스레드에서 한다.
// 렌더 스레드에서 부르면 죽는다.
bool request_spawn(std::uintptr_t session, std::uint32_t item_key,
                   std::int64_t count, const float pos[3]);

// 캐릭터(탈것·NPC 포함)를 월드에 소환한다. SpawnCharacterCheatReq
// (ID 2510) 를 게임 스레드에서 부른다. 미시도 치트라 화면 확인 전이다.
bool request_char_spawn(std::uintptr_t session, std::uint32_t char_key,
                        const float pos[3]);
bool char_spawn_ready();

// 걸어 둔 요청이 처리됐는가. 아직이면 false.
bool spawn_pending();
const SpawnOutcome& last_outcome();

// 바닥 스폰 메시지를 해석해 둔다.
bool spawn_resolve_message(const mem::Rtti& rtti, const mem::Reader& reader);
const CheatMessage& spawn_message();

// 실제 작업 함수를 찾아 둔다.
bool spawn_resolve(const mem::Rtti& rtti, const mem::Reader& reader);

// 실제 작업 함수를 후킹해 불릴 때마다 인자를 로그로 남긴다. 우리가
// 부른 것이 거기까지 갔는지 보고, 게임이 평소에 넣는 값(특히 뜻을
// 모르는 필드3)을 그대로 볼 수 있다 - 인벤토리에서 아이템을 버리면
// 같은 함수가 불린다.
bool spawn_trace_install();
void spawn_trace_remove();

// 부를 준비가 됐는가.
bool spawn_ready();

}  // namespace cdtb::game
