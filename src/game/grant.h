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

// 바닥에 아이템을 떨구는 실제 작업 함수를 찾는다. 프로덕션 호출자는 없다 -
// 바닥 떨구기 추적을 지운 뒤(2026-09-10) tests/grant_tests.cpp 만 패턴 탐색
// 검증에 쓴다. 참조 수 스캔이 죽은 코드로 집어도 지우지 말 것.
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
bool actor_hook_installed();

// 본 적 있으면 그 자리의 횟수를 늘리고, 처음이면 자리를 잡는다.
// 자리가 없으면 그대로 둔다. 돌려주는 것은 개수다.
int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value);

// 세션을 적어 둘 자리를 고른다.
//
//   - 이미 표에 있으면 그 자리 (`*fresh_slot` = 거짓)
//   - 빈 자리가 있으면 그 자리 (`*fresh_slot` = 참)
//   - 꽉 찼으면 **가장 오래 전에 본 자리**를 밀어낸다 (`*fresh_slot` = 참)
//
// 표에는 지금까지 축출이 없었다. 월드에 한 번 들어가면 16칸이 다 차고,
// 그 뒤에 생긴 세션은 조용히 버려졌다 - 인플레이스 저장/로드 뒤 살아
// 있는 세션이 표에 아예 못 들어와 지급이 먹통이 된 원인이다(실측
// 2026-09-10: 로드 전후로 표의 주소 16개가 하나도 안 바뀌었고, 전부
// 82초 넘게 안 불린 죽은 칸이었다).
//
// 살아 있는 세션은 게임이 쉬지 않고 부르므로 마지막으로 본 시각이 늘
// 앞선다 - 가장 오래된 자리를 밀어내면 살아 있는 것은 밀리지 않는다.
//
// `*fresh_slot` 이 참이면 그 칸은 새 세션 것이다. 부르는 쪽이 호출수·
// 이름표·액터를 지워야 한다. 안 지우면 옛 이름표가 새 세션을 가려
// 서버 쪽인데도 후보에서 빠진다.
int session_slot_for(const std::uintptr_t* slots,
                     const std::uint64_t* last_seen, int count, int cap,
                     std::uintptr_t value, bool* fresh_slot);

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

// 세션 표의 칸 수. 표는 한 번 들어온 주소를 지우지 않고, 칸이 차면
// 새 세션을 조용히 버린다 - 로드를 거듭하면 살아 있는 세션이 표에
// 못 들어올 수 있다. 진단이 그 포화를 볼 수 있어야 한다.
int session_capacity();

// 게이트가 실제로 풀리는 서버 세션 중 호출 최다. 없으면 -1.
//
// 지급 패널이 쓰는 규칙이다. 게이트 통과 여부는 부르는 쪽이
// gate_object 로 미리 재서 넘긴다 - 이 함수는 메모리를 읽지 않으므로
// 시험할 수 있고, 진단(sessions 명령)과 패널이 같은 함수를 본다.
int best_gate_session_index(const bool* gate_open, const std::uint32_t* hits,
                            const bool* is_server, int n);

// 지급·구동에 쓸 세션을 고른다. 서버 쪽이면서 **게이트가 실제로
// 풀리는** 것 중 호출 최다. 없으면 0.
//
// 세 경로(지급 패널·보관함·동반자)가 저마다 다른 기준으로 골랐고,
// 그래서 로드 뒤 한쪽만 먹통이 되거나 한쪽만 죽은 세션을 잡았다.
// 규칙은 하나여야 한다.
//
// 게이트 통과가 유일하게 믿을 수 있는 생존 신호다 - 실측 2026-09-10,
// 인플레이스 로드 직후: 죽은 세션이 session_looks_live 에 "예" 를 줬고
// freshness 도 통과했다(표 전체가 똑같이 오래되면 '가장 최근 것 대비'
// 비교가 무의미해진다). 게이트만은 정확히 닫혔다.
std::uintptr_t pick_drive_session(const mem::Reader& reader);
// 용병단(MercenaryClanActorComponent) 컴포넌트. 준 세션의 [+0x68]+0x110, 없으면 본
// 세션 전부를 같은 사슬로 훑는다(세션 0 을 주면 곧장 전부). 세션 이름표가 필요 없어
// 명부 탐색의 값싼 길이다(RTTI 스캔 10~17초 대신 읽기 두 번).
bool clan_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out);

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

