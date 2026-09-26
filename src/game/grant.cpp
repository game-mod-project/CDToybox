// @build 1.0.0.2976  **kGoodDriveSites 에 2976 자리를 아직 안 넣었다.** 정적으로는
//   0x2AD10DD(vtable[13] = 0x2AD0FD0 의 +0x10D, 2949 와 같은 자리)인데, 이 표에는
//   게임에서 성공 경로 줄로 확인한 뒤에 넣는다. 그때까지는 ini `drive_sites`
//   에 2AD10DD 를 적어 빌드 없이 살린다. 근거: specs/2026-09-26-game-update-2976.md §4
// @build 1.0.0.2949  kGoodDriveSites 에 2949 자리(0x2AD106D)를 넣었다(2026-09-22) -
//   2944 자리 0x2AD105D 와 **같은 메서드의 같은 호출**임을 정적으로 확인했다(아래
//   kGoodDriveSites 주석). **게임 확인(2026-09-22 15:41)** - `구동 자리 (스레드 12584):
//   ? ? ? +2AD106D +2AFA58F …` 뒤 지급이 끝났고, 보관함 지급 · 소켓 5칸 지급 · 획득(2959)도
//   같은 자리에서 돌았다.
//   다음 갱신에서 또 죽으면 로그의 `구동 건너뜀: 확인되지 않은 자리 +<RVA>`
//   를 ini 에 적어 다시 빌드 없이 살린 뒤, 확인되면 여기 넣는다.
//   AOB 패턴들은 자가 치유한다.

#include "game/grant.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>

#include "core/log.h"
#include "game/companion.h"
#include "game/knowledge.h"
#include "game/items.h"
#include "mem/hook.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 함수 앞머리 그대로다. 주소를 박아 두면 패치마다 밀리므로 바이트로
// 찾는다. 둘 다 349MB 이미지 안에서 유일한 것을 확인했다.
constexpr const char* kActorGetterPattern =
    "40 53 48 83 EC 20 48 8B 41 68 48 8B D9 48 8B 48 20 0F B7 41";

// TrItemValue 생성자. 기본값을 우리가 흉내내지 않고 게임에 맡긴다.
constexpr const char* kItemValueCtorPattern =
    "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 C7 01 FF FF FF FF 48 8D 41 40 89";

// 스레드의 작업 디스패처. 12바이트만으로 이미지 안에서 유일하다.
constexpr const char* kTaskDispatcherPattern =
    "48 83 EC 28 48 8B 41 78 48 8B 50 08";

// 메시지 펌프 (RVA 0x2369170, 2.00.01). 앞머리 40바이트. 24바이트는
// 10곳, 32바이트는 2곳이 겹친다 - 이 프롤로그 모양이 흔하다.
//
//   mov rax,rsp / mov [rax+0x10],rbx / ... / push rdi r12-r15
//   sub rsp,0x50 / mov r15,r9 / mov r12,r8 / mov r13,rdx
//
// 인자는 다섯이다: rcx, rdx, r8(큐), r9, [rsp+0x28](TLS+0x4300 컨텍스트).
// @aob 0 - 조사 훅(기본 꺼짐). 필요할 때 재도출한다. 2850 부터 0곳
constexpr const char* kMessagePumpPattern =
    "48 8B C4 48 89 58 10 48 89 68 18 48 89 70 20 48 89 48 08 57 41 54 41 "
    "55 41 56 41 57 48 83 EC 50 4D 8B F9 4D 8B E0 4C 8B";

// 작업 실행 래퍼 (RVA 0xB1D2DE0). 앞 24바이트(save rbx,rsi; push rdi;
// sub rsp,0x20; mov rax,gs:[0x58])는 흔해 15곳이 겹친다. 함수 고유
// 바이트(mov rbx,rcx; mov [rcx+0x70],1; mov rsi,[rax])까지 34바이트로
// 유일하다.
constexpr const char* kTaskRunPattern =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 65 48 8B 04 25 58 00 00 "
    "00 48 89 CB C6 41 70 01 48 8B 30";

using ActorGetterFn = std::uintptr_t(__fastcall*)(void*);

ActorGetterFn g_orig_actor_getter = nullptr;
void* g_actor_getter_target = nullptr;
std::atomic<std::uintptr_t> g_last_actor{0};
bool g_installed = false;

// 서로 다른 값을 모은다. 훅 안에서 RTTI 를 푸는 것은 너무 비싸므로
// 주소만 적어 두고 나중에 분석 스레드가 클래스를 붙인다.
constexpr int kSeenCap = 16;
std::uintptr_t g_seen[kSeenCap]{};
std::uint32_t g_seen_hits[kSeenCap]{};

// 세션은 조회 함수의 인자다. 처리기에 넘길 것은 이쪽이다.
std::uintptr_t g_sess[kSeenCap]{};
std::uint32_t g_sess_hits[kSeenCap]{};
std::uintptr_t g_sess_actor[kSeenCap]{};
char g_sess_class[kSeenCap][96]{};
bool g_sess_server[kSeenCap]{};
std::uint64_t g_sess_last[kSeenCap]{};
std::atomic<int> g_sess_count{0};
// 구동이 죽은 세션. 같은 자리로 또 보내면 또 죽는다.
std::atomic<std::uintptr_t> g_drive_fault{0};

std::atomic<int> g_seen_count{0};

// 처리기. 역직렬화가 파싱을 마치고 부르는 그 함수다. 값이 아니라
// 포인터를 받는다.
//   rcx 서술자  rdx 패킷  r8 아이템키  r9 개수  arg5 필드3  arg6 위치
using HandlerFn = void(__fastcall*)(void*, void*, const std::uint32_t*,
                                    const std::int64_t*, const std::uint16_t*,
                                    const float*);

// SpawnCharacter 처리기(0x278B860). 인자 순서·타입이 아이템 스폰과
// 다르다 - 역직렬화(0x2563430)의 처리기 호출을 실측했다:
//   rcx 서술자  rdx 패킷  r8 &캐릭터키u32  r9 &Bu32  [+0x20] &위치float3
//   [+0x28] &플래그u8
// 처리기는 캐릭터키가 0이면 거부한다(아이템 스폰의 키 검사와 같다).
CheatMessage g_give_msg;
CheatMessage g_stat_msg;
CheatMessage g_endur_msg;

// 내구도 처리기. 인자 넷뿐이다.
using EndurFn = void(__fastcall*)(void*, void*, const std::uint16_t*,
                                  const std::uint16_t*);      // VaryStat - 대상 ID 조회 함수를 여기서 유도한다

// 엔티티 조회. (매니저, 결과, u32 ID)
using EntityLookupFn = void*(__fastcall*)(void*, void*, std::uint32_t);
EntityLookupFn g_orig_entity = nullptr;
void* g_entity_target = nullptr;
bool g_entity_installed = false;

constexpr int kEntCap = 32;
std::uint32_t g_ent[kEntCap]{};
std::uint32_t g_ent_hits[kEntCap]{};
std::atomic<int> g_ent_count{0};

// TrItemValue 생성자와 인벤토리 직행 처리기.
using CtorFn = void*(__fastcall*)(void*);
using GiveFn = void(__fastcall*)(void*, void*, void*);
CtorFn g_item_value_ctor = nullptr;
const mem::Reader* g_reader = nullptr;

// 걸어 둔 요청. 렌더 스레드가 채우고, TLS 가 준비된 게임 스레드가
// 집어 간다.
enum class Kind { Inventory, Endurance, Message, HireSpecies };

struct Pending {
    Kind kind = Kind::Inventory;
    std::uintptr_t session = 0;
    std::uint32_t key = 0;
    std::int64_t count = 0;
    GiveExtras extras;           // 담금질·소켓 (TrItemValue 칸들)
    std::uint16_t a = 0;         // 내구도 인자
    std::uint16_t b = 0;
    // 범용 메시지 구동(Message) 인자. 와이어는 머리 5바이트 포함.
    MessageDesc msg;
    std::uint8_t wire[kMessageWireMax]{};
    std::size_t wire_len = 0;
    std::uint32_t serial = 0;   // 요청 번호(stamp_request)
};
// 아래에서 정의한다. 후킹이 먼저 나온다.
void run_hire_species(std::uintptr_t session, std::uint16_t key,
                      SpawnOutcome* out);
void run_give(std::uintptr_t session, std::uint32_t item_key,
              std::int64_t count, const GiveExtras& extras, SpawnOutcome* out);
void run_endurance(std::uintptr_t session, std::uint16_t a, std::uint16_t b,
                   SpawnOutcome* out);
void run_message(std::uintptr_t session, const MessageDesc& msg,
                 const std::uint8_t* wire, std::size_t len, SpawnOutcome* out);

// 레인별 칸. 아이템 지급과 동반자 구동이 서로를 막지 않게
// 칸을 나눠 둔다(grant.h DriveLane 설명).
struct LaneSlot {
    Pending req;
    std::atomic<bool> has{false};
    std::atomic<std::uint64_t> at{0};
};
LaneSlot g_lane[kDriveLaneCount];

// 쿨타임은 **레인별**이다. 실행은 직렬이어야 하지만, 지급을 했다고
// 획득까지 2초 막을 이유는 없다(사용자 지적 2026-09-09).
std::atomic<unsigned long long> g_lane_done[kDriveLaneCount]{};

LaneSlot& lane_of(DriveLane l) {
    return g_lane[static_cast<int>(l)];
}

// 어느 레인이든 걸린 것이 있는가.
// 그 레인이 마지막으로 난 시각. 쿨타임은 레인별이다.
unsigned long long lane_done_ms(const LaneSlot& l) {
    const int idx = static_cast<int>(&l - &g_lane[0]);
    if (idx < 0 || idx >= kDriveLaneCount) return 0;
    return g_lane_done[idx].load(std::memory_order_acquire);
}

bool any_pending() {
    for (auto& l : g_lane) {
        if (l.has.load(std::memory_order_acquire)) return true;
    }
    return false;
}

// 요청을 건 시각. 게임 스레드가 집어 가지 않으면 대기열이 영영 막힌다
// - 실측 2026-09-06: 메뉴 화면에서 건 요청 하나가 6분 넘게 남아 그 뒤
// 모든 요청이 거부됐다. 실행 지점은 월드가 돌 때만 불리므로, 오래
// 묵은 요청은 버리고 새 요청을 받는다.
constexpr std::uint64_t kPendingMaxMs = 15000;

// 묵은 요청이면 버린다. 버렸으면 true.
bool drop_stale_pending() {
    bool dropped = false;
    for (int i = 0; i < kDriveLaneCount; ++i) {
        LaneSlot& l = g_lane[i];
        if (!l.has.load(std::memory_order_acquire)) continue;
        const std::uint64_t at = l.at.load(std::memory_order_acquire);
        if (at == 0 || GetTickCount64() - at < kPendingMaxMs) continue;
        if (!l.has.exchange(false, std::memory_order_acq_rel)) continue;
        log::warnf("대기열[{}]: {}ms 동안 실행되지 않은 요청을 버린다 - "
                   "월드가 돌고 있어야 실행된다",
                   i, GetTickCount64() - at);
        dropped = true;
    }
    return dropped;
}


// 게임 함수를 후킹 안에서 부르면, 그 후킹이 걸린 자리가 이미 락을
// 쥐고 있을 때 교착한다 - 실측에서 세 번째 호출이 돌아오지 않고
// 게임 조작이 통째로 멈췄다. 다음으로 줄인다.
//
//   1. 우리 후킹 안에 이미 들어와 있으면 실행하지 않는다 (중첩 금지)
//   2. 한 번에 하나만, 끝날 때까지 다음 요청을 받지 않는다
//   3. 연속 호출 사이에 간격을 둔다
thread_local int g_detour_depth = 0;
// 계측 훅(펌프) 전용 재귀 가드. g_detour_depth 와 **반드시 분리한다** -
// 펌프가 g_detour_depth 를 밀어 올려 지급의 실행 조건(액터 조회
// depth==1)을 깬 적이 있다(2026-09-04). 계측은 실행 게이트를 건드리면
// 안 된다.
thread_local int g_pump_depth = 0;
std::atomic<bool> g_running{false};
// 구동이 시작된 시각. 물렸을 때 얼마나 오래됐는지 보려고 둔다.
std::atomic<unsigned long long> g_running_at{0};
std::atomic<unsigned long long> g_last_done{0};
// 실행 중이던 구동을 손으로 풀었다 = 그 호출이 안 돌아왔다는 뜻.
std::atomic<bool> g_drive_dead{false};
// 지급이 실제로 실행되는 스레드 = 게임 로직 스레드. 액터 조회가
// depth==1·TLS 준비 상태로 도는 그 스레드다. 작업 실행 래퍼가 이
// 스레드에서도 도는지 가리는 데 쓴다.
std::atomic<std::uint32_t> g_game_thread{0};
constexpr unsigned long long kCooldownMs = 2000;

