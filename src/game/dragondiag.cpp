#include "game/dragondiag.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "core/log.h"
#include "game/wheelfill.h"
#include "mem/hook.h"
#include "mem/reader.h"
#include "mem/safe_read.h"

namespace cdtb::game {
namespace {

// 1.0.0.2850 확정 RVA (2026-09-14~15 재확인, disasm.py).
//
// **반증(2026-09-15 라이브)**: 실제 휠 클릭(사자 소환됨)은 게이트 0x2ACA250 을
// 전혀 안 밟았다 - 게이트는 2962 서버 메시지(무효 경로)에서만 불린다. 따라서
// 진짜 소환 경로를 찾으려면 실제 스폰 프리미티브 0x2A22DE0 을 직접 본다.
// 이 함수는 게이트(0x2ACA441) 포함 5곳에서 불린다; 어느 호출자가 휠 클릭의
// 스폰인지 _ReturnAddress 로 가린다.
constexpr std::uint64_t kGateRva = 0x2ACA250;   // (대조) 소환 게이트
constexpr std::uint64_t kSpawnRva = 0x2A22DE0;  // 실제 스폰 프리미티브
// 휠 소환 사슬의 두 지점 (2026-09-15 실측으로 자리를 고쳤다).
//
// v4 에서 `0x2ADBA00`(게이트를 부르는 함수)에 걸었는데 **A.T.A.G. 도 드래곤도
// 안 들어왔다.** 그 함수는 목록에서 부를 때 같은 다른 경로였다. 실제 휠 경로는
// 스폰 로그가 찍어 준 호출자 `+0x2B78493` 쪽이고, 거기서 뒤로 훑어 함수 시작을
// 찾았다 - `0x2B78330`(프롤로그 `48 89 5c 24 08 48 89 74 24 18 55 57 41 54 41 56`).
// 한 겹 위는 앞선 세션이 기록해 둔 `0x29411E0` 이다.
//
// 실측된 갈림: **A.T.A.G. 클릭은 스폰 프리미티브까지 가고(r8=1000019),
// 드래곤 클릭은 아무 줄도 안 남긴다.** 이 둘 중 어디까지 오는지가 다음을 정한다.
// 훅 자리는 **PE 예외 디렉터리(.pdata)로 확정한다.** `tools/rtti/funcstart.py`.
//
// 처음엔 "뒤로 훑어 `CC CC CC CC` 패딩 다음" 을 함수 시작으로 보는 어림짐작을
// 썼다가 셋 중 둘을 틀렸다(2026-09-15):
//   +0x2AC9E58 -> 짐작 0x2AC87C0 · 실제 **0x2AC9C10** (0x1450 빗나감)
//   0x2941350  -> 함수 시작이 아니라 **0x29411E0 안쪽(+0x170)**
// 함수 중간에 훅을 걸면 명령을 덮어써 게임을 팅기게 할 수 있다. 짐작으로 걸린
// 훅은 진입이 0건이었고, 그 0 건을 "드래곤이 안 온다" 로 읽을 뻔했다.
constexpr std::uint64_t kCallFnRva = 0x2B78330;    // 휠 소환 루틴
constexpr std::uint64_t kWheelFnRva = 0x29411E0;   // 그 한 겹 위
// 소환 경로가 **둘**이다. 스폰 로그의 호출자가 두 가지로 찍힌다 -
// `+0x2B78493`(함수 0x2B78330) 과 `+0x2AC9E58`(함수 0x2AC9C10). 하나만 걸어
// 두면 드래곤이 다른 쪽으로 갔을 때 또 "아무것도 없음" 이 나와 무의미해진다.
// 프롤로그 `48 8b c4 48 89 58 18 48 89 50 10 55 56 57 41 54` - 표준이다.
constexpr std::uint64_t kAltFnRva = 0x2AC9C10;     // 또 하나의 소환 경로

// **거부 코드를 내는 함수** (2026-09-15 밤, 디스어셈블로 확정).
//
// 사슬: 휠함수(0x29411E0) -> 휠소환(0x2B78330) -> 슬롯조회(0x2A22DE0)
//       -> **0x20170E0** -> 여기서 갈린다.
//
//   0x2017103  rsi = rdx          <- 두 번째 인자가 오류코드 버퍼다
//   0x201713B  xor r12d, r12d
//   0x201713E  mov [rsi], r12d    <- **0 = 성공**
//   0x2017141  mov rax, rsi       <- 반환값은 그 버퍼의 포인터다
//
// `[rsi]` 에 쓰는 자리가 **열한 곳**이다 - 성공 하나와 서로 다른 오류 코드 열.
// 각각 전역에서 값을 읽어 온다. 실행 중 게임에서 여덟 전역을 다 읽었다
// (2026-09-16): 0x6BBBC08=4257992575 · 0x6BBCA10=3825518385(범용, 세 자리가
// 공유) · 0x6BBCDE4=103837360 · 0x6BBC014=1172209432 · 0x6BBC0C0=300143221 ·
// 0x6BBC24C=2542652655 · 0x6BAA568=1773986448 · 0x6BBC008=4205559856.
//
// **앞서 "결과물은 포인터의 하위 32비트" 라고 적은 것은 틀렸다.** 바깥 함수는
// 그 포인터를 역참조한다:
//
//   0x2A22F40  call 0x20170E0
//   0x2A22F45  mov  eax, [rax]   <- 역참조. 이게 진짜 오류 코드다
//   0x2A22F49  je   성공경로
//
// 그러니 v8 이 찍던 `결과물` 은 처음부터 진짜 코드였고, **드래곤의 0 은 이
// 관문을 통과했다는 뜻**이다. 실제로 A.T.A.G. 가 받았던 4205559856 은 위
// 표의 0x6BBC008 과 정확히 같다(자리 0x20175A7).
constexpr std::uint64_t kErrFnRva = 0x20170E0;

// **마지막 관문** (2026-09-16 실측). 갈래 7 의 끝에서 이 조회가 빈손이면
// 0x20175A7 이 코드 4205559856(0xFAABC030) 을 낸다 - 드래곤이 받는 바로 그
// 값이다. 그 **앞의 카테고리 관문은 통과**한다(막혔다면 300143221 이 나온다).
// 즉 얹기로 바꾼 카테고리 5 자체는 슬롯이 받아 준다.
//
//   0x2096C30(주인, 카테고리, 행번호)
//     rcx += 0x18                   <- 카테고리별 해시 표의 뿌리
//     0x20910C0(뿌리, 카테고리)      -> 레코드+8 = {항목배열, 개수}
//     항목마다 0x20A1470(항목, 행번호) 로 맞춰 본다
//     못 찾으면 **전역 0x6BB80C0** 을 돌려준다
//       (+0x20 = 0xFFFF · +0x28 = -1 - 실행 중 게임에서 확인했다)
//
// 표 배치는 ReserveSlotInfoManager 와 같다:
//   +0x30 버킷 수 · +0x34 0이면 없음 · +0x40 버킷표(0x100 간격) · +0x48 레코드
//   버킷: [0] 항목 수 · +8 부터 {u32 키, u32 색인}
//   레코드: +0x04 u16 키 · +0x08 항목배열 · +0x10 개수
// **실제 소환 배달부** (2026-09-16). `0x2A22DE0` 의 꼬리는 분기가 없는
// 직선이라 늘 성공(0)을 쓴다 - 일은 전부 여기로 넘어간다.
//
//   0x2A23194  rcx = r15 ([r14+8])  · rdx = rsp+0x38 (작은 서술자)
//   0x2A2318C  r8  = 0x382A30 결과  · r9  = rbp+0x280 (0x2390F40 이 만든 것)
//   0x2A23197  call 0x292B040       <- 여기
//
// 말은 휠에서 불러 **실제로 나온다**(사용자 실측 2026-09-16). 드래곤은 여기까지
// 똑같이 오는데(코드=0) 안 나온다. 그러니 갈리는 자리는 이 함수 안이다.
constexpr std::uint64_t kDispRva = 0x292B040;
// 휠 소환이 이 함수를 부르고 **돌아오는** 자리. 다른 호출자는 안 찍는다.
constexpr std::uintptr_t kDispWheelRet = 0x2A2319C;
constexpr std::uint64_t kFindRva = 0x2096C30;
constexpr std::uint64_t kEmptyRecRva = 0x6BB80C0;
constexpr std::uintptr_t kMapOff = 0x18;
// **거부 코드를 직접 찍는다(2026-09-15).** 드래곤이 "호출할 수 없는 장소입니다" 로
// 막히는데 후보가 여럿이다(`eErrNoCallVehicleInvalidPosition` · `...InvalidAir` ·
// `...MercenaryIndoor` · `...MercenaryRegion` · `...BlockedSpawnPositionByObstacle` ·
// `eErrNoFailToFindSummonMercenaryPosition` …). 하나씩 찍어 보는 대신 **게임이
// 실제로 내는 코드를 읽는다.**
//
// 근거: 휠 소환 헬퍼가 스폰 결과를 보고 0 이 아니면 그 코드를 알림으로 실어 보낸다.
//
//   0x2B7849E  ebx = [rbp+0xA8]        스폰이 쓴 결과 코드
//   0x2B784A4  test ebx,ebx / je       0 이면 조용히 지나간다
//   0x2B784AC  esi = 0x3F5             알림 메시지 id
//   0x2B784BB  [rsp+0x43] = ebx        ★ 그 코드가 본문에 실린다
//
// 그래서 **알림 송신(AsyncRequestSend)** 에 걸고 본문 머리가 0x3F5 인 것만 찍는다.
// 다른 알림은 건드리지 않는다. 읽기만 하고 원본을 그대로 부른다.
constexpr std::uint64_t kNotifyRva = 0xFB7F060;
constexpr std::uint16_t kNotifyMsgId = 0x3F5;   // 클라 알림
constexpr std::size_t kNotifyCodeOff = 3;       // 본문 +3 에 u32 코드

// 게이트: void* gate(rcx, rdx=out, r8, r9). r9 하위16=검색 키. 4 레지스터뿐.
using GateFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t);
// 스폰: 관찰된 5곳 모두 rcx, rdx=out, r8d, r9, [rsp+0x20]=out5 로 부른다.
// 결과는 *rdx 에 쓰인다(호출자들이 그 자리를 읽는다). 반환값 rax 는 안 쓰임.
using SpawnFn = void*(__fastcall*)(void*, void*, std::uint32_t, void*, void*);

// 알림 송신: 진입부가 `rdi = r8`(본문) · `esi = r9w` · `r12d = dx` 로 받는다.
// 레지스터 넷을 그대로 흘려보내면 되므로 4인자로 선언한다(스택 인자를 안 쓴다).
using NotifyFn = void*(__fastcall*)(void*, std::uint64_t, void*, std::uint64_t);

GateFn g_orig_gate = nullptr;
// 바깥 함수도 레지스터 넷만 흘려보낸다(스택 인자는 호출자 프레임에 그대로 있다).
using CallFn = void*(__fastcall*)(void*, void*, void*, std::uint64_t);
CallFn g_orig_callfn = nullptr;
CallFn g_orig_wheelfn = nullptr;
CallFn g_orig_altfn = nullptr;
using ErrFn = void*(__fastcall*)(void*, std::uint32_t*, std::uint64_t, void*);
ErrFn g_orig_errfn = nullptr;
void* g_errfn_target = nullptr;
std::atomic<int> g_errfn_budget{300};
// 마지막 관문의 조회. 인자는 전부 정수·포인터라 평범한 디투어로 안전하다.
using FindFn = void*(__fastcall*)(void*, std::uint64_t, std::uint64_t,
                                  std::uint64_t);
FindFn g_orig_find = nullptr;
void* g_find_target = nullptr;
// 소환 배달부. **인자가 여섯이다** - 넷으로 선언했다가 게임을 팅기게 했다
// (2026-09-16, 로드 중 사망). 호출 자리가 분명히 여섯을 싣는다:
//
//   0x2A23176  [rsp+0x28] = rbp+0x4C0   6번째
//   0x2A23180  [rsp+0x20] = rsp+0x30    5번째
//   0x2A23185  r9 · r8 · rdx · rcx      1~4번째
//
// 넷짜리 디투어는 원본을 부를 때 **자기 프레임**을 새로 잡으므로, 원본이
// `[rsp+0x20]`·`[rsp+0x28]` 에서 읽는 것은 우리 스택의 쓰레기가 된다.
// "스택 인자는 호출자 프레임에 그대로 있다" 는 다른 훅의 주석을 그대로
// 가져다 쓴 것이 잘못이었다 - 그 함수들은 스택 인자를 **안 읽는다**.
using DispFn = void*(__fastcall*)(void*, void*, void*, void*, void*, void*);
DispFn g_orig_disp = nullptr;
void* g_disp_target = nullptr;
std::atomic<int> g_disp_budget{60};
// 한 줄 요약은 넉넉히, **목록 덤프만** 따로 조인다. 예전에 둘을 같은
// 예산으로 묶었다가 시작 직후 열거에서 24건을 다 태워, 정작 보고 싶은
// 클릭이 한 줄도 안 찍혔다(2026-09-16).
std::atomic<int> g_find_budget{400};
std::atomic<int> g_find_dump_budget{12};
// 휠 칸 등록 채우기. 종행을 못 박지 않으면 빈 말 칸까지 채워 버린다.
constexpr std::uintptr_t kEntrySpecies = 0x20;   // u16 종행
constexpr std::uintptr_t kEntrySlot = 0x148;     // u16 올려 둔 휠 칸
std::atomic<int> g_fill_log_budget{12};
void* g_altfn_target = nullptr;
std::atomic<int> g_altfn_budget{300};
void* g_callfn_target = nullptr;
void* g_wheelfn_target = nullptr;
std::atomic<int> g_callfn_budget{300};
std::atomic<int> g_wheelfn_budget{300};
SpawnFn g_orig_spawn = nullptr;
NotifyFn g_orig_notify = nullptr;
void* g_gate_target = nullptr;
void* g_spawn_target = nullptr;
void* g_notify_target = nullptr;
std::uintptr_t g_base = 0;
std::atomic<bool> g_installed{false};

std::atomic<int> g_gate_budget{1000};
std::atomic<int> g_spawn_budget{2000};
// 알림은 거부가 날 때만 찍히지만, 예산을 둬서 로그가 묻히지 않게 한다
// (TROUBLESHOOTING 6.19 - 한 줄이 로그를 묻는다).
std::atomic<int> g_notify_budget{200};
std::atomic<long> g_spawn_seq{0};

std::uintptr_t caller_rva(const void* ret) {
    const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(ret);
    return (g_base != 0 && raw > g_base) ? raw - g_base : raw;
}

// 알림 본문 머리가 0x3F5 인 것만 찍는다. **읽기만** 하고 그대로 흘려보낸다.
void* __fastcall det_notify(void* a1, std::uint64_t a2, void* body,
                            std::uint64_t a4) {
    if (body != nullptr) {
        const std::uintptr_t at = reinterpret_cast<std::uintptr_t>(body);
        std::uint8_t head[16] = {};
        // **SEH 로 감싼 읽기를 쓴다** - 본문이 늘 16바이트 이상이라는 보장이 없다.
        if (mem::safe_read_bytes(at, head, sizeof head)) {
            std::uint16_t id = 0;
            std::memcpy(&id, head, sizeof id);
            if (id == kNotifyMsgId &&
                g_notify_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
                std::uint32_t code = 0;
                std::memcpy(&code, head + kNotifyCodeOff, sizeof code);
                log::infof("클라 알림 0x3F5: 코드 0x{:08X} ({}) · 본문 "
                           "{:02X} {:02X} {:02X} {:02X} {:02X} {:02X} {:02X} "
                           "{:02X} {:02X} {:02X} {:02X} {:02X}",
                           code, code, head[0], head[1], head[2], head[3],
                           head[4], head[5], head[6], head[7], head[8], head[9],
                           head[10], head[11]);
            }
        }
    }
    return g_orig_notify(a1, a2, body, a4);
}

// 게이트를 부르는 바깥 함수. **들어오는지**를 본다 - 들어오는데 게이트 줄이
// 안 따라오면 거부는 이 안이고, 아예 안 들어오면 더 앞(UI)이다.
void* __fastcall det_callfn(void* a1, void* a2, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    if (g_callfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("휠소환 진입(0x2B78330): 호출자=+0x{:X} rcx=0x{:X} rdx=0x{:X}"
                   " r8=0x{:X} r9=0x{:X}",
                   caller_rva(ret), reinterpret_cast<std::uintptr_t>(a1),
                   reinterpret_cast<std::uintptr_t>(a2),
                   reinterpret_cast<std::uintptr_t>(a3), a4);
    }
    return g_orig_callfn(a1, a2, a3, a4);
}

// 휠 소환 사슬의 한 겹 위. 여기까지도 안 오면 거부는 UI 안이다.
void* __fastcall det_wheelfn(void* a1, void* a2, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    if (g_wheelfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("휠함수 진입(0x29411E0): 호출자=+0x{:X} rcx=0x{:X} rdx=0x{:X}"
                   " r8=0x{:X} r9=0x{:X}",
                   caller_rva(ret), reinterpret_cast<std::uintptr_t>(a1),
                   reinterpret_cast<std::uintptr_t>(a2),
                   reinterpret_cast<std::uintptr_t>(a3), a4);
    }
    return g_orig_wheelfn(a1, a2, a3, a4);
}

// 또 하나의 소환 경로(0x2AC9C10). 스폰 호출자가 `+0x2AC9E58` 로 찍히는 쪽이다.
void* __fastcall det_altfn(void* a1, void* a2, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    if (g_altfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("다른경로 진입(0x2AC9C10): 호출자=+0x{:X} rcx=0x{:X}"
                   " rdx=0x{:X} r8=0x{:X} r9=0x{:X}",
                   caller_rva(ret), reinterpret_cast<std::uintptr_t>(a1),
                   reinterpret_cast<std::uintptr_t>(a2),
                   reinterpret_cast<std::uintptr_t>(a3), a4);
    }
    return g_orig_altfn(a1, a2, a3, a4);
}

// 거부 코드를 내는 함수. **원본을 부른 뒤 rdx 가 가리키는 u32 를 읽는다** -
// 그것이 진짜 오류 코드다(0 = 성공). 반환값은 그 버퍼를 가리키는 포인터라,
// 바깥 함수가 역참조해 같은 값을 얻는다.
void* __fastcall det_errfn(void* a1, std::uint32_t* out_err, std::uint64_t a3,
                           void* a4) {
    const void* ret = _ReturnAddress();
    void* r = g_orig_errfn(a1, out_err, a3, a4);
    std::uint32_t code = 0xFFFFFFFFu;
    if (out_err != nullptr) {
        mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(out_err), &code,
                             sizeof code);
    }
    if (g_errfn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("거부코드(0x20170E0): 호출자=+0x{:X} 슬롯u16={} -> 코드={}"
                   " ({})",
                   caller_rva(ret), static_cast<std::uint16_t>(a3), code,
                   code == 0 ? "성공" : "거부");
    }
    return r;
}