// 그 세션을 마지막으로 본 시각(GetTickCount64 기준, 못 봤으면 0).
//
// 세션 표는 한 번 들어온 주소가 영원히 남는다. 접속이 다시 맺어지면
// 옛 주소는 이미 풀린 메모리인데 누적 호출 횟수가 가장 커서 계속
// 뽑힌다 - 실측 2026-09-06: 월드 전환 뒤 같은 세션으로 메시지를
// 구동했더니 게임 안에서 0xC0000005 로 죽었고, 그것을 네 번 반복한
// 끝에 클라이언트가 오류를 내며 메인 화면으로 떨어졌다. 살아 있는
// 세션은 게임 코드가 쉬지 않고 부르므로, 최근에 봤는지가 곧 살아
// 있는지다.
std::uint64_t session_last_seen(int index);

// 살아 있는 서버 세션 중 호출이 가장 많은 자리. 없으면 -1.
//
// 살아 있는지를 절대 시각으로 자르지 않는다. 서버 쪽 세션이 얼마나
// 자주 불리는지 모르는 채로 문턱을 정하면 답이 그 추측에 끌려간다 -
// 실측 2026-09-06: 3초로 잘랐더니 월드 안인데도 후보가 없었다.
//
// 대신 서로 견준다. 죽은 세션은 아예 안 불리므로 시각이 그 자리에
// 멈추고, 살아 있는 것들은 계속 앞으로 간다. 가장 최근 것보다
// max_age_ms 넘게 뒤처진 자리는 죽었다고 본다.
//
// now_ms 는 "전부 멈췄는가"만 본다. 로딩 화면이면 살아 있는 것이
// 없으니 아무것도 고르지 않는 편이 맞다.
int best_live_session_index(const std::uint32_t* hits, const bool* is_server,
                            const std::uint64_t* last_seen, int n,
                            std::uint64_t now_ms, std::uint64_t max_age_ms);

// 가장 최근 세션보다 이만큼 뒤처지면 죽었다고 본다.
constexpr std::uint64_t kSessionFreshMs = 5000;

// 가장 최근 세션조차 이만큼 오래됐으면 손을 뗀다.
//
// 넉넉하게 잡는다. 멈춘 게임과 죽은 세션은 시간만으로 구별되지
// 않는다 - 실측 2026-09-06: 창을 내려 둔 5분 사이에 모든 세션이
// 한 번도 안 불려, 멀쩡한 세션까지 죽었다고 판정했다. 죽었는지는
// 시간이 아니라 session_looks_live() 로 직접 읽어서 가린다.
constexpr std::uint64_t kSessionDeadMs = 600000;

// 세션이 아직 살아 있는가. 처리기가 만지는 자리를 우리가 먼저
// 읽어 본다.
//
//   [세션 + 0x88] -> 그 포인터 + 1 을 읽는다
//
// 처리기가 죽은 자리가 바로 여기다(실측 2026-09-06, RVA 0x2962533·
// 0x2960DDD). 풀린 세션은 널이 아니면서 +0x88 이 쓰레기라 널 검사를
// 통과해 버린다. 그래서 그 다음 칸까지 실제로 읽어 봐야 안다.
bool session_looks_live(const mem::Reader& reader, std::uintptr_t session);

// 구동이 게임 안에서 죽은 그 세션. 죽은 적이 없으면 0.
//
// 한 번 죽은 세션으로 또 구동하면 같은 자리에서 또 죽는다. 그 반복이
// 클라이언트를 망가뜨리므로, 죽은 세션은 잠가 두고 새 세션이 잡힐
// 때까지 요청을 받지 않는다.
std::uintptr_t drive_fault_session();