// 안전한 실행 지점을 찾으려고 호출 스택을 한 번만 뜬다. 지금은
// 후킹이 걸린 자리에서 게임 함수를 부르는데, 그 자리가 락을 쥐고
// 있으면 교착한다. 스택의 바깥쪽 프레임이 곧 그 스레드의 루프
// 뿌리이고, 거기가 안전한 지점이다.
std::atomic<bool> g_traced{false};

void log_call_stack() {
    void* frames[40]{};
    const USHORT n = RtlCaptureStackBackTrace(0, 40, frames, nullptr);
    const std::uintptr_t base = g_reader->module_base();
    const std::size_t size = g_reader->module_size();
    log::infof("실행 지점 호출 스택 {}단", n);
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) {
            log::infof("  [{}] 모듈+0x{:X}", i, a - base);
        } else {
            log::infof("  [{}] 0x{:X} (모듈 밖)", i, a);
        }
    }
}

// 평소 플레이 중(치트 처리가 아닌) 액터 조회 스택. 20:58 스택은 치트
// 처리 중이라 그 순간의 사슬이었다. 지급이 안 걸린 상태의 스택이라야
// 매 프레임 세션 업데이트 루프가 드러난다. 서로 다른 바깥 프레임을
// 몇 개만 잡아 로그한다 - RtlCaptureStackBackTrace 는 비싸므로 상한을
// 둔다.
constexpr int kNormalTraces = 4;
std::atomic<int> g_normal_traces{0};
std::uintptr_t g_normal_outer[kNormalTraces]{};

// 구동을 시작하는 **그 자리**를 한 줄로 남긴다.
//
// 구동이 게임 안에서 돌아오지 않는 일이 간헐적으로 생긴다
// (실측 2026-09-09: 지급 한 번, 획득 두 번). __finally 가 돌지
// 않았으니 예외가 아니라 진짜로 막힌 것이고, 게임 코드 한복판에서
// 게임 함수를 부르는 재진입 교착으로 보인다.
//
// 성공한 구동과 막힌 구동의 **부르는 자리**를 비교하면 어느 자리가
// 안전한지 갈린다. 막히면 이 줄이 마지막으로 남으므로 그 자리가
// 범인이다. 구동할 때만 찍으므로 비용은 없다시피 하다.
// 실측으로 확인된 **안전한 구동 자리**.
//
// 구동 지점(det_actor_getter)은 게임 코드 한복판이라, 그 자리가 이미
// 락을 쥐고 있으면 게임 함수를 부르는 순간 교착한다. 그러면 게임
// 로직 스레드가 통째로 멈춰 **저장도 정상 종료도 안 된다**.
//
// 자리를 찍어 비교하니 성공과 멈춤이 서로 다른 자리였다
// (실측 2026-09-09).
//
//   성공  +2A0263D +2A2BBBF ...   지급·획득 전부 이 자리
//   멈춤  +27A95D5 +1041C5B7 ...  이 자리에서 게임이 멈췄다
//
// 그래서 확인된 자리에서만 구동한다. 모르는 자리면 이번은
// 건너뛰고 요청은 그대로 둔다 - 다음에 안전한 자리가 오면 돌아간다.
// 새 자리를 더 모으려면 건너뛴 자리를 로그에서 보고 여기 넣는다.
// 1.0.0.2850(2026-09-11): 옛 자리 0x2A0263D 가 +0x1740 밀렸다(역직렬화들과 같은
// 폭). 새 exe 에서 액터 조회(0x2074BA0)를 부르는 반환 주소 중 정확히 그 자리다.
// 틀리면 요청이 전부 건너뛰어지고 "구동 건너뜀: 확인되지 않은 자리 +RVA" 로그에
// 실제 자리가 찍힌다("구동 자리 …" 줄은 성공 경로 전용). 2850 첫 실행(옛 값을
// 가진 1차 빌드) 실측: 지급 때마다 `+2A03D7D` 가 남고 지급·획득이 전부 미뤄졌다.
// 1.0.0.2944(2026-09-18): **옛 자리 0x2A03D7D 가 죽었다** - 새 exe 에서 그 자리는
// `lea r13,[rip+..]` 이고 액터 조회를 부르는 반환 주소가 아니다. 액터 조회
// (0x212A0F0) 호출이 681곳이라 그중 어느 것이 "안전한 구동 자리" 인지는 **정적으로
// 못 가린다** - 2850 때도 런타임 로그로 잡았다. 그래서 값을 옛 것으로 두고,
// 첫 실행에서 "구동 건너뜀: 확인되지 않은 자리 +RVA" 로 찍히는 자리를
// `CDToybox.ini` 의 `drive_sites` 에 넣으면 **다시 빌드하지 않고** 살아난다
// (아래 g_extra_drive_sites). specs/2026-09-18-game-update-2944.md.
//
// 그 런타임 자리가 잡혔다: **2944 는 `0x2AD105D`** 다. ini 로 넣어 굴린 뒤
// 성공 경로 줄이 남았고(2026-09-18 17:52:51 `구동 자리 (스레드 42408):
// ? ? ? +2AD105D …` - 이 줄은 **구동이 실제로 돈 뒤에만** 찍힌다) 사용자가
// 지급을 확인했다. 여기 넣어 두면 새로 설치해도 ini 손질 없이 돈다.
// ini 갈래는 그대로 둔다 - 다음 갱신 때 또 필요하다.
//
// 2850 자리를 안 지우는 이유: 그 RVA 는 2944 에서 호출자로 안 나타나므로
// 놔둬도 해가 없고, 2850 으로 되돌린 사람에게는 그것이 유일한 자리다.
//
// 1.0.0.2949(2026-09-22): **`0x2AD106D`**. 2949 첫 실행에서 지급이 멈춘 채
// `구동 건너뜀: 확인되지 않은 자리 +2AD106D` 가 찍혔고(후보 중 하나), 정적으로
// 2944 자리와 같은 호출임을 확인했다:
//   - `0x2AD1068 call 0x212A100`(= 액터 조회, kActorGetterPattern 이 잡는 자리)의
//     반환 자리다.
//   - 그 함수 0x2AD0F60 은 `ServerEquipSlotActorComponent` vtable(0x5B28F80)의
//     13번 칸(+0x68)이다. 2944 성공 스택의 다음 프레임 `+2AFA57F` 에 해당하는
//     2949 의 0x2AFA58F 는 바로 `call [rax+0x68]` 의 반환 자리다(둘 다 +0x10).
// 옛 두 자리는 2949 에서 호출 반환 자리가 아니다(0x2A03D7D = `mov [rdx],r14d`,
// 0x2AD105D = 명령 중간) - 남겨도 여기서는 안 걸린다.
// 게임에서 확인했다(2026-09-22 15:41:16): 성공 경로 줄이 정적으로 예측한 두 프레임 그대로
// `? ? ? +2AD106D +2AFA58F +10471DC9 +2AF9CA7 +27EBA91` 로 찍히고 지급이 끝났다.
constexpr std::uint64_t kGoodDriveSites[] = {0x2A03D7D, 0x2AD105D, 0x2AD106D};

// 부르는 자리(모듈 안 첫 프레임)를 낸다. 모르면 0.
std::uint64_t drive_site_rva() {
    if (g_reader == nullptr) return 0;
    void* frames[8]{};
    const USHORT n = RtlCaptureStackBackTrace(0, 8, frames, nullptr);
    const std::uintptr_t base = g_reader->module_base();
    const std::size_t size = g_reader->module_size();
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) return a - base;
    }
    return 0;
}

// ini 가 더해 준 자리. 디투어 안에서 읽으므로 할당도 잠금도 없는 고정 칸이다 -
// 시작할 때 한 번 채우고 그 뒤로는 읽기만 한다.
constexpr int kMaxExtraDriveSites = 8;
std::uint64_t g_extra_drive_sites[kMaxExtraDriveSites]{};
std::atomic<int> g_extra_drive_count{0};

bool drive_site_is_good(std::uint64_t site) {
    if (site == 0) return false;
    for (const auto g : kGoodDriveSites) {
        if (g == site) return true;
    }
    const int n = g_extra_drive_count.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_extra_drive_sites[i] == site) return true;
    }
    return false;
}

void log_drive_site() {
    if (g_reader == nullptr) return;
    void* frames[8]{};
    const USHORT n = RtlCaptureStackBackTrace(0, 8, frames, nullptr);
    const std::uintptr_t base = g_reader->module_base();
    const std::size_t size = g_reader->module_size();
    std::string line;
    char buf[32];
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) {
            std::snprintf(buf, sizeof(buf), " +%llX",
                          static_cast<unsigned long long>(a - base));
        } else {
            std::snprintf(buf, sizeof(buf), " ?");
        }
        line += buf;
    }
    log::infof("구동 자리 (스레드 {}):{}", GetCurrentThreadId(), line);
}

void log_normal_stack() {
    void* frames[48]{};
    const USHORT n = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    const std::uintptr_t base = g_reader->module_base();
    const std::size_t size = g_reader->module_size();
    // 바깥쪽(스레드 뿌리에 가까운) 모듈 내 프레임으로 중복을 가른다.
    std::uintptr_t outer = 0;
    for (int i = n - 1; i >= 0; --i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) {
            outer = a - base;
            break;
        }
    }
    const int seen = g_normal_traces.load(std::memory_order_acquire);
    for (int i = 0; i < seen; ++i) {
        if (g_normal_outer[i] == outer) return;  // 같은 경로는 한 번만
    }
    if (seen >= kNormalTraces) return;
    g_normal_outer[seen] = outer;
    g_normal_traces.store(seen + 1, std::memory_order_release);
    log::infof("=== 평소 플레이 액터 조회 스택 #{} (스레드 {}), {}단 ===",
               seen, GetCurrentThreadId(), n);
    for (USHORT i = 0; i < n; ++i) {
        const auto a = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (a >= base && a < base + size) {
            log::infof("  [{}] 모듈+0x{:X}", i, a - base);
        } else {
            log::infof("  [{}] 0x{:X} (모듈 밖)", i, a);
        }
    }
}
// 결과 칸. 게임 스레드가 쓰고 렌더·명령 스레드가 읽으므로 뮤텍스로 감싼다.
SpawnOutcome g_outcome;
std::mutex g_outcome_mutex;
// 생산자(렌더 스레드·명령 파일 스레드)가 같은 레인을 동시에 채우지 않게 한다.
std::mutex g_produce_mutex;

std::atomic<std::uint32_t> g_request_serial{0};
// 이 스레드가 마지막으로 매긴 번호. 스레드별이라 다른 생산자가 그 사이에 번호를
// 올려도 내 것이 바뀌지 않는다(Codex 지적 2026-09-11).
thread_local std::uint32_t t_last_serial = 0;

// 요청마다 번호를 매기고 결과 칸을 비운다. 창은 자기 번호의 결과만 읽는다.
void stamp_request(LaneSlot& lane) {
    const std::uint32_t s =
        g_request_serial.fetch_add(1, std::memory_order_acq_rel) + 1;
    lane.req.serial = s;
    t_last_serial = s;
    std::lock_guard<std::mutex> lock(g_outcome_mutex);
    g_outcome = SpawnOutcome{};
    g_outcome.serial = s;
}

// 작업 디스패처. 여기 진입점이 안전한 실행 지점이다 - 스택이 얕고
// 아직 아무 작업도 시작하지 않았다.
using TaskDispatchFn = void(__fastcall*)(void*);
TaskDispatchFn g_orig_dispatch = nullptr;
void* g_dispatch_target = nullptr;
bool g_tick_installed = false;

// 메시지 펌프. 다섯 인자를 그대로 넘긴다 - 다섯째는 스택이다.
using MessagePumpFn = void(__fastcall*)(void*, void*, void*, void*, void*);
MessagePumpFn g_orig_pump = nullptr;
void* g_pump_target = nullptr;
std::atomic<bool> g_pump_installed{false};

// 작업 실행 래퍼. 인자 하나(작업 객체). 계측 전용.
using TaskRunFn = void(__fastcall*)(void*);
TaskRunFn g_orig_taskrun = nullptr;
void* g_taskrun_target = nullptr;
std::atomic<bool> g_taskrun_installed{false};
std::atomic<std::uint32_t> g_taskrun_calls{0};
// 어느 스레드가 작업을 실행하는지. 훅 안이라 값싼 것만 한다 -
// 스레드 ID 최대 16개와 각각의 횟수.
constexpr int kTRThreads = 16;
std::uint32_t g_tr_tid[kTRThreads]{};
std::atomic<std::uint32_t> g_tr_hits[kTRThreads]{};
std::atomic<int> g_tr_nthreads{0};