// 카테고리 하나의 항목 배열을 찾는다. 표 배치는 ReserveSlotInfoManager 와
// 같다. 못 찾으면 false - 그건 정상이다(그 카테고리가 없을 수 있다).
bool find_category_list(std::uintptr_t owner, std::uint32_t cat,
                        std::uintptr_t* out_arr, std::uint32_t* out_cnt,
                        std::uintptr_t* out_rec) {
    const std::uintptr_t map = owner + kMapOff;
    std::uint32_t buckets = 0;
    std::uint32_t live = 0;
    if (!mem::safe_read_bytes(map + 0x30, &buckets, sizeof buckets)) return false;
    if (!mem::safe_read_bytes(map + 0x34, &live, sizeof live)) return false;
    if (buckets == 0 || live == 0) return false;
    std::uintptr_t table = 0;
    if (!mem::safe_read_bytes(map + 0x40, &table, sizeof table)) return false;
    const std::uintptr_t bucket = table + (cat % buckets) * 0x100;
    std::uint32_t n = 0;
    if (!mem::safe_read_bytes(bucket, &n, sizeof n)) return false;
    for (std::uint32_t i = 0; i < n && i < 64; ++i) {
        std::uint32_t key = 0;
        if (!mem::safe_read_bytes(bucket + i * 8 + 8, &key, sizeof key)) break;
        if (key != cat) continue;
        std::uint32_t idx = 0;
        std::uintptr_t recs = 0;
        std::uintptr_t rec = 0;
        if (!mem::safe_read_bytes(bucket + i * 8 + 12, &idx, sizeof idx)) break;
        if (!mem::safe_read_bytes(map + 0x48, &recs, sizeof recs)) break;
        if (!mem::safe_read_bytes(recs + idx * 8, &rec, sizeof rec)) break;
        std::uintptr_t arr = 0;
        std::uint32_t cnt = 0;
        if (!mem::safe_read_bytes(rec + 8, &arr, sizeof arr)) break;
        if (!mem::safe_read_bytes(rec + 0x10, &cnt, sizeof cnt)) break;
        if (arr == 0 || cnt > 4096) break;
        *out_arr = arr;
        *out_cnt = cnt;
        *out_rec = rec;
        return true;
    }
    return false;
}