// 구동 게이트 상태. 지급·소환이 전부 "대기열/쿨다운"으로 거부될 때
// 무엇이 물고 있는지 밖에서 보려고 둔다.
//
// 실측 2026-09-08: 한 번 물리면 재시작 전까지 지급도 소환도 안 됐는데
// 밖에서 원인을 볼 방법이 없었다. 게임 결함으로 오인하기 쉬웠다.
// 구동 대기열은 용도별로 나뉘어 있다.
//
// 예전에는 칸이 하나라 아이템 지급과 동반자 구동이 같은 자리를
// 나눠 썼다. 그러면 (1) 한쪽이 걸리면 다른 쪽까지 막히고,
// (2) 대기 상태가 엉뚜한 패널에 만 떴다 - 근처 탭에서 획득을
// 눌렀는데 ‘아이템 지급’ 패널에 대기 중이 뜼는 것을 사용자가
// 짚었다(2026-09-09).
//
// 실행 자체는 여전히 한 번에 하나다(게임 스레드 실행 지점·쿨다운).
// 나누는 것은 **걸어 두는 칸**과 그 상태 표시다.
enum class DriveLane {
    Item = 0,       // 아이템 지급·바닥 스폰·내구도
    Companion = 1,  // 동반자 구동(획득·거두기·부적 사용·메시지)
};
inline constexpr int kDriveLaneCount = 2;

struct DriveGate {
    bool pending = false;
    bool running = false;
    unsigned long long pending_age_ms = 0;
    unsigned long long running_age_ms = 0;
    unsigned long long cooldown_left_ms = 0;
    std::uintptr_t fault_session = 0;
};
// 레인 하나의 상태. running·쿨다운·fault 는 전체 공유라 같은 값이 나온다.
DriveGate drive_gate_state(DriveLane lane);
// 30초 넘게 물려 있을 때만 푼다. 진짜로 도는 중에는 풀지 않는다.
bool drive_gate_reset();

// 구동 지점이 게임 안에서 멈춰 다시 살아나지 못하는 상태인가.
//
// 실측 2026-09-09: 지급 처리기를 불렀는데 그 호출이 돌아오지 않았다
// ("인벤토리 지급 끝" 이 로그에 없다). 그 스레드의 재귀 깊이가
// 1 에 박혀 **이후 모든 구동이 죽는다.** 게이트를 손으로 풀어도
// 그 스레드는 여전히 멈춰 있어 살아나지 않는다 - 사용자가 계속
// 눌러 보게 두는 것보다 상태를 밝히는 편이 낫다.
//
// 실행 중이던 구동을 손으로 풀었을 때 서는다. 게임을 다시 시작해야
// 풀린다.
bool drive_point_dead();

// 죽은 세션 잠금을 푼다. 새 세션을 잡았을 때만 쓴다.
void clear_drive_fault();

// **ini 에서 읽은 구동 자리를 더한다.** 코드에 박아 둔 `kGoodDriveSites` 는
// 게임이 갱신되면 죽으므로(1.0.0.2944 에서 실제로 죽었다), 로그에 찍힌 자리를
// 사람이 ini 에 적어 다시 빌드하지 않고 살릴 수 있게 한다.
//
// **시작할 때 한 번만 부른다.** 구동 판정은 게임 디투어 안에서 돌아 할당도
// 잠금도 하면 안 되므로, 고정 칸에 복사해 두고 원자 개수로만 읽는다.
// n 이 칸 수를 넘으면 앞에서부터 칸 수만큼만 받는다.
void set_extra_drive_sites(const std::uint64_t* rvas, int n);

// 지금 안전 목록에 있는 자리 수(코드 + ini). 화면·시험용.
int drive_site_count();

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
    std::uint32_t serial = 0;   // 어느 요청의 결과인가. request_* 가 매긴다
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

    // +0x5E (u8) -> 레코드 +0x70 = **열리는 소켓 칸 수**.
    //
    // 게임의 필드 복사 함수(0x234F930)가 하는 일이 이것이다:
    //   1. 소켓 벡터를 5칸으로 확보하고 전부 `FF FF 00 00 FF`(잠김)로
    //   2. `레코드+0x70 = 이 값`
    //   3. k < 이 값 인 칸마다 `+0x40 + k*6` 6바이트를 옮기고
    //      **다섯 번째 바이트를 k 로 덮어써 그 칸을 연다.**
    // 즉 **이 값이 곧 "락 우회"** 다 - 보석 없이 빈 칸만 열 수도 있다.
    //
    // **상한을 넘기면 게임이 거절한다.** `items::socket_room_for` 가
    // 아이템 표에서 허용치를 내고, `request_give` 가 최종으로 자른다
    // (부르는 쪽에 맡기면 한 곳만 빠져도 큐가 멈춘다).
    std::uint8_t socket_count = 0;
    GiveSocket sockets[kGiveMaxSockets]{};   // +0x40 .. +0x5D
};