// 시간 기반 덤프. 카운트 기반(N회마다)은 빈도를 못 읽는다 - 20000회가
// 1초 만인지 10분 만인지 알 수 없다. 5초마다 한 스레드만 선점해
// 총계·최근 구간 비율·스레드별 횟수를 남긴다.
constexpr unsigned long long kTRDumpMs = 5000;
std::atomic<unsigned long long> g_tr_next_dump{0};
std::atomic<unsigned long long> g_tr_last_tick{0};
std::atomic<std::uint32_t> g_tr_last_total{0};

void __fastcall det_task_run(void* task) {
    const std::uint32_t tid = GetCurrentThreadId();
    const int n = g_tr_nthreads.load(std::memory_order_acquire);
    int idx = -1;
    for (int i = 0; i < n; ++i) {
        if (g_tr_tid[i] == tid) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && n < kTRThreads) {
        g_tr_tid[n] = tid;
        g_tr_nthreads.store(n + 1, std::memory_order_release);
        idx = n;
    }
    if (idx >= 0) g_tr_hits[idx].fetch_add(1, std::memory_order_relaxed);
    const std::uint32_t total =
        g_taskrun_calls.fetch_add(1, std::memory_order_relaxed) + 1;

    const unsigned long long now = GetTickCount64();
    unsigned long long due = g_tr_next_dump.load(std::memory_order_acquire);
    if (now >= due &&
        g_tr_next_dump.compare_exchange_strong(due, now + kTRDumpMs,
                                               std::memory_order_acq_rel)) {
        const unsigned long long last_tick =
            g_tr_last_tick.exchange(now, std::memory_order_acq_rel);
        const std::uint32_t last_total =
            g_tr_last_total.exchange(total, std::memory_order_acq_rel);
        if (last_tick != 0 && now > last_tick) {
            const std::uint32_t dc = total - last_total;
            const unsigned long long dt = now - last_tick;
            log::infof("작업 실행 래퍼: 총 {}회, 최근 {}ms 에 {}회 ({}/초), "
                       "스레드 {}개",
                       total, dt, dc, dc * 1000ULL / dt,
                       g_tr_nthreads.load(std::memory_order_acquire));
        } else {
            log::infof("작업 실행 래퍼: 총 {}회, 스레드 {}개 (첫 덤프)", total,
                       g_tr_nthreads.load(std::memory_order_acquire));
        }
        const std::uint32_t game_tid =
            g_game_thread.load(std::memory_order_relaxed);
        const int nn = g_tr_nthreads.load(std::memory_order_acquire);
        for (int i = 0; i < nn; ++i) {
            const bool is_game = (game_tid != 0 && g_tr_tid[i] == game_tid);
            log::infof("  스레드 {} : {}회{}", g_tr_tid[i],
                       g_tr_hits[i].load(std::memory_order_relaxed),
                       is_game ? "  <- 게임 로직" : "");
        }
    }
    g_orig_taskrun(task);
}
// 펌프가 어느 스레드·어느 작업 컨텍스트에서 도는지 처음 몇 번만
// 남긴다. 세션마다 펌프가 따로 돌 수 있어 실행 지점을 가리는 근거다.
constexpr int kPumpSeenCap = 8;
std::uint32_t g_pump_tid[kPumpSeenCap]{};
std::uintptr_t g_pump_ctx[kPumpSeenCap]{};
std::atomic<int> g_pump_seen{0};
std::atomic<std::uint32_t> g_pump_calls{0};
bool safe_deref(std::uintptr_t at, std::uintptr_t* out);

// 걸어 둔 요청이 있으면 여기서 실행한다. 조건을 한 곳에 모은다.
// 실제로 하나를 실행했으면 true.
// 실제 실행부. 예외가 난 자리를 밖으로 보내지 않고 그대로 둔다 -
// 게이트 푸는 일은 부르는 쪽의 __finally 가 맡는다.
int g_last_lane = -1;

bool run_one_picked() {
    bool ran = false;
    // 레인을 순서대로 본다. 한 번에 하나만 실행한다 - 실행 지점은
    // 여전히 직렬화되어야 한다.
    int picked = -1;
    for (int i = 0; i < kDriveLaneCount; ++i) {
        if (g_lane[i].has.exchange(false, std::memory_order_acq_rel)) {
            picked = i;
            break;
        }
    }
    g_last_lane = picked;
    if (picked >= 0) {
        ran = true;
        const Pending req = g_lane[picked].req;
        // 지역 칸에 받아 한 번에 게시한다 - run_* 가 쓰는 도중에 창이 읽지 않게.
        SpawnOutcome local;
        switch (req.kind) {
            case Kind::Inventory:
                run_give(req.session, req.key, req.count, req.extras,
                         &local);
                break;
            case Kind::Endurance:
                run_endurance(req.session, req.a, req.b, &local);
                break;
            case Kind::HireSpecies:
                run_hire_species(req.session,
                                 static_cast<std::uint16_t>(req.key),
                                 &local);
                break;
            case Kind::Message:
                run_message(req.session, req.msg, req.wire, req.wire_len,
                            &local);
                break;
        }
        // run_* 가 결과를 통째로 덮어 번호가 지워진다.
        local.serial = req.serial;
        {
            std::lock_guard<std::mutex> lock(g_outcome_mutex);
            g_outcome = local;
        }
        // 종류를 가리지 않는다. 게임 안에서 죽었다는 것은 우리가
        // 넘긴 세션이 이미 풀렸다는 뜻이고 - 실측 2026-09-06: 죽은
        // 자리가 `mov rax,[세션+0x88]` 이었다 - 같은 자리로 또 보내면
        // 또 죽는다. 그 반복이 클라이언트를 오류로 떨어뜨린다.
        if (local.crashed && req.session != 0) {
            g_drive_fault.store(req.session, std::memory_order_release);
            log::warnf("세션 0x{:X} 를 잠갔다 - 새 세션이 잡힐 때까지 구동하지 않는다",
                       req.session);
        }
    }
    return ran;
}

// 걸어 둔 요청이 있으면 여기서 실행한다. 조건을 한 곳에 모은다.
// 실제로 하나를 실행했으면 true.
bool run_pending_if_any() {

    if (!any_pending()) return false;
    if (!thread_ready_for_spawn()) return false;
    if (g_running.exchange(true, std::memory_order_acq_rel)) return false;
    g_running_at.store(GetTickCount64(), std::memory_order_release);
    bool ran = false;
    // **게이트는 무슨 일이 있어도 푸단다.**
    //
    // 실측 2026-09-09: 지급과 획득이 각각 한 번씩, 작업을 시작해 놓고
    // "끝" 로그 없이 사라졌다. 그런데 게임은 62 FPS 로 멀썩히 돌고
    // 있었다 - 즉 게임 스레드가 멈춘 것이 아니라 **예외가 이 함수를
    // 건너뛰어 풀렸다.** 이 빌드는 /EHsc 라 SEH 는 C++ 소멸자를
    // 돌리지 않으므로 RAII 로는 막힐 수 없다. __finally 여야 한다.
    //
    // 이것이 없으면 g_running 이 영원히 true 로 남아 그 뒤 모든 지급·
    // 획득이 죽고, 게임을 다시 시작하는 수밖에 없었다.
    // 확인된 자리가 아니면 구동하지 않는다. 요청은 그대로 둔다.
    const std::uint64_t site = drive_site_rva();
    if (!drive_site_is_good(site)) {
        static std::uint64_t s_last_skip = 0;
        if (s_last_skip != site) {
            s_last_skip = site;
            log::infof("구동 건너뜀: 확인되지 않은 자리 +{:X} - 안전한 자리를 기다린다",
                       site);
        }
        g_running.store(false, std::memory_order_release);
        return false;
    }
    log_drive_site();
    __try {
        ran = run_one_picked();
    } __finally {
        const unsigned long long done_at = GetTickCount64();
        g_last_done.store(done_at, std::memory_order_release);
        if (g_last_lane >= 0 && g_last_lane < kDriveLaneCount) {
            g_lane_done[g_last_lane].store(done_at, std::memory_order_release);
        }
        g_running.store(false, std::memory_order_release);
        if (AbnormalTermination()) {
            log::errorf("구동이 예외로 풀렸다 - 게이트는 풀었으니 계속 쓸 수 있다");
        }
    }
    return ran;
}

// 메시지 펌프가 **돌아온 뒤에** 우리 일을 한다. 그 틱의 메시지는 전부
// 처리됐고 처리기의 락은 놓였다. TLS+0x250 은 작업 실행기가 작업
// 시작에 세우고 끝에 지우므로 여기서는 서 있다 - 디스패처 자리에서
// 서 있지 않던 것과 다른 점이다.
//
// 펌프는 세션마다 따로 돌 수 있다. 처음 몇 번은 스레드와 작업
// 컨텍스트를 남겨 어느 펌프에서 실행됐는지 알 수 있게 한다.
void note_pump_context() {
    const std::uint32_t tid = GetCurrentThreadId();
    std::uintptr_t ctx = 0;
    const std::uintptr_t tls = static_cast<std::uintptr_t>(__readgsqword(0x58));
    std::uintptr_t slot0 = 0;
    if (tls != 0 && safe_deref(tls, &slot0) && slot0 != 0) {
        safe_deref(slot0 + 0x250, &ctx);
    }
    const int n = g_pump_seen.load(std::memory_order_acquire);
    for (int i = 0; i < n; ++i) {
        if (g_pump_tid[i] == tid && g_pump_ctx[i] == ctx) return;
    }
    if (n >= kPumpSeenCap) return;
    g_pump_tid[n] = tid;
    g_pump_ctx[n] = ctx;
    g_pump_seen.store(n + 1, std::memory_order_release);
    log::infof("메시지 펌프 경계: 스레드 {} 작업 컨텍스트 0x{:X}", tid, ctx);
}

// 계측 전용이다. **g_detour_depth 를 건드리지 않는다** - 대신 펌프
// 전용 가드(g_pump_depth)로 중첩 진입 때 로그만 걸러 낸다. 실행은
// 하지 않는다(펌프는 메시지 구동이라 프레임 경계가 아니었다).
std::atomic<std::uint32_t> g_pump_raw{0};   // depth 게이트 없는 순수 진입 수
void __fastcall det_message_pump(void* a, void* b, void* c, void* d, void* e) {
    // [진단] detour 가 실제로 도는가 + 이 스레드에서 thread_local 이
    // 0 으로 초기화되는가. 게이트 없이 진입할 때마다 세고, 첫 다섯 번은
    // g_pump_depth 의 현재 값(++ 전)을 그대로 찍는다. 값이 0 이 아니면
    // thread_local 미초기화 가설이 맞다.
    const int depth_before = g_pump_depth;
    const std::uint32_t raw = g_pump_raw.fetch_add(1, std::memory_order_relaxed) + 1;
    if (raw <= 5) {
        log::infof("[펌프진단] 진입 {}회 스레드 {} g_pump_depth(++전)={}", raw,
                   GetCurrentThreadId(), depth_before);
    }
    ++g_pump_depth;
    g_orig_pump(a, b, c, d, e);
    const std::uint32_t calls =
        g_pump_calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (g_pump_depth == 1) {
        if (calls <= 3 || calls % 1000 == 0) {
            log::infof("메시지 펌프 호출 {}회 (스레드 {})", calls,
                       GetCurrentThreadId());
        }
        if (any_pending()) note_pump_context();
    }
    --g_pump_depth;
}

// 작업 콜백이 **끝난 뒤에** 우리 일을 한다.
//
// 진입 시점에 해 봤더니 요청이 실행되지 않았다 - 그때는 TLS 블록이
// 아직 서 있지 않다. 작업이 돌면서 늦게 잡히는 구조다. 콜백이
// 돌아온 자리는 그 작업이 쥐었던 락을 이미 놓았고 TLS 는 서 있다.
// 스택도 여전히 얕다(스레드 본체 -> 이 함수).
void __fastcall det_task_dispatch(void* self) {
    g_orig_dispatch(self);
    if (g_detour_depth == 0) {
        ++g_detour_depth;
        run_pending_if_any();
        --g_detour_depth;
    }
}

// 지나가는 엔티티 ID 를 모은다. 원본을 그대로 부른다.
void* __fastcall det_entity_lookup(void* mgr, void* out, std::uint32_t id) {
    if (id != 0) {
        const int n = g_ent_count.load(std::memory_order_relaxed);
        int i = 0;
        for (; i < n; ++i) {
            if (g_ent[i] == id) {
                ++g_ent_hits[i];
                break;
            }
        }
        if (i == n && n < kEntCap) {
            g_ent[n] = id;
            g_ent_hits[n] = 1;
            g_ent_count.store(n + 1, std::memory_order_release);
        }
    }
    return g_orig_entity(mgr, out, id);
}