// 카테고리 하나의 목록을 그대로 걸어 찍는다. **읽기만** 한다.
void dump_category_list(std::uintptr_t owner, std::uint32_t cat) {
    std::uintptr_t arr = 0;
    std::uintptr_t rec = 0;
    std::uint32_t cnt = 0;
    if (!find_category_list(owner, cat, &arr, &cnt, &rec)) {
        log::infof("  카테고리 {} 목록을 못 찾았다", cat);
        return;
    }
    log::infof("  카테고리 {} 레코드 0x{:X} - 항목 {}개", cat, rec, cnt);
    for (std::uint32_t k = 0; k < cnt && k < 32; ++k) {
        std::uintptr_t ent = 0;
        if (!mem::safe_read_bytes(arr + k * 8, &ent, sizeof ent)) break;
        std::uint16_t slot = 0xFFFF;
        std::uint16_t sp = 0xFFFF;
        mem::safe_read_bytes(ent + kEntrySlot, &slot, sizeof slot);
        mem::safe_read_bytes(ent + kEntrySpecies, &sp, sizeof sp);
        log::infof("    [{}] 0x{:X}  종행={}  칸={}", k, ent, sp, slot);
    }
}

// **지정한 종행의 항목만** 골라 휠 칸을 채운다. 조회가 바로 뒤에 일어나므로
// 조회가 묻는 칸 번호(`want`)를 그대로 적는다.
void fill_category_slots(std::uintptr_t owner, std::uint32_t cat,
                         std::uint16_t want) {
    const std::uint16_t* fill_rows = nullptr;
    const int n = wheel_fill_rows(&fill_rows);
    if (n <= 0 || want == 0xFFFF) return;
    std::uintptr_t arr = 0;
    std::uintptr_t rec = 0;
    std::uint32_t cnt = 0;
    if (!find_category_list(owner, cat, &arr, &cnt, &rec)) return;
    for (std::uint32_t k = 0; k < cnt; ++k) {
        std::uintptr_t ent = 0;
        if (!mem::safe_read_bytes(arr + k * 8, &ent, sizeof ent)) break;
        std::uint16_t slot = 0xFFFF;
        std::uint16_t sp = 0xFFFF;
        if (!mem::safe_read_bytes(ent + kEntrySlot, &slot, sizeof slot)) continue;
        if (!mem::safe_read_bytes(ent + kEntrySpecies, &sp, sizeof sp)) continue;
        if (!wheel_fill_wanted(slot, sp, fill_rows, n)) continue;
        if (!mem::safe_write_bytes(ent + kEntrySlot, &want, sizeof want)) continue;
        if (g_fill_log_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
            log::infof("휠 칸 등록: 종행 {} 를 카테고리 {} 의 칸 {} 에 올렸다"
                       " (항목 0x{:X})",
                       sp, cat, want, ent);
        }
    }
}