// 소켓 칸 수를 게임이 받아 주는 값으로 자른다.
//
// `room` 은 `items::socket_room_for(키)` 다 - 장비가 아니거나 겹치는
// 아이템이면 0, 장비면 아이템 표의 `max_sockets`. 배열이 다섯 칸이라
// `kGiveMaxSockets` 로도 자른다.
//
// 순수 함수라 게임 없이 시험한다.
std::uint8_t clamp_socket_count(std::uint8_t requested, std::uint32_t room);

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

// ----------------------------------------------------------------------
// 범용 메시지 구동 (역직렬화 함수 직접 호출).
//
// 게임 요청 메시지(TrocTr*Req)는 처리기가 역직렬화 함수(서술자
// vtable[2]) 안에 인라인돼 있어, `deser(서술자, &결과, 패킷, ?)` 로 부르면
// 곧 처리다. 패킷은 [+0x10] u16 전체길이 · [+0x18] 페이로드 포인터 ·
// [+0x38] 플래그 0 이면 된다(소켓 Phase 2 실측, 2026-09-05). 페이로드
// 머리 5바이트 = [ID u16][u8][본문길이 u16].
//
// 지급과 같은 대기열(게임 스레드 실행 지점·SEH·쿨다운)을 탄다.
struct MessageDesc {
    std::uintptr_t descriptor = 0;  // 정적 초기화가 vtable 을 넣는 전역
    std::uintptr_t deser = 0;       // vtable[2]
    std::uint32_t id = 0;           // 서술자 +0x0C
};
// RTTI 클래스 이름(예: "TrocTrUseItemByItemInfoReq")으로 해석한다.
bool resolve_message(const mem::Rtti& rtti, const mem::Reader& reader,
                     const char* class_name, MessageDesc* out);
// 와이어(머리 포함)를 걸어 둔다. 대기열이 차 있거나 쿨다운이면 false.
inline constexpr std::size_t kMessageWireMax = 128;
bool request_message(std::uintptr_t session, const MessageDesc& msg,
                     const std::uint8_t* wire, std::size_t len);

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

// ----------------------------------------------------------------------
// 동반자 등록 가능 여부 검사 (등록 자체가 아니다)
//
// 획득 2338 의 작업(0x2AE02C0)과 소지품 고용 2454 의 작업(0x2AD4000)이
// (2850 빌드; 2760 까지 0x2ADE280 / 0x2AD1FC0)
// 공통으로 부르는 함수다. 처음에 이것을 "등록의 실체"로 단정했는데
// **틀렸다** - 검사일 뿐이고, 0 을 돌려주면 각 경로가 그 뒤에 자기
// 방식으로 등록한다(2338 은 0x26B4FD0, 2454 는 다른 것 - 공통이 없다).
//
//   u32* f(용병단, u32* 결과, u16 **캐릭터표 행번호**, 1, 1)
//     0 이면 통과. 그 외는 거부 코드.
//
// **인자는 캐릭터 키가 아니라 행 번호다.** 실측 2026-09-08:
//   키 30024(고양이) -> 0x0D982DB0 거부
//   행 6511(같은 고양이) -> 0x00000000 통과
//   행 3551(양) -> 0x00000000 통과
// 통과해도 명부에는 아무것도 안 들어간다 - 검사이기 때문이다.
//
// 본체 0xE0D44F0(2850; 2760 은 0xE13BA40)은 행으로 캐릭터 레코드를 찾아
// +0xBE(_mercenaryInfo)를 읽고, 용병단의 타입별 한도 목록과 대조한다. 한도
// 목록 오프셋(+0xF8 배열 · +0x100 개수)은 2760 실측이고 2850 본체에서는 다시
// 확인하지 않았다(리뷰 관찰 2026-09-11: 2850 은 용병단 +0x18 을 0x20910C0 에
// 넘겨 {배열, 개수} 를 돌려받는 형태로 보인다).
//
// 남겨 두는 이유: 어떤 종이 등록 자격이 있는지 게임에게 직접 물어볼 수
// 있다. 목록 표시에 쓸 수 있다.
// 2850 빌드. 2760 까지 0x2097BC0(+0x1630 - 작업 함수들의 +0x2040 과 다르다,
// 영역이 다르다). 고용 작업 +0x3F3 의 call 대상이고 2454 작업 +0x36A 도 같은
// 자리를 부른다. 본체로 가는 jmp 썽크(E9)다 - run_hire_species 가 부르기 전에
// 썽크를 따라가 본체 프롤로그까지 확인한다(아래 두 함수).
inline constexpr std::uint64_t kHireCheckRva = 0x214E8B0;   // 2850 까지 0x20991F0