// 게임의 여러 스레드에서 불린다. 하는 일은 값을 적어 두는 것뿐이다.
std::uintptr_t __fastcall det_actor_getter(void* session) {
    // 우리가 부른 게임 함수가 이 후킹을 다시 밟는다. 진입할 때마다
    // 깊이를 세어, 중첩된 자리에서는 아무것도 실행하지 않는다.
    ++g_detour_depth;
    const std::uintptr_t actor = g_orig_actor_getter(session);

    // 액터를 낸 세션만 표에 적는다. 지급은 세션 -> 액터 경로를 타므로
    // 널만 돌려주는 세션은 애초에 후보가 못 되는데, 실측 2026-09-10 에
    // 그런 세션(…E0600~…E0C00 등)이 16칸 중 9칸을 먹어 정작 살아 있는
    // 세션이 들어올 자리를 없앴다.
    if (session != nullptr && actor != 0) {
        const auto s = reinterpret_cast<std::uintptr_t>(session);
        const int m = g_sess_count.load(std::memory_order_relaxed);
        bool fresh = false;
        const int slot =
            session_slot_for(g_sess, g_sess_last, m, kSeenCap, s, &fresh);
        if (slot >= 0) {
            if (fresh) {
                // 앞 세션의 흔적을 먼저 지우고 주소를 맨 마지막에
                // 세운다. 순서가 거꾸로면 읽는 쪽이 "새 주소 + 옛
                // 이름표" 인 찰나를 볼 수 있는데, 그것이 바로 새 세션을
                // 클라이언트로 오인해 후보에서 빼는 자리다.
                g_sess_class[slot][0] = 0;
                g_sess_server[slot] = false;
                g_sess_actor[slot] = 0;
                g_sess_hits[slot] = 0;
                g_sess[slot] = s;
            }
            ++g_sess_hits[slot];
            // 이 세션이 어떤 액터를 내는지 같이 적어 둔다. 나중에 분석
            // 스레드가 클래스를 붙여 서버 쪽인지 가린다.
            g_sess_actor[slot] = actor;
            // 마지막으로 액터를 낸 시각. 축출이 이 값으로 가장 오래된
            // 칸을 고른다 - 살아 있는 세션은 게임이 쉬지 않고 부르므로
            // 밀려나지 않는다.
            g_sess_last[slot] = ::GetTickCount64();
            if (slot >= m) {
                g_sess_count.store(slot + 1, std::memory_order_release);
            }
        }
    }
    if (actor != 0) {
        g_last_actor.store(actor, std::memory_order_relaxed);
        const int n = g_seen_count.load(std::memory_order_relaxed);
        const int now = note_actor(g_seen, g_seen_hits, n, kSeenCap, actor);
        if (now != n) g_seen_count.store(now, std::memory_order_release);
    }

    // 실행 지점이 둘이다. 작업 디스패처가 더 안전하지만 그 자리에는
    // TLS 가 서 있지 않아 - 두 번 실측했다 - 요청이 실행되지 않았다.
    // 작업이 도는 동안에만 잡히고 끝나면 정리되는 모양이다.
    //
    // 그래서 여기서도 집어 간다. 이 자리는 게임 코드 한복판이라
    // 위험하지만 실제로 동작이 확인된 유일한 자리다. 깊이가 1일
    // 때만, 한 번에 하나만, 2초 간격으로 - 그 셋을 넣은 뒤로는
    // 교착이 재발하지 않았다.
    //
    // 이 자리가 실측으로 확인된 유일한 실행 지점이다. 펌프·디스패처
    // 자리는 메시지 처리 작업 전용이라 싱글플레이 유휴 상태에서는
    // 돌지 않는다(2026-09-04 실측). 그래서 여기서 실행한다.
    // 이 스레드가 곧 게임 로직 스레드다 - 작업 실행 래퍼 계측이
    // 어느 스레드가 게임 로직인지 가리는 데 쓴다.
    if (g_detour_depth == 1 && thread_ready_for_spawn()) {
        g_game_thread.store(GetCurrentThreadId(), std::memory_order_relaxed);
        // 지급이 안 걸린 평소 상태의 스택을 몇 개 잡는다 - 매 프레임
        // 세션 업데이트 루프를 찾기 위한 것. 상한이 차면 아무것도 안 한다.
        if (g_reader != nullptr &&
            !any_pending() &&
            g_normal_traces.load(std::memory_order_acquire) < kNormalTraces) {
            log_normal_stack();
        }
    }
    bool grant_ran = false;
    if (g_detour_depth == 1) grant_ran = run_pending_if_any();

    // 지식 스킬 등록. **렌더 스레드에서 부르면 TLS 가 없어 죽는다**(1.8/1.13).
    //
    // 이 자리는 게임의 핫 패스다. 그래서 순서가 중요하다:
    //  1) `knowledge_has_pending()` 은 **원자 하나**만 읽는다 - 평소엔 여기서 끝난다.
    //  2) 지급이 이번 바퀴에 돌았으면 건너뛴다 - 한 디투어에 무거운 일을 겹쳐
    //     쌓지 않는다(지급 쪽이 교착으로 고생한 자리다).
    //  3) TLS 검사는 그 둘을 지난 뒤에만 한다.
    if (g_detour_depth == 1 && !grant_ran &&
        cdtb::game::knowledge_has_pending() && thread_ready_for_spawn()) {
        cdtb::game::knowledge_run_pending();
    }

    if (g_detour_depth == 1 && g_reader != nullptr &&
        !g_traced.load(std::memory_order_acquire) && thread_ready_for_spawn()) {
        if (!g_traced.exchange(true, std::memory_order_acq_rel)) {
            log_call_stack();
        }
    }

    --g_detour_depth;
    return actor;
}

// 게임 함수를 부르다 죽으면 오버레이가 통째로 내려간다 - 실측에서
// 그렇게 됐다. 예외를 여기서 막는다. 이 함수 안에는 소멸자를 가진
// 객체를 두지 않는다(__try 가 허용하지 않는다).
// TLS 사슬을 따라가다 죽지 않게 감싼다.
bool safe_deref(std::uintptr_t at, std::uintptr_t* out) {
    __try {
        *out = *reinterpret_cast<std::uintptr_t*>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// 예외 코드만으로는 어디서 죽었는지 알 수 없다. 터진 주소까지
// 받아 둔다 - 그 주소를 파일에서 디스어셈블하면 무엇을 참조하다
// 죽었는지 바로 보인다.
int seh_filter(EXCEPTION_POINTERS* ep, std::uint32_t* code,
               std::uintptr_t* addr) {
    *code = static_cast<std::uint32_t>(ep->ExceptionRecord->ExceptionCode);
    *addr = reinterpret_cast<std::uintptr_t>(
        ep->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}

using DeserFn = void*(__fastcall*)(void*, void*, void*, void*);
bool call_deser_guarded(DeserFn fn, void* descriptor, std::uint32_t* result,
                        void* packet, std::uint32_t* seh_out,
                        std::uintptr_t* fault_out) {
    __try {
        fn(descriptor, result, packet, nullptr);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, fault_out)) {
        return false;
    }
}

bool call_endur_guarded(EndurFn fn, void* self, void* packet,
                        const std::uint16_t* a, const std::uint16_t* b,
                        std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, a, b);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool call_give_guarded(GiveFn fn, void* self, void* packet, void* value,
                       std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, value);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

using HireSpeciesFn = std::uint32_t*(__fastcall*)(void*, std::uint32_t*,
                                                 std::uint16_t, std::uint32_t,
                                                 std::uint8_t);

bool call_hire_species_guarded(HireSpeciesFn fn, void* clan,
                               std::uint32_t* result, std::uint16_t key,
                               std::uint32_t* seh_out,
                               std::uintptr_t* addr_out) {
    __try {
        fn(clan, result, key, 1, 1);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool call_ctor_guarded(CtorFn fn, void* obj, std::uint32_t* seh_out,
                       std::uintptr_t* addr_out) {
    __try {
        fn(obj);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool call_handler_guarded(HandlerFn fn, void* self, void* packet,
                          const std::uint32_t* key, const std::int64_t* count,
                          const std::uint16_t* f3, const float* pos,
                          std::uint32_t* seh_out, std::uintptr_t* addr_out) {
    __try {
        fn(self, packet, key, count, f3, pos);
        return true;
    } __except (seh_filter(GetExceptionInformation(), seh_out, addr_out)) {
        return false;
    }
}

bool find_one(const std::vector<std::uint8_t>& image, const char* pattern,
              std::uint64_t* rva_out) {
    if (rva_out == nullptr || image.empty()) return false;
    const auto parsed = mem::parse_pattern(pattern);
    if (!parsed) return false;

    // 두 개까지만 모은다. 하나를 넘으면 어차피 고를 수 없다.
    const mem::Range range{image.data(), image.size()};
    const auto hits = mem::find_all(range, *parsed, 2);
    if (hits.size() != 1) return false;

    *rva_out = static_cast<std::uint64_t>(hits[0] - image.data());
    return true;
}

}  // namespace

bool find_actor_getter_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kActorGetterPattern, rva_out);
}

bool find_entity_lookup(const std::uint8_t* body, std::size_t n,
                        std::uint64_t body_rva, std::uint64_t* fn_rva) {
    if (body == nullptr || fn_rva == nullptr) return false;
    static const std::uint8_t kAnchor[] = {0x44, 0x8B, 0x03, 0x48, 0x8D, 0x54,
                                           0x24, 0x60, 0x48, 0x8B, 0x08, 0xE8};
    std::uint64_t found = 0;
    int hits = 0;
    if (n < sizeof(kAnchor) + 4) return false;
    for (std::size_t i = 0; i + sizeof(kAnchor) + 4 <= n; ++i) {
        if (std::memcmp(body + i, kAnchor, sizeof(kAnchor)) != 0) continue;
        std::int32_t rel = 0;
        std::memcpy(&rel, body + i + sizeof(kAnchor), sizeof(rel));
        found = body_rva + i + sizeof(kAnchor) + 4 +
                static_cast<std::uint64_t>(static_cast<std::int64_t>(rel));
        if (++hits > 1) return false;
    }
    if (hits != 1) return false;
    *fn_rva = found;
    return true;
}

bool entity_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_entity_installed) return true;
    if (g_stat_msg.handler == 0 &&
        !resolve_cheat_message(rtti, reader, "VaryStatCheatReq", &g_stat_msg)) {
        return false;
    }
    const auto& img = rtti.image();
    const std::uint64_t hrva = g_stat_msg.handler - reader.module_base();
    if (hrva + 0x900 > img.size()) return false;

    std::uint64_t fn = 0;
    if (!find_entity_lookup(img.data() + hrva, 0x900, hrva, &fn)) {
        log::warnf("엔티티 조회 함수를 못 찾았다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_entity_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(fn));
    if (!mem::hook_install(g_entity_target, &det_entity_lookup,
                           reinterpret_cast<void**>(&g_orig_entity))) {
        log::errorf("엔티티 조회 후킹 실패 (RVA 0x{:X})", fn);
        g_entity_target = nullptr;
        return false;
    }
    g_entity_installed = true;
    log::infof("엔티티 조회 후킹 설치 (RVA 0x{:X})", fn);
    return true;
}

int seen_entities(std::uint32_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_ent_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_ent[i];
        if (hits_out != nullptr) hits_out[i] = g_ent_hits[i];
    }
    return take;
}

bool find_task_dispatcher_rva(const std::vector<std::uint8_t>& image,
                              std::uint64_t* rva_out) {
    return find_one(image, kTaskDispatcherPattern, rva_out);
}

bool tick_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_tick_installed) return true;
    std::uint64_t rva = 0;
    if (!find_task_dispatcher_rva(rtti.image(), &rva)) {
        log::warnf("작업 디스패처를 찾지 못했다 - 안전 지점 없이 돈다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_dispatch_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_dispatch_target, &det_task_dispatch,
                           reinterpret_cast<void**>(&g_orig_dispatch))) {
        log::errorf("작업 디스패처 후킹 실패 (RVA 0x{:X})", rva);
        g_dispatch_target = nullptr;
        return false;
    }
    g_tick_installed = true;
    log::infof("작업 디스패처 후킹 설치 (RVA 0x{:X}) - 여기서만 실행한다", rva);
    return true;
}

bool tick_hook_installed() { return g_tick_installed; }

bool find_message_pump_rva(const std::vector<std::uint8_t>& image,
                           std::uint64_t* rva_out) {
    return find_one(image, kMessagePumpPattern, rva_out);
}