// **마지막 관문의 조회**. 빈손이면 드래곤이 받는 4205559856 이 나온다.
// 무엇을 찾고 있었는지와, 그 카테고리 목록에 실제로 무엇이 들어 있는지를
// 나란히 찍는다. 원본을 그대로 부르고 결과만 읽는다.
// **소환 배달부**. 말과 드래곤을 같은 자로 재려고 인자와 반환을 그대로 찍는다.
// 읽기만 한다.
void* __fastcall det_disp(void* a1, void* a2, void* a3, void* a4, void* a5,
                          void* a6) {
    const void* ret = _ReturnAddress();
    void* r = g_orig_disp(a1, a2, a3, a4, a5, a6);
    // **휠 경로만 찍는다.** 로드 중에 다른 호출자(+0x282B243)가 이 함수를
    // 자주 불러 예산을 다 태운다 - 정작 보고 싶은 클릭이 안 찍힌다.
    const bool from_wheel = (caller_rva(ret) == kDispWheelRet);
    if (from_wheel &&
        g_disp_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        // **출력 인자 둘을 읽는다.** 호출 자리가 둘 다 0 으로 초기화해서
        // 넘긴다(0x2A2311B · 0x2A23122) - 함수가 결과를 거기 쓴다는 뜻이다.
        // 반환값(0xF4240)은 말·A.T.A.G.·드래곤이 전부 같아서 뜻이 없었다.
        std::uint64_t o5 = 0;
        std::uint64_t o6 = 0;
        std::uint32_t slot_key = 0;
        mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(a5), &o5,
                             sizeof o5);
        mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(a6), &o6,
                             sizeof o6);
        mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(a3), &slot_key,
                             sizeof slot_key);
        log::infof("소환배달(0x292B040): 슬롯키={} a3=0x{:X}"
                   " -> 반환=0x{:X} · [a5]=0x{:X} · [a6]=0x{:X}",
                   slot_key, reinterpret_cast<std::uintptr_t>(a3),
                   reinterpret_cast<std::uintptr_t>(r), o5, o6);
    }
    return r;
}