// 썽크 n바이트가 `jmp rel32`(E9) 면 그 대상 절대 주소, 아니면 0. thunk_addr 는
// 썽크의 절대 주소다(rel32 는 다음 명령 기준). 읽기는 호출자가 한다.
std::uintptr_t hire_check_jmp_target(const std::uint8_t* thunk, std::size_t n,
                                     std::uintptr_t thunk_addr);

// 본체 프롤로그. 2850 의 0xE0D44F0: mov rax,rsp / mov [rax+8],rbx /
// mov [rax+0x20],r9d. 썽크 주변 ±0x200 에 E9 바이트가 43개 있어도 이 프롤로그로
// 가는 것은 하나뿐이라(실측 2026-09-11) 첫 바이트만 보던 가드보다 훨씬 좁다.
inline constexpr std::uint8_t kHireCheckBodyPrologue[] = {
    0x48, 0x89, 0xE0, 0x48, 0x89, 0x58, 0x08, 0x44, 0x89, 0x48, 0x20};
bool hire_check_body_ok(const std::uint8_t* body, std::size_t n);

// 그 행이 등록 가능한지 게임에게 묻는다. 게임 스레드에서 실행한다.
bool request_hire_species(std::uintptr_t session, std::uint16_t char_row);
bool hire_species_ready();

// 캐릭터 소환 치트(SpawnCharacterCheatReq)는 **쓰지 않는다.**
//
// 반복 구동하면 게임 스레드가 처리기 안에서 빠져나오지 못한다 - 실측
// 2026-09-08: 고양이를 열 번 부르던 중 다섯 번째에서 반환이 없었고,
// 게임이 멈춘 채 141초가 지나도 그대로였다. 같은 증상을 그날 세 번
// 겪었고 두 번은 게임이 팅겼다. 2026-09-05 설계 문서가 "NPC 증발·
// 무한로딩" 으로 금지해 둔 것과 같은 증상이다(처리기 주소가 바뀌어도
// 결론은 같았다).
//
// 몇 번은 정상으로 돌기 때문에 "된다"고 오판하기 쉽다. 실제로 그날
// 고양이·양·염소를 등록하는 데까지 성공했지만, 반복하면 멈춘다.
// 구동·명령·UI 를 전부 걷어냈다. 되살리지 말 것.
//
// 종을 동반자로 올리는 안전한 길은 근처 탭 획득(2338)뿐이다. 대상이
// 월드에 실제로 있어야 한다는 제약이 붙는다.

// 걸어 둔 요청이 처리됐는가. 아직이면 false.
// 그 레인에 걸린 요청이 있는가.
bool spawn_pending(DriveLane lane);
// 마지막 결과의 복사본. 게임 스레드가 쓰는 중에 읽지 않도록 뮤텍스 아래에서
// 복사한다(Codex 지적 2026-09-11).
SpawnOutcome last_outcome();

// **이 스레드가** 마지막으로 건 요청의 번호(스레드별). 렌더 스레드와 명령 파일
// 스레드가 서로의 번호를 읽지 않는다. 결과의 serial 과 같을 때만 "내 결과"다 -
// 지급 창·보관함·로스터가 같은 결과 칸을 보므로, 남의 결과를 내 것처럼 읽지 않는다.
std::uint32_t last_request_serial();
inline bool outcome_is_mine(const SpawnOutcome& o, std::uint32_t my_serial) {
    return o.serial == my_serial;
}

// RTTI 망글 이름을 사람 눈에 맞게 자른다 - ".?AV" 뒤부터 첫 '@' 앞까지.
// 비어 있거나 널이면 "(확인 중)". out 은 늘 종료된다(n 은 1 이상).
void short_class_name(const char* mangled, char* out, std::size_t n);

// 바닥 스폰 메시지를 해석해 둔다.
bool spawn_resolve_message(const mem::Rtti& rtti, const mem::Reader& reader);
const CheatMessage& spawn_message();

// 부를 준비가 됐는가.
bool spawn_ready();

}  // namespace cdtb::game