bool pump_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_pump_installed.load(std::memory_order_acquire)) return true;
    std::uint64_t rva = 0;
    if (!find_message_pump_rva(rtti.image(), &rva)) {
        log::warnf("메시지 펌프를 찾지 못했다 - 액터 조회 자리에서 실행한다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_pump_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_pump_target, &det_message_pump,
                           reinterpret_cast<void**>(&g_orig_pump))) {
        log::errorf("메시지 펌프 후킹 실패 (RVA 0x{:X})", rva);
        g_pump_target = nullptr;
        return false;
    }
    g_pump_installed.store(true, std::memory_order_release);
    log::infof("메시지 펌프 후킹 설치 (RVA 0x{:X}) - 돌아온 자리에서 실행한다",
               rva);
    return true;
}

void pump_hook_remove() {
    if (!g_pump_installed.load(std::memory_order_acquire)) return;
    g_pump_installed.store(false, std::memory_order_release);
    mem::hook_remove(g_pump_target);
    g_pump_target = nullptr;
    g_orig_pump = nullptr;
    log::infof("메시지 펌프 후킹 원복 (호출 {}회)",
               g_pump_calls.load(std::memory_order_relaxed));
}

bool pump_hook_installed() {
    return g_pump_installed.load(std::memory_order_acquire);
}

bool find_task_run_rva(const std::vector<std::uint8_t>& image,
                       std::uint64_t* rva_out) {
    return find_one(image, kTaskRunPattern, rva_out);
}

bool taskrun_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_taskrun_installed.load(std::memory_order_acquire)) return true;
    std::uint64_t rva = 0;
    if (!find_task_run_rva(rtti.image(), &rva)) {
        log::warnf("작업 실행 래퍼를 찾지 못했다");
        return false;
    }
    if (!mem::hook_init()) return false;
    g_taskrun_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_taskrun_target, &det_task_run,
                           reinterpret_cast<void**>(&g_orig_taskrun))) {
        log::errorf("작업 실행 래퍼 후킹 실패 (RVA 0x{:X})", rva);
        g_taskrun_target = nullptr;
        return false;
    }
    g_taskrun_installed.store(true, std::memory_order_release);
    log::infof("작업 실행 래퍼 후킹 설치 (RVA 0x{:X}) - 빈도만 잰다", rva);
    return true;
}

void taskrun_hook_remove() {
    if (!g_taskrun_installed.load(std::memory_order_acquire)) return;
    g_taskrun_installed.store(false, std::memory_order_release);
    mem::hook_remove(g_taskrun_target);
    g_taskrun_target = nullptr;
    g_orig_taskrun = nullptr;
    log::infof("작업 실행 래퍼 후킹 원복 (총 {}회)",
               g_taskrun_calls.load(std::memory_order_relaxed));
}

bool taskrun_hook_installed() {
    return g_taskrun_installed.load(std::memory_order_acquire);
}

bool actor_hook_install(const mem::Rtti& rtti, const mem::Reader& reader) {
    if (g_installed) return true;

    std::uint64_t rva = 0;
    if (!find_actor_getter_rva(rtti.image(), &rva)) {
        log::warnf("액터 조회 함수를 찾지 못했다 - 패치로 밀렸을 수 있다");
        return false;
    }
    if (!mem::hook_init()) return false;

    g_actor_getter_target = reinterpret_cast<void*>(
        reader.module_base() + static_cast<std::uintptr_t>(rva));
    if (!mem::hook_install(g_actor_getter_target, &det_actor_getter,
                           reinterpret_cast<void**>(&g_orig_actor_getter))) {
        log::errorf("액터 조회 후킹 실패 (RVA 0x{:X})", rva);
        g_actor_getter_target = nullptr;
        return false;
    }
    g_installed = true;
    log::infof("액터 조회 후킹 설치 (RVA 0x{:X})", rva);
    return true;
}

bool actor_hook_installed() { return g_installed; }

int note_actor(std::uintptr_t* slots, std::uint32_t* hits, int count, int cap,
               std::uintptr_t value) {
    if (slots == nullptr || hits == nullptr) return count;
    for (int i = 0; i < count; ++i) {
        if (slots[i] == value) {
            ++hits[i];
            return count;
        }
    }
    if (count >= cap) return count;
    slots[count] = value;
    hits[count] = 1;
    return count + 1;
}

int session_slot_for(const std::uintptr_t* slots,
                     const std::uint64_t* last_seen, int count, int cap,
                     std::uintptr_t value, bool* fresh_slot) {
    if (fresh_slot != nullptr) *fresh_slot = false;
    if (slots == nullptr || last_seen == nullptr || cap <= 0) return -1;
    for (int i = 0; i < count && i < cap; ++i) {
        if (slots[i] == value) return i;
    }
    if (fresh_slot != nullptr) *fresh_slot = true;
    if (count < cap) return count;
    // 꽉 찼다. 가장 오래 전에 본 자리를 내준다.
    int oldest = 0;
    for (int i = 1; i < cap; ++i) {
        if (last_seen[i] < last_seen[oldest]) oldest = i;
    }
    return oldest;
}

int seen_sessions(std::uintptr_t* out, std::uint32_t* hits_out, int cap) {
    const int n = g_sess_count.load(std::memory_order_acquire);
    const int take = (n < cap) ? n : cap;
    for (int i = 0; i < take; ++i) {
        out[i] = g_sess[i];
        if (hits_out != nullptr) hits_out[i] = g_sess_hits[i];
    }
    return take;
}

std::uintptr_t last_actor() {
    return g_last_actor.load(std::memory_order_relaxed);
}

int best_actor_index(const std::uint32_t* hits, const bool* is_server, int n) {
    if (hits == nullptr || is_server == nullptr) return -1;
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (!is_server[i]) continue;
        if (best < 0 || hits[i] > hits[best]) best = i;
    }
    return best;
}

int session_capacity() { return kSeenCap; }

int best_gate_session_index(const bool* gate_open, const std::uint32_t* hits,
                            const bool* is_server, int n) {
    if (gate_open == nullptr || hits == nullptr || is_server == nullptr) {
        return -1;
    }
    int best = -1;
    std::uint32_t best_hits = 0;
    for (int i = 0; i < n; ++i) {
        if (!is_server[i] || !gate_open[i]) continue;
        // 같으면 뒤엣것을 잡는다 - 표는 뒤로 갈수록 새 세션이다.
        if (best < 0 || hits[i] >= best_hits) {
            best = i;
            best_hits = hits[i];
        }
    }
    return best;
}

std::uintptr_t pick_drive_session(const mem::Reader& reader) {
    std::uintptr_t seen[kSeenCap]{};
    std::uint32_t hits[kSeenCap]{};
    const int n = seen_sessions(seen, hits, kSeenCap);
    if (n == 0) return 0;
    bool server[kSeenCap]{};
    bool gate_open[kSeenCap]{};
    std::uintptr_t gate = 0;
    for (int i = 0; i < n; ++i) {
        server[i] = session_is_server(i);
        // 안전 읽기라 풀린 세션은 여기서 자연히 실패한다.
        gate_open[i] = gate_object(reader, seen[i], &gate);
    }
    const int pick = best_gate_session_index(gate_open, hits, server, n);
    if (pick < 0) return 0;
    const std::uintptr_t session = seen[pick];
    // 새 세션을 잡았으면 지난 고장 잠금은 의미가 없다.
    if (session != drive_fault_session()) clear_drive_fault();
    return session;
}

std::uintptr_t session_actor(int index) {
    if (index < 0 || index >= kSeenCap) return 0;
    return g_sess_actor[index];
}

void set_session_class(int index, const char* name) {
    if (index < 0 || index >= kSeenCap || name == nullptr) return;
    std::size_t i = 0;
    for (; i + 1 < sizeof(g_sess_class[0]) && name[i] != 0; ++i) {
        g_sess_class[index][i] = name[i];
    }
    g_sess_class[index][i] = 0;
    // 이름 안에 Server 가 들어 있으면 서버 쪽이다.
    g_sess_server[index] = std::strstr(name, "Server") != nullptr;
}

const char* session_class(int index) {
    if (index < 0 || index >= kSeenCap) return "";
    return g_sess_class[index];
}

bool session_is_server(int index) {
    if (index < 0 || index >= kSeenCap) return false;
    return g_sess_server[index];
}

std::uint64_t session_last_seen(int index) {
    if (index < 0 || index >= kSeenCap) return 0;
    return g_sess_last[index];
}

int best_live_session_index(const std::uint32_t* hits, const bool* is_server,
                            const std::uint64_t* last_seen, int n,
                            std::uint64_t now_ms, std::uint64_t max_age_ms) {
    if (hits == nullptr || is_server == nullptr || last_seen == nullptr) {
        return -1;
    }
    // 서버 쪽 중 가장 최근에 본 시각. 견줄 기준이 된다.
    std::uint64_t newest = 0;
    for (int i = 0; i < n; ++i) {
        if (!is_server[i] || last_seen[i] == 0) continue;
        if (last_seen[i] > newest) newest = last_seen[i];
    }
    if (newest == 0) return -1;
    // 가장 최근 것조차 오래됐으면 전부 멈춘 것이다 - 로딩 화면.
    if (now_ms > newest && now_ms - newest > kSessionDeadMs) return -1;
    int best = -1;
    for (int i = 0; i < n; ++i) {
        if (!is_server[i] || last_seen[i] == 0) continue;
        if (newest - last_seen[i] > max_age_ms) continue;
        if (best < 0 || hits[i] > hits[best]) best = i;
    }
    return best;
}

bool session_looks_live(const mem::Reader& reader, std::uintptr_t session) {
    if (session == 0) return false;
    // 사용자 공간 주소인가. 커널 쪽이나 정렬이 어긋난 값은 세션이
    // 아니다.
    if (session < 0x10000 || session >= 0x7FFFFFFFFFFF) return false;
    if ((session & 7) != 0) return false;
    std::uintptr_t gate = 0;
    if (!reader.read_value(session + 0x88, &gate)) return false;
    if (gate < 0x10000 || gate >= 0x7FFFFFFFFFFF) return false;
    if ((gate & 7) != 0) return false;
    // 처리기는 여기서 바이트 하나를 본다. 읽히지 않으면 그 자리에서
    // 죽는다 - 우리가 먼저 읽어 본다.
    std::uint8_t flag = 0;
    if (!reader.read_value(gate + 1, &flag)) return false;
    return true;
}

DriveGate drive_gate_state(DriveLane lane) {
    LaneSlot& l = lane_of(lane);
    DriveGate g;
    g.pending = l.has.load(std::memory_order_acquire);
    g.running = g_running.load(std::memory_order_acquire);
    const unsigned long long now = GetTickCount64();
    const unsigned long long pat = l.at.load(std::memory_order_acquire);
    const unsigned long long rat = g_running_at.load(std::memory_order_acquire);
    g.pending_age_ms = (g.pending && pat != 0) ? now - pat : 0;
    g.running_age_ms = (g.running && rat != 0) ? now - rat : 0;
    const unsigned long long done = g_last_done.load(std::memory_order_acquire);
    g.cooldown_left_ms =
        (done != 0 && now - done < kCooldownMs) ? kCooldownMs - (now - done) : 0;
    g.fault_session = drive_fault_session();
    return g;
}

bool drive_gate_reset() {
    // 오래 물려 있을 때만 푼다. 진짜로 도는 중에 풀면 게임 스레드가
    // 쓰는 자리를 다른 요청이 덮어쓴다.
    // 레인 어느 쪽이든 30초 넘게 물려 있으면 그것만 푸다.
    bool stuck_pending = false;
    for (int i = 0; i < kDriveLaneCount; ++i) {
        const DriveGate lg = drive_gate_state(static_cast<DriveLane>(i));
        if (lg.pending && lg.pending_age_ms > 30000) stuck_pending = true;
    }
    const DriveGate g = drive_gate_state(DriveLane::Item);
    const bool stuck_running = g.running && g.running_age_ms > 30000;
    if (!stuck_pending && !stuck_running) return false;
    if (stuck_pending) {
        for (int i = 0; i < kDriveLaneCount; ++i) {
            const DriveGate lg = drive_gate_state(static_cast<DriveLane>(i));
            if (lg.pending && lg.pending_age_ms > 30000) {
                g_lane[i].has.store(false, std::memory_order_release);
            }
        }
    }
    if (stuck_running) {
        // 실행 중이던 것을 푸는 것은 그 호출이 게임 안에서 돌아오지
        // 않았다는 뜻이다. 그 스레드는 재귀 깊이가 박혀 다시 구동을
        // 서비스하지 못한다. 숨기지 말고 표시한다.
        g_running.store(false, std::memory_order_release);
        g_drive_dead.store(true, std::memory_order_release);
        log::errorf("구동 지점이 게임 안에서 멈췄다 - 게임을 다시 시작해야 한다");
    }
    log::warnf("구동 게이트를 손으로 풀었다 (대기 {}ms, 실행 {}ms)",
               g.pending_age_ms, g.running_age_ms);
    return true;
}