void* __fastcall det_find(void* owner, std::uint64_t cat, std::uint64_t row,
                          std::uint64_t a4) {
    if (wheel_fill_enabled()) {
        fill_category_slots(reinterpret_cast<std::uintptr_t>(owner),
                            static_cast<std::uint32_t>(cat),
                            static_cast<std::uint16_t>(row));
    }
    void* r = g_orig_find(owner, cat, row, a4);
    if (g_find_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        const std::uintptr_t got = reinterpret_cast<std::uintptr_t>(r);
        const std::uintptr_t empty = g_base + kEmptyRecRva;
        std::uint16_t f20 = 0xFFFF;
        std::int64_t f28 = -1;
        mem::safe_read_bytes(got + 0x20, &f20, sizeof f20);
        mem::safe_read_bytes(got + 0x28, &f28, sizeof f28);
        log::infof("등록조회(0x2096C30): 카테고리={} 행={} -> {} "
                   "(+0x20={} +0x28={})",
                   static_cast<std::uint16_t>(cat),
                   static_cast<std::uint16_t>(row),
                   got == empty ? "빈손" : "찾음", f20, f28);
        if (g_find_dump_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
            dump_category_list(reinterpret_cast<std::uintptr_t>(owner),
                               static_cast<std::uint32_t>(cat));
        }
    }
    return r;
}