// 아래에 정의돼 있다.
bool clan_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out);

bool hire_species_ready() {
    return g_reader != nullptr && g_orig_actor_getter != nullptr;
}

bool request_hire_species(std::uintptr_t session, std::uint16_t char_key) {
    std::lock_guard<std::mutex> produce(g_produce_mutex);
    if (!hire_species_ready() || session == 0 || char_key == 0) return false;
    drop_stale_pending();
    LaneSlot& lane = lane_of(DriveLane::Companion);
    if (lane.has.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - lane_done_ms(lane) < kCooldownMs) {
        return false;
    }
    if (g_drive_fault.load(std::memory_order_acquire) == session) return false;

    lane.req = Pending{};
    lane.req.kind = Kind::HireSpecies;
    lane.req.session = session;
    lane.req.key = char_key;
    stamp_request(lane);
    lane.at.store(GetTickCount64(), std::memory_order_release);
    lane.has.store(true, std::memory_order_release);
    log::infof("종 등록 요청을 걸었다 (키 {}) - 게임 스레드를 기다린다",
               char_key);
    return true;
}

std::uintptr_t drive_fault_session() {
    return g_drive_fault.load(std::memory_order_acquire);
}

void set_extra_drive_sites(const std::uint64_t* rvas, int n) {
    if (rvas == nullptr || n <= 0) {
        g_extra_drive_count.store(0, std::memory_order_release);
        return;
    }
    if (n > kMaxExtraDriveSites) n = kMaxExtraDriveSites;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (rvas[i] == 0) continue;   // 0 은 "모르는 자리" 표식이라 받지 않는다
        g_extra_drive_sites[k++] = rvas[i];
    }
    g_extra_drive_count.store(k, std::memory_order_release);
    if (k > 0) {
        std::string line;
        char buf[24];
        for (int i = 0; i < k; ++i) {
            std::snprintf(buf, sizeof(buf), " +%llX",
                          static_cast<unsigned long long>(
                              g_extra_drive_sites[i]));
            line += buf;
        }
        log::infof("구동 자리 ini 추가 {}곳:{}", k, line);
    }
}

int drive_site_count() {
    return static_cast<int>(std::size(kGoodDriveSites)) +
           g_extra_drive_count.load(std::memory_order_acquire);
}

void clear_drive_fault() {
    g_drive_fault.store(0, std::memory_order_release);
}

namespace {

// modrm 이 [reg] (mod=00, rm ∈ {rax,rcx,rdx,rbx,rsi,rdi}) 인가.
// rm=4 는 SIB, rm=5 는 RIP 상대라 뺀다.
bool is_deref_reg(std::uint8_t modrm) {
    if ((modrm & 0xC0) != 0) return false;
    const std::uint8_t rm = modrm & 7;
    return rm == 0 || rm == 1 || rm == 2 || rm == 3 || rm == 6 || rm == 7;
}

// 본문 앞쪽 어딘가에서 `xor src,src` 로 src 를 0 으로 만들었는가.
// 성공코드 0 을 레지스터로 저장하는 새 컴파일러 관용구를 알아보려면
// 그 레지스터가 정말 0 인지 봐야 한다.
bool xored_zero_before(const std::uint8_t* body, std::size_t pos,
                       std::uint8_t src) {
    const std::uint8_t modrm = static_cast<std::uint8_t>(0xC0 | (src << 3) | src);
    for (std::size_t j = 0; j + 1 < pos; ++j) {
        if ((body[j] == 0x31 || body[j] == 0x33) && body[j + 1] == modrm) {
            // 바로 앞이 REX.R/B 면 r8~r15 이라 다른 레지스터다.
            if (j > 0 && body[j - 1] >= 0x44 && body[j - 1] <= 0x4F) continue;
            return true;
        }
    }
    return false;
}

// 호출 바로 앞(24바이트)에 다섯 번째 이상 인자를 스택으로 넘기는
// 저장(`mov [rsp+disp], reg`, disp>=0x20)이 있는가. 그림자 영역
// 0x00~0x18 은 앞 네 레지스터 인자 몫이고, 0x20 이상은 5번째부터다.
// 우리 처리기는 (self, packet, &struct) 3인자라 이게 없다 - 스트림
// 경로 처리기(5인자)를 가려낸다.
bool has_stack_arg_before(const std::uint8_t* body, std::size_t call_pos) {
    const std::size_t lo = call_pos > 24 ? call_pos - 24 : 0;
    for (std::size_t j = lo; j + 5 <= call_pos; ++j) {
        if ((body[j] == 0x48 || body[j] == 0x4C) && body[j + 1] == 0x89 &&
            body[j + 2] == 0x44 && body[j + 3] == 0x24 && body[j + 4] >= 0x20) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool find_handler_call(const std::uint8_t* body, std::size_t n,
                       std::uint64_t body_rva, std::uint64_t* handler_rva) {
    if (body == nullptr || handler_rva == nullptr || n < 11) return false;

    // 함수 끝에서 멈춘다. 넘어가면 옆 함수에서도 맞아 둘이 된다 -
    // 실측에서 그렇게 실패했다. MSVC 는 함수 사이를 int3 로 채운다.
    for (std::size_t i = 0; i + 4 <= n; ++i) {
        if (body[i] == 0xCC && body[i + 1] == 0xCC && body[i + 2] == 0xCC &&
            body[i + 3] == 0xCC) {
            n = i;
            break;
        }
    }
    if (n < 11) return false;

    // 처리기 호출은 `call rel32` 뒤에 성공코드 0 을 결과 포인터에
    // 저장하는 것으로 알아본다. 그 저장이 두 꼴로 나온다.
    //   C7 /0 [reg] 00000000       mov dword [reg], 0     (즉시값)
    //   89 /r  [reg]                mov dword [reg], src   (src 는 0)
    // 2026-09-04 업데이트에서 컴파일러가 즉시값 대신 미리 xor 로 0 을
    // 만든 레지스터를 쓰기 시작해, 즉시값만 보던 옛 코드가 give 의
    // 엉뚱한(스트림 경로) 처리기를 집었다 - 성공은 뜨나 아이템은
    // 안 생겼다. 두 꼴을 다 본다.
    struct Cand {
        std::size_t call_pos;
        std::uint64_t target;
    };
    Cand cands[16];
    std::size_t ncand = 0;

    for (std::size_t i = 5; i + 2 <= n && ncand < 16; ++i) {
        if (body[i - 5] != 0xE8) continue;
        bool store = false;
        if (body[i] == 0xC7 && i + 6 <= n && is_deref_reg(body[i + 1]) &&
            body[i + 2] == 0 && body[i + 3] == 0 && body[i + 4] == 0 &&
            body[i + 5] == 0) {
            store = true;
        } else if (body[i] == 0x89 && is_deref_reg(body[i + 1])) {
            // 앞바이트가 REX 면 32비트 저장이 아니다. 순수 89 /r 만.
            const bool rex = (i >= 1 && body[i - 1] >= 0x40 && body[i - 1] <= 0x4F);
            const std::uint8_t src = static_cast<std::uint8_t>((body[i + 1] >> 3) & 7);
            if (!rex && xored_zero_before(body, i, src)) store = true;
        }
        if (!store) continue;

        std::int32_t rel = 0;
        std::memcpy(&rel, body + i - 4, sizeof(rel));
        const std::uint64_t target =
            body_rva + i +
            static_cast<std::uint64_t>(static_cast<std::int64_t>(rel));
        // 같은 대상은 한 번만.
        bool dup = false;
        for (std::size_t k = 0; k < ncand; ++k) {
            if (cands[k].target == target) { dup = true; break; }
        }
        if (!dup) cands[ncand++] = Cand{i - 5, target};
    }

    if (ncand == 0) return false;
    if (ncand == 1) {
        *handler_rva = cands[0].target;
        return true;
    }

    // 여럿이면 5인자 스트림 경로를 빼고 3인자만 남긴다.
    std::uint64_t kept = 0;
    std::size_t nkept = 0;
    for (std::size_t k = 0; k < ncand; ++k) {
        if (has_stack_arg_before(body, cands[k].call_pos)) continue;
        kept = cands[k].target;
        ++nkept;
    }
    if (nkept != 1) return false;
    *handler_rva = kept;
    return true;
}

bool spawn_args_ok(std::uint32_t item_key, std::int64_t count) {
    return item_key != 0 && count > 0;
}

bool gate_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out) {
    if (out == nullptr || session == 0) return false;
    std::uintptr_t p = 0;
    if (!reader.read(session + 0xA0, &p, sizeof(p)) || p == 0) return false;
    if (!reader.read(p + 0x68, &p, sizeof(p)) || p == 0) return false;
    if (!reader.read(p + 0x130, &p, sizeof(p)) || p == 0) return false;
    *out = p;
    return true;
}

// 용병단(MercenaryClanActorComponent). 문 객체와 같은 사슬인데 끝만
// 다르다 - 문은 +0x130, 용병단은 +0x110.
// 하나의 세션에서 사슬을 따라간다. 끊기면 false.
bool clan_from_session(const mem::Reader& reader, std::uintptr_t session,
                       std::uintptr_t* out) {
    if (out == nullptr || session == 0) return false;
    // 2454 역직렬화(0x2965662)가 쓰는 사슬 그대로다.
    //   mov rcx, [세션+0x68] / mov rcx, [rcx+0x110]
    // 문 객체(gate_object)가 쓰는 +0xA0 한 단계는 여기 없다 - 그것은
    // 세션 vtable[0x160] 이 돌려주는 스포너를 흉내낸 것이고 용병단은
    // 세션에서 바로 간다. 실측 2026-09-08으로 확인했다.
    std::uintptr_t p = 0;
    if (!reader.read(session + 0x68, &p, sizeof(p)) || p == 0) return false;
    if (!reader.read(p + 0x110, &p, sizeof(p)) || p == 0) return false;
    *out = p;
    return true;
}

// 용병단(MercenaryClanActorComponent)을 찾는다.
//
// **세션마다 달려 있지 않다.** 실측 2026-09-08: 고른 세션에서는
// [컴포넌트+0x110] 이 0 이었다(+0x130 의 문 객체는 살아 있었다).
// 세션이 여섯 개인데 용병단은 그중 일부에만 붙는다. 그래서 준 세션을
// 먼저 보고, 없으면 본 세션 전부를 훑는다.
bool clan_object(const mem::Reader& reader, std::uintptr_t session,
                 std::uintptr_t* out) {
    if (out == nullptr) return false;
    if (clan_from_session(reader, session, out)) return true;
    for (int i = 0; i < kSeenCap; ++i) {
        const std::uintptr_t s = g_sess[i];
        if (s == 0 || s == session) continue;
        if (clan_from_session(reader, s, out)) {
            // 세션 0 으로 부른 명부 탐색(값싼 길)은 매 바퀴 오므로 조용히 간다 -
            // 찾은 쪽(discover_clan)이 한 번 남긴다.
            if (session != 0) {
                log::infof("용병단은 세션 0x{:X} 에 있다 (고른 세션 0x{:X} 에는 없다)",
                           s, session);
            }
            return true;
        }
    }
    return false;
}

void log_gate(const mem::Rtti& rtti, const mem::Reader& reader,
              std::uintptr_t session) {
    std::uintptr_t obj = 0;
    if (!gate_object(reader, session, &obj)) {
        log::warnf("문: 세션 0x{:X} 에서 사슬이 끊겼다", session);
        return;
    }
    std::uintptr_t vtable = 0;
    if (!reader.read(obj, &vtable, sizeof(vtable))) return;
    log::infof("문 객체 0x{:X} ({}) vtable 0x{:X}", obj,
               rtti.class_of_object(obj), vtable);
    // 무리별 슬롯. 이 셋만 보면 35개가 다 덮인다.
    for (const std::uint32_t slot : {0xD0u, 0x140u, 0x160u}) {
        std::uintptr_t fn = 0;
        if (!reader.read(vtable + slot, &fn, sizeof(fn)) || fn == 0) continue;
        log::infof("  +0x{:X} -> 0x{:X} (RVA 0x{:X})", slot, fn,
                   fn - reader.module_base());
    }
}

bool resolve_cheat_message(const mem::Rtti& rtti, const mem::Reader& reader,
                           const char* class_name, CheatMessage* out) {
    if (out == nullptr || class_name == nullptr) return false;

    const auto types = rtti.find_types(class_name, 2);
    if (types.size() != 1) {
        log::warnf("치트 메시지 {}: 클래스가 {}개 - 고를 수 없다", class_name,
                   types.size());
        return false;
    }
    const auto vts = rtti.vtables_for(types[0].descriptor);
    if (vts.empty()) return false;
    const std::uintptr_t vtable = vts[0];
    const std::uint64_t vtable_rva = vtable - reader.module_base();

    // 정적 초기화가 vtable 을 넣는 전역이 곧 메시지 서술자다.
    //   48 8D 05 <-vtable>   lea rax, [rip+..]
    //   48 89 05 <-서술자>   mov [rip+..], rax
    const auto& img = rtti.image();
    std::uint64_t desc_rva = 0;
    int hits = 0;
    for (std::size_t i = 0; i + 14 <= img.size(); ++i) {
        if (img[i] != 0x48 || img[i + 1] != 0x8D || img[i + 2] != 0x05) continue;
        if (img[i + 7] != 0x48 || img[i + 8] != 0x89 || img[i + 9] != 0x05) {
            continue;
        }
        std::int32_t d1 = 0, d2 = 0;
        std::memcpy(&d1, img.data() + i + 3, 4);
        std::memcpy(&d2, img.data() + i + 10, 4);
        if (static_cast<std::uint64_t>(i + 7 + d1) != vtable_rva) continue;
        desc_rva = static_cast<std::uint64_t>(i + 14 + d2);
        if (++hits > 1) break;
    }
    if (hits != 1) {
        log::warnf("치트 메시지 {}: 서술자를 {}곳에서 찾았다", class_name, hits);
        return false;
    }

    CheatMessage m;
    m.descriptor = reader.module_base() + static_cast<std::uintptr_t>(desc_rva);

    // 서술자의 vptr 이 그 vtable 이어야 한다. 아니면 해석이 틀렸다.
    std::uintptr_t vptr = 0;
    if (!reader.read(m.descriptor, &vptr, sizeof(vptr)) || vptr != vtable) {
        log::warnf("치트 메시지 {}: 서술자 vptr 이 다르다", class_name);
        return false;
    }
    reader.read(m.descriptor + 0x0C, &m.id, sizeof(m.id));

    // vtable[2] 가 역직렬화 함수다.
    std::uintptr_t deser = 0;
    if (!reader.read(vtable + 0x10, &deser, sizeof(deser))) return false;
    const std::uint64_t deser_rva = deser - reader.module_base();
    if (deser_rva + 0x600 > img.size()) return false;

    std::uint64_t handler_rva = 0;
    if (!find_handler_call(img.data() + deser_rva, 0x600, deser_rva,
                           &handler_rva)) {
        log::warnf("치트 메시지 {}: 처리기 호출을 못 찾았다", class_name);
        return false;
    }
    m.handler = reader.module_base() + static_cast<std::uintptr_t>(handler_rva);

    log::infof("치트 메시지 {}: ID {} 서술자 0x{:X} 처리기 0x{:X}", class_name,
               m.id, m.descriptor, m.handler);
    *out = m;
    return true;
}

bool grant_resolve_messages(const mem::Rtti& rtti, const mem::Reader& reader) {
    g_reader = &reader;

    // 인벤토리 직행도 같이 해석한다.
    if (g_give_msg.handler == 0) {
        resolve_cheat_message(rtti, reader,
                              "CreateItemFromTrItemValueCheatReq", &g_give_msg);
    }
    if (g_item_value_ctor == nullptr) {
        std::uint64_t rva = 0;
        if (find_one(rtti.image(), kItemValueCtorPattern, &rva)) {
            g_item_value_ctor = reinterpret_cast<CtorFn>(
                reader.module_base() + static_cast<std::uintptr_t>(rva));
            log::infof("TrItemValue 생성자 확보 (RVA 0x{:X})", rva);
        } else {
            log::warnf("TrItemValue 생성자를 찾지 못했다");
        }
    }


    if (g_endur_msg.handler == 0) {
        resolve_cheat_message(rtti, reader, "VaryEnduranceItemByCheatReq",
                              &g_endur_msg);
    }
    return true;
}

bool thread_ready_for_spawn() {
    // gs:[0x58] 는 TEB 의 ThreadLocalStoragePointer 다. 작업 함수는
    // 거기서 꺼낸 블록의 +0x250 를 다시 따라가 쓴다. 그 사슬이
    // 서 있지 않은 스레드에서 부르면 널을 참조하고 죽는다.
    const std::uintptr_t tls = static_cast<std::uintptr_t>(__readgsqword(0x58));
    if (tls == 0) return false;
    std::uintptr_t slot0 = 0;
    if (!safe_deref(tls, &slot0) || slot0 == 0) return false;
    std::uintptr_t block = 0;
    if (!safe_deref(slot0 + 0x250, &block) || block == 0) return false;
    return true;
}

namespace {

// 처리기(0x278B860)가 실제로 하는 판정을 그대로 재현해 어디서 빠지는지
// 잡는다. 처리기는 조용히 실패하므로(개체가 안 생김) 이 재현이 유일한
// 실측이다. 값을 POD 에 담고 로그는 __except 밖에서 한다.
struct CharTrace {
    int step = 0;                 // 마지막으로 통과한 단계
    std::uintptr_t spawner = 0;   // 세션 vtable[0x160] 결과
    int ctx_byte = -1;            // 0x1D48380 이 채우는 +0x10 바이트
    std::uintptr_t gate = 0;      // [스포너+0x68]+0x130
    int gate_ok = -1;             // 게이트 vtable[0x140](gate,0)
    std::uintptr_t record = 0;    // 키 조회 결과
    int ordinal = -1;             // record 의 u16 순번 (0xFFFF 면 없음)
};

// 검사 자리를 부르기 전에 썽크와 본체 프롤로그를 읽어 본다. 읽기 실패와
// 불일치를 로그에서 구분한다(리뷰 관찰 2026-09-11).
static bool hire_check_site_ok(const mem::Reader& reader) {
    const std::uintptr_t base = reader.module_base();
    const std::uintptr_t site = base + kHireCheckRva;
    std::uint8_t thunk[5]{};
    if (!reader.read(site, thunk, sizeof(thunk))) {
        log::warnf("종 등록 검사: RVA 0x{:X} 를 읽지 못했다 - 부르지 않는다",
                   kHireCheckRva);
        return false;
    }
    const std::uintptr_t body = hire_check_jmp_target(thunk, sizeof(thunk), site);
    if (body == 0) {
        log::warnf("종 등록 검사: RVA 0x{:X} 첫 바이트 {:02X} 가 jmp(E9) 가 아니다 - "
                   "게임 갱신으로 밀린 자리라 부르지 않는다",
                   kHireCheckRva, thunk[0]);
        return false;
    }
    const std::uintptr_t body_rva = body >= base ? body - base : body;
    std::uint8_t head[sizeof(kHireCheckBodyPrologue)]{};
    if (!reader.read(body, head, sizeof(head))) {
        log::warnf("종 등록 검사: 썽크 대상 RVA 0x{:X} 를 읽지 못했다 - 부르지 않는다",
                   body_rva);
        return false;
    }
    if (!hire_check_body_ok(head, sizeof(head))) {
        log::warnf("종 등록 검사: 썽크 대상 RVA 0x{:X} 프롤로그가 다르다 ({:02X} {:02X} "
                   "{:02X} {:02X} {:02X}) - 다른 함수의 썽크다, 부르지 않는다",
                   body_rva, head[0], head[1], head[2], head[3], head[4]);
        return false;
    }
    return true;
}

void run_hire_species(std::uintptr_t session, std::uint16_t key,
                      SpawnOutcome* out) {
    SpawnOutcome o;
    if (out != nullptr) *out = o;
    if (g_reader == nullptr) return;

    std::uintptr_t clan = 0;
    if (!clan_object(*g_reader, session, &clan)) {
        o.no_actor = true;
        log::warnf("종 등록: 세션 0x{:X} 에서 용병단 사슬이 끊겼다", session);
        if (out != nullptr) *out = o;
        return;
    }

    auto fn = reinterpret_cast<HireSpeciesFn>(g_reader->module_base() +
                                              kHireCheckRva);
    // 고정 RVA 라 게임이 갱신되면 엉뚱한 자리를 부른다. 그 자리는 본체로 가는
    // jmp 썽크(E9)다 - 썽크를 따라가 본체 프롤로그까지 맞아야 부른다(2026-09-11
    // 2850 갱신 때 옛 자리가 함수 한복판이었다. E9 한 바이트만 보면 주변 E9 가
    // 4.7% 라 다른 함수의 썽크에 떨어질 수 있다 - 리뷰 관찰).
    if (!hire_check_site_ok(*g_reader)) {
        if (out != nullptr) *out = o;
        return;
    }
    std::uint32_t result = 0xFFFFFFFFu;
    o.called = true;
    o.crashed = !call_hire_species_guarded(fn, reinterpret_cast<void*>(clan),
                                           &result, key, &o.seh, &o.fault);
    if (o.crashed) {
        log::errorf("종 등록이 게임 안에서 죽었다: 0x{:X} at 0x{:X} (RVA 0x{:X})",
                    o.seh, o.fault, o.fault - g_reader->module_base());
    } else {
        log::infof("등록 검사: 행 {} -> 코드 0x{:08X} ({}). 검사일 뿐이라 "
                   "명부에는 들어가지 않는다",
                   key, result, result == 0 ? "통과" : "거부");
    }
    if (out != nullptr) *out = o;
}

// 인벤토리로 바로 넣는다. TLS 가 준비된 스레드에서만 부른다.
void run_give(std::uintptr_t session, std::uint32_t item_key,
              std::int64_t count, const GiveExtras& extras,
              SpawnOutcome* out) {
    SpawnOutcome o;
    std::uintptr_t gate = 0;
    if (!gate_object(*g_reader, session, &gate)) {
        o.no_actor = true;
        log::warnf("인벤토리 지급: 세션 0x{:X} 는 사슬이 끊겼다", session);
        if (out != nullptr) *out = o;
        return;
    }

    // 구조체는 게임 생성자에게 맡긴다. 기본값을 흉내내지 않는다.
    alignas(16) std::uint8_t value[kItemValueSize]{};
    o.called = true;
    if (!call_ctor_guarded(g_item_value_ctor, value, &o.seh, &o.fault)) {
        o.crashed = true;
        log::errorf("인벤토리 지급: TrItemValue 생성자에서 죽었다 0x{:X}", o.seh);
        if (out != nullptr) *out = o;
        return;
    }
    if (!fill_item_value(value, sizeof(value), item_key, count, extras)) {
        if (out != nullptr) *out = o;
        return;
    }

    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);

    log::infof("인벤토리 지급: 세션 0x{:X} 키 {} 개수 {} 담금질 {} 소켓 {}"
               " 내구도 {} 연마 {}",
               session, item_key, count, extras.temper,
               static_cast<int>(extras.socket_count), extras.endurance,
               extras.sharpness);
    o.crashed = !call_give_guarded(
        reinterpret_cast<GiveFn>(g_give_msg.handler),
        reinterpret_cast<void*>(g_give_msg.descriptor), packet, value, &o.seh,
        &o.fault);
    if (o.crashed) {
        log::errorf("인벤토리 지급이 게임 안에서 죽었다: 0x{:X} (RVA 0x{:X})",
                    o.seh, o.fault - g_reader->module_base());
    } else {
        log::infof("인벤토리 지급 끝");
    }
    if (out != nullptr) *out = o;
}

// 내구도. 문이 세션 자체의 vtable +0x160 이라 관리자 사슬 검사가
// 필요 없다. 그래도 세션은 서버 쪽이어야 한다.
void run_endurance(std::uintptr_t session, std::uint16_t a, std::uint16_t b,
                   SpawnOutcome* out) {
    SpawnOutcome o;
    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);
    std::uint16_t va = a;
    std::uint16_t vb = b;

    log::infof("내구도: 세션 0x{:X} a={} b={}", session, a, b);
    o.called = true;
    o.crashed = !call_endur_guarded(
        reinterpret_cast<EndurFn>(g_endur_msg.handler),
        reinterpret_cast<void*>(g_endur_msg.descriptor), packet, &va, &vb,
        &o.seh, &o.fault);
    if (o.crashed) {
        log::errorf("내구도가 게임 안에서 죽었다: 0x{:X} (RVA 0x{:X})", o.seh,
                    o.fault - g_reader->module_base());
    } else {
        log::infof("내구도 끝");
    }
    if (out != nullptr) *out = o;
}

}  // namespace

std::uintptr_t hire_check_jmp_target(const std::uint8_t* thunk, std::size_t n,
                                     std::uintptr_t thunk_addr) {
    if (thunk == nullptr || n < 5 || thunk[0] != 0xE9) return 0;
    std::int32_t rel = 0;
    std::memcpy(&rel, thunk + 1, sizeof(rel));
    return thunk_addr + 5 + static_cast<std::intptr_t>(rel);
}