void* __fastcall det_gate(void* a1, void* out, void* a3, std::uint64_t a4) {
    const void* ret = _ReturnAddress();
    const std::uint16_t key = static_cast<std::uint16_t>(a4);
    void* r = g_orig_gate(a1, out, a3, a4);
    std::uint32_t result = 0xFFFFFFFFu;
    if (out != nullptr) std::memcpy(&result, out, sizeof(result));
    if (g_gate_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        log::infof("소환게이트: 키=0x{:X} 호출자=+0x{:X} 오류코드={}", key,
                   caller_rva(ret), result);
    }
    return r;
}

void* __fastcall det_spawn(void* rcx, void* out, std::uint32_t r8, void* r9,
                           void* a5) {
    const void* ret = _ReturnAddress();
    void* r = g_orig_spawn(rcx, out, r8, r9, a5);
    std::uint32_t result = 0xFFFFFFFFu;
    if (out != nullptr) std::memcpy(&result, out, sizeof(result));
    // **관문 통과 직후의 가상 호출 대상을 같이 찍는다** (2026-09-16).
    //
    //   0x2A23050  rax = [r14]            r14 = [arg1+8]
    //   0x2A23065  call qword ptr [rax + 0x208]
    //
    // 이 자리가 슬롯에 대한 **동작(호출 모션)을 거는 곳**으로 보인다.
    // 드래곤과 말이 서로 다른 함수로 가는지가 갈림이다. 새 훅을 걸지 않고
    // 이미 안전한 이 훅에서 값만 읽는다 - 읽기뿐이라 인자 위험이 없다.
    std::uintptr_t act_fn = 0;
    {
        std::uintptr_t r14 = 0;
        std::uintptr_t vt = 0;
        if (mem::safe_read_bytes(reinterpret_cast<std::uintptr_t>(rcx) + 8,
                                 &r14, sizeof r14) &&
            r14 > 0x10000 &&
            mem::safe_read_bytes(r14, &vt, sizeof vt) && vt > 0x10000) {
            mem::safe_read_bytes(vt + 0x208, &act_fn, sizeof act_fn);
        }
    }
    const long seq = g_spawn_seq.fetch_add(1, std::memory_order_relaxed);
    if (g_spawn_budget.fetch_sub(1, std::memory_order_relaxed) > 0) {
        // **게이트의 "결과" 와 뜻이 다르다.** 게이트는 오류 코드(0 = 오류 없음)
        // 이고, 여기 out 자리는 만들어진 객체다 - 큰 값이면 만든 것, 0 이면 못
        // 만든 것이다. 같은 이름으로 찍다가 거꾸로 읽었다(2026-09-15).
        log::infof("스폰0x2A22DE0[{}]: 호출자=+0x{:X} r8={} 결과물=0x{:X} ({})"
                   " · 동작함수=+0x{:X}",
                   seq, caller_rva(ret), r8, result,
                   result != 0 ? "거부" : "통과",
                   act_fn == 0 ? 0 : caller_rva(reinterpret_cast<void*>(act_fn)));
    }
    return r;
}

}  // namespace