bool hire_check_body_ok(const std::uint8_t* body, std::size_t n) {
    return body != nullptr && n >= sizeof(kHireCheckBodyPrologue) &&
           std::memcmp(body, kHireCheckBodyPrologue,
                       sizeof(kHireCheckBodyPrologue)) == 0;
}

bool endurance_ready() {
    return g_endur_msg.handler != 0 && g_reader != nullptr;
}

bool request_endurance(std::uintptr_t session, std::uint16_t a,
                       std::uint16_t b) {
    std::lock_guard<std::mutex> produce(g_produce_mutex);
    if (!endurance_ready() || session == 0) return false;
    drop_stale_pending();
    LaneSlot& lane = lane_of(DriveLane::Item);
    if (lane.has.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - lane_done_ms(lane) < kCooldownMs) {
        return false;
    }
    lane.req = Pending{};
    lane.req.kind = Kind::Endurance;
    lane.req.session = session;
    lane.req.a = a;
    lane.req.b = b;
    stamp_request(lane);
    lane.at.store(GetTickCount64(), std::memory_order_release);
    lane.has.store(true, std::memory_order_release);
    log::infof("내구도 요청을 걸었다");
    return true;
}

std::uint8_t clamp_socket_count(std::uint8_t requested, std::uint32_t room) {
    if (room > static_cast<std::uint32_t>(kGiveMaxSockets)) {
        room = static_cast<std::uint32_t>(kGiveMaxSockets);
    }
    if (static_cast<std::uint32_t>(requested) > room) {
        return static_cast<std::uint8_t>(room);
    }
    return requested;
}

int clamp_count_to_stack(int count, std::uint32_t max_stack) {
    if (count < 1) count = 1;
    if (max_stack == 0) return count;
    if (static_cast<std::int64_t>(count) >
        static_cast<std::int64_t>(max_stack)) {
        // 여기 왔다면 max_stack < count <= INT_MAX 이므로 좁혀도 된다.
        return static_cast<int>(max_stack);
    }
    return count;
}

bool fill_item_value(void* buf, std::size_t n, std::uint32_t item_key,
                     std::int64_t count, const GiveExtras& extras) {
    // 장비 연마 칸이 +0x1AE 까지 가므로 그만큼은 있어야 한다.
    // (소켓만 보고 0x60 으로 잡았다가 그 뒤를 쓰게 됐다.)
    if (buf == nullptr || n < 0x1B0) return false;
    if (extras.socket_count > kGiveMaxSockets) return false;

    auto* p = static_cast<std::uint8_t*>(buf);
    std::memcpy(p + 0x08, &item_key, sizeof(item_key));
    std::memcpy(p + 0x0C, &extras.temper, sizeof(extras.temper));
    std::memcpy(p + 0x10, &count, sizeof(count));
    std::memcpy(p + 0x2A, &extras.endurance, sizeof(extras.endurance));

    // 개수만큼만 옮긴다. 나머지 칸은 게임 생성자가 채운 빈 값
    // (FF FF 00 00 FF) 그대로 두어야 한다.
    for (int i = 0; i < extras.socket_count; ++i) {
        std::memcpy(p + 0x40 + i * kGiveSocketBytes, extras.sockets[i].raw,
                    kGiveSocketBytes);
    }
    p[0x5E] = extras.socket_count;
    std::memcpy(p + 0x1AE, &extras.sharpness, sizeof(extras.sharpness));
    return true;
}

bool give_ready() {
    return g_give_msg.handler != 0 && g_item_value_ctor != nullptr &&
           g_orig_actor_getter != nullptr && g_reader != nullptr;
}

namespace {

void run_message(std::uintptr_t session, const MessageDesc& msg,
                 const std::uint8_t* wire, std::size_t len, SpawnOutcome* out) {
    SpawnOutcome o;
    if (msg.deser == 0 || msg.descriptor == 0 || wire == nullptr || len < 5 ||
        len > kMessageWireMax) {
        log::warnf("메시지 구동: 인자 불량 (deser 0x{:X} len {})", msg.deser, len);
        if (out != nullptr) *out = o;
        return;
    }
    alignas(16) std::uint8_t buf[kMessageWireMax]{};
    std::memcpy(buf, wire, len);
    // 패킷: [+0x10] u16 전체길이 · [+0x18] 페이로드 포인터 · [+0x38] 플래그 0.
    // [+0x00] 은 성공 경로에서 안 쓰지만 실패 경로 대비로 세션을 둔다.
    std::uint64_t packet[8]{};
    packet[0] = static_cast<std::uint64_t>(session);
    packet[2] = static_cast<std::uint64_t>(len);           // +0x10
    packet[3] = reinterpret_cast<std::uint64_t>(&buf[0]);  // +0x18
    packet[7] = 0;                                         // +0x38
    std::uint32_t result = 0;
    log::infof("메시지 구동 ID {}: 세션 0x{:X} 길이 {}", msg.id, session, len);
    o.called = true;
    o.crashed = !call_deser_guarded(reinterpret_cast<DeserFn>(msg.deser),
                                    reinterpret_cast<void*>(msg.descriptor),
                                    &result, packet, &o.seh, &o.fault);
    o.result = result;
    if (o.crashed) {
        log::errorf("메시지 구동 ID {} 가 게임 안에서 죽었다: 0x{:X} (RVA 0x{:X})",
                    msg.id, o.seh,
                    g_reader != nullptr ? o.fault - g_reader->module_base() : o.fault);
    } else {
        log::infof("메시지 구동 ID {} 끝 (결과 {})", msg.id, result);
    }
    if (out != nullptr) *out = o;
}

}  // namespace

bool resolve_message(const mem::Rtti& rtti, const mem::Reader& reader,
                     const char* class_name, MessageDesc* out) {
    if (out == nullptr || class_name == nullptr) return false;
    const std::string want = std::string(".?AV") + class_name + "@pa@@";
    std::uintptr_t td = 0;
    for (const auto& t : rtti.find_types(class_name, 8)) {
        if (t.name == want) {
            td = t.descriptor;
            break;
        }
    }
    if (td == 0) {
        log::warnf("메시지 {}: 클래스를 못 찾음", class_name);
        return false;
    }
    const auto vts = rtti.vtables_for(td);
    if (vts.empty()) return false;
    const std::uintptr_t vtable = vts[0];
    const std::uint64_t vtable_rva = vtable - reader.module_base();

    // 정적 초기화가 vtable 을 넣는 전역이 곧 메시지 서술자다.
    //   48 8D 05 <-vtable>   lea rax,[rip+..]
    //   48 89 05 <-서술자>   mov [rip+..],rax
    const auto& img = rtti.image();
    std::uint64_t desc_rva = 0;
    int hits = 0;
    for (std::size_t i = 0; i + 14 <= img.size(); ++i) {
        if (img[i] != 0x48 || img[i + 1] != 0x8D || img[i + 2] != 0x05) continue;
        if (img[i + 7] != 0x48 || img[i + 8] != 0x89 || img[i + 9] != 0x05) {
            continue;
        }
        std::int32_t d1 = 0, d2 = 0;
        std::memcpy(&d1, img.data() + i + 3, 4);
        std::memcpy(&d2, img.data() + i + 10, 4);
        if (static_cast<std::uint64_t>(i + 7 + d1) != vtable_rva) continue;
        desc_rva = static_cast<std::uint64_t>(i + 14 + d2);
        if (++hits > 1) break;
    }
    if (hits != 1) {
        log::warnf("메시지 {}: 서술자를 {}곳에서 찾았다", class_name, hits);
        return false;
    }
    MessageDesc m;
    m.descriptor = reader.module_base() + static_cast<std::uintptr_t>(desc_rva);
    std::uintptr_t vptr = 0;
    if (!reader.read(m.descriptor, &vptr, sizeof(vptr)) || vptr != vtable) {
        log::warnf("메시지 {}: 서술자 vptr 이 다르다", class_name);
        return false;
    }
    reader.read(m.descriptor + 0x0C, &m.id, sizeof(m.id));
    if (!reader.read(vtable + 0x10, &m.deser, sizeof(m.deser)) || m.deser == 0) {
        return false;
    }
    log::infof("메시지 {}: ID {} 서술자 0x{:X} 역직렬화 RVA 0x{:X}", class_name,
               m.id, m.descriptor, m.deser - reader.module_base());
    *out = m;
    return true;
}

bool request_message(std::uintptr_t session, const MessageDesc& msg,
                     const std::uint8_t* wire, std::size_t len) {
    std::lock_guard<std::mutex> produce(g_produce_mutex);
    if (session == 0 || msg.deser == 0 || wire == nullptr) return false;
    if (len < 5 || len > kMessageWireMax) return false;
    if (g_reader == nullptr) return false;
    if (session == g_drive_fault.load(std::memory_order_acquire)) return false;
    drop_stale_pending();
    LaneSlot& lane = lane_of(DriveLane::Companion);
    if (lane.has.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - lane_done_ms(lane) < kCooldownMs) {
        return false;
    }
    lane.req = Pending{};
    lane.req.kind = Kind::Message;
    lane.req.session = session;
    lane.req.msg = msg;
    std::memcpy(lane.req.wire, wire, len);
    lane.req.wire_len = len;
    stamp_request(lane);
    lane.at.store(GetTickCount64(), std::memory_order_release);
    lane.has.store(true, std::memory_order_release);
    log::infof("메시지 구동 요청을 걸었다 (ID {} 길이 {}) - 게임 스레드를 기다린다",
               msg.id, len);
    return true;
}

bool request_give(std::uintptr_t session, std::uint32_t item_key,
                  std::int64_t count, const GiveExtras& extras) {
    std::lock_guard<std::mutex> produce(g_produce_mutex);
    if (!give_ready()) return false;
    if (!spawn_args_ok(item_key, count) || session == 0) return false;
    if (session == g_drive_fault.load(std::memory_order_acquire)) return false;
    drop_stale_pending();
    LaneSlot& lane = lane_of(DriveLane::Item);
    if (lane.has.load(std::memory_order_acquire)) return false;
    if (g_running.load(std::memory_order_acquire)) return false;
    if (GetTickCount64() - lane_done_ms(lane) < kCooldownMs) {
        return false;
    }

    lane.req = Pending{};
    lane.req.kind = Kind::Inventory;
    lane.req.session = session;
    lane.req.key = item_key;
    lane.req.count = count;
    // 소켓은 여기서 최종으로 자른다. 2026-09-04 에는 아예 0 으로 밀었는데,
    // 그 판정("업데이트가 소켓 전달을 없앴다")은 생성 함수 0x2A70000 의
    // **두 갈래 중 하나만 보고** 내린 오독이었다. 실제로는:
    //   - 겹치는 아이템 갈래: 소켓수>0 이면 오류 (0x2A7022C)
    //   - 장비 갈래:         `표 +0x238 >= 소켓수` 면 통과 (0x2A70341)
    // 그리고 필드 복사 함수 0x234F930 은 지금도 `+0x40` 의 소켓 바이트를
    // 옮긴다(레지스터 인덱스라 예전 오프셋 훑기에 안 잡혔다).
    // 근거: specs/2026-09-07-socket-grant-unlock-research.md.
    GiveExtras safe = extras;
    safe.socket_count =
        clamp_socket_count(extras.socket_count, socket_room_for(item_key));
    lane.req.extras = safe;
    stamp_request(lane);
    lane.at.store(GetTickCount64(), std::memory_order_release);
    lane.has.store(true, std::memory_order_release);
    log::infof("인벤토리 지급 요청을 걸었다 key={} count={} (담금질 {} 내구도 {}"
               " 연마 {} 소켓 {}/{}) - 게임 스레드를 기다린다",
               item_key, count, safe.temper, safe.endurance, safe.sharpness,
               static_cast<int>(safe.socket_count),
               static_cast<int>(extras.socket_count));
    return true;
}

bool drive_point_dead() {
    return g_drive_dead.load(std::memory_order_acquire);
}

bool spawn_pending(DriveLane lane) {
    return lane_of(lane).has.load(std::memory_order_acquire);
}

SpawnOutcome last_outcome() {
    std::lock_guard<std::mutex> lock(g_outcome_mutex);
    return g_outcome;
}

std::uint32_t last_request_serial() { return t_last_serial; }

void short_class_name(const char* mangled, char* out, std::size_t n) {
    if (n == 0) return;
    if (mangled == nullptr || mangled[0] == 0) {
        std::snprintf(out, n, "%s", "(확인 중)");
        return;
    }
    const char* p = std::strstr(mangled, ".?AV");
    const char* s = (p != nullptr) ? p + 4 : mangled;
    // "@pa@@" 같은 망글 꼬리는 사람 눈엔 잡음이다.
    std::size_t i = 0;
    while (s[i] != 0 && s[i] != '@' && i + 1 < n) {
        out[i] = s[i];
        ++i;
    }
    out[i] = 0;
}


}  // namespace cdtb::game