bool dragondiag_install(const mem::Reader& reader) {
    if (g_installed.load(std::memory_order_acquire)) return true;

    g_base = reader.module_base();
    if (g_base == 0) return false;
    if (!mem::hook_init()) return false;

    g_errfn_target = reinterpret_cast<void*>(g_base + kErrFnRva);
    const bool errfn_ok = mem::hook_install(
        g_errfn_target, &det_errfn, reinterpret_cast<void**>(&g_orig_errfn));
    if (!errfn_ok) {
        log::errorf("거부코드 후킹 실패 (RVA 0x{:X})", kErrFnRva);
        g_errfn_target = nullptr;
    }

    g_disp_target = reinterpret_cast<void*>(g_base + kDispRva);
    const bool disp_ok = mem::hook_install(
        g_disp_target, &det_disp, reinterpret_cast<void**>(&g_orig_disp));
    if (!disp_ok) {
        log::errorf("소환배달 후킹 실패 (RVA 0x{:X})", kDispRva);
        g_disp_target = nullptr;
    }

    g_find_target = reinterpret_cast<void*>(g_base + kFindRva);
    const bool find_ok = mem::hook_install(
        g_find_target, &det_find, reinterpret_cast<void**>(&g_orig_find));
    if (!find_ok) {
        log::errorf("등록조회 후킹 실패 (RVA 0x{:X})", kFindRva);
        g_find_target = nullptr;
    }

    g_altfn_target = reinterpret_cast<void*>(g_base + kAltFnRva);
    const bool altfn_ok = mem::hook_install(
        g_altfn_target, &det_altfn, reinterpret_cast<void**>(&g_orig_altfn));
    if (!altfn_ok) {
        log::errorf("다른경로 후킹 실패 (RVA 0x{:X})", kAltFnRva);
        g_altfn_target = nullptr;
    }

    g_wheelfn_target = reinterpret_cast<void*>(g_base + kWheelFnRva);
    const bool wheelfn_ok = mem::hook_install(
        g_wheelfn_target, &det_wheelfn,
        reinterpret_cast<void**>(&g_orig_wheelfn));
    if (!wheelfn_ok) {
        log::errorf("휠함수 후킹 실패 (RVA 0x{:X})", kWheelFnRva);
        g_wheelfn_target = nullptr;
    }

    g_callfn_target = reinterpret_cast<void*>(g_base + kCallFnRva);
    const bool callfn_ok = mem::hook_install(
        g_callfn_target, &det_callfn,
        reinterpret_cast<void**>(&g_orig_callfn));
    if (!callfn_ok) {
        log::errorf("호출함수 후킹 실패 (RVA 0x{:X})", kCallFnRva);
        g_callfn_target = nullptr;
    }

    g_gate_target = reinterpret_cast<void*>(g_base + kGateRva);
    const bool gate_ok = mem::hook_install(
        g_gate_target, &det_gate, reinterpret_cast<void**>(&g_orig_gate));
    if (!gate_ok) {
        log::errorf("소환게이트 후킹 실패 (RVA 0x{:X})", kGateRva);
        g_gate_target = nullptr;
    }

    g_spawn_target = reinterpret_cast<void*>(g_base + kSpawnRva);
    const bool spawn_ok = mem::hook_install(
        g_spawn_target, &det_spawn, reinterpret_cast<void**>(&g_orig_spawn));
    if (!spawn_ok) {
        log::errorf("스폰 후킹 실패 (RVA 0x{:X})", kSpawnRva);
        g_spawn_target = nullptr;
    }

    // **알림(0xFB7F060) 훅은 뗐다 (2026-09-15).** 게임이 팅겼고 죽은 자리가
    // 그 함수 안이었다(`RIP 모듈+0x0FB7F0DE`, RCX·RDI 가 32비트로 잘린 값).
    //
    // 원인은 주석에 이미 적혀 있었다 - 이 함수는 진입부가 `rdi = r8` · `esi =
    // r9w` · **`r12d = dx`** 로 받는다. `r12` 는 인자 레지스터가 아니다. 평범한
    // C++ 디투어는 원본을 부르기 **전에** 자기 용도로 r12 를 쓸 수 있으므로
    // "레지스터 넷만 흘려보내면 된다" 는 판단이 틀렸다.
    //
    // 게다가 이 훅은 넣은 뒤로 **쓸 만한 줄을 하나도 안 남겼다** - 거부될 때
    // msg 0x3F5 가 단 한 건도 안 왔다. 얻는 것 없이 위험만 있었다.
    // 다시 걸려면 인자를 흘려보내지 않는 naked 썽크가 필요하다.
    const bool notify_ok = false;

    g_installed.store(true, std::memory_order_release);
    log::infof(
        "소환 진단 v11 - **소환배달 0x{:X} {}** · 등록조회 0x{:X} {} ·"
        " 거부코드 0x{:X} {} ·"
        " 휠함수 0x{:X} {} · 휠소환 0x{:X} {} · 다른경로 0x{:X} {} ·"
        " 게이트 0x{:X} {} · 스폰 0x{:X} {}"
        " (말과 드래곤을 하나씩 부르면 '소환배달' 줄이 나란히 찍힌다)",
        kDispRva, disp_ok ? "후킹" : "실패", kFindRva,
        find_ok ? "후킹" : "실패", kErrFnRva,
        errfn_ok ? "후킹" : "실패", kWheelFnRva,
        wheelfn_ok ? "후킹" : "실패", kCallFnRva,
        callfn_ok ? "후킹" : "실패", kAltFnRva, altfn_ok ? "후킹" : "실패",
        kGateRva, gate_ok ? "후킹" : "실패", kSpawnRva,
        spawn_ok ? "후킹" : "실패");
    (void)notify_ok;
    return gate_ok || spawn_ok || callfn_ok || wheelfn_ok || altfn_ok ||
           errfn_ok || find_ok || disp_ok;
}

}  // namespace cdtb::game
