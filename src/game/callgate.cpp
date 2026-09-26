// @build 1.0.0.2976  관문 넷 재도출(2026-09-26) - **자리는 넷 다 +0x60** 인데
//   지붕·지역은 창에 전역 변위가 들어 있어(코드 +0x60, 슬롯 -0x1C) 창 바이트가
//   DB->5F · 7F->03 으로 바뀌었다. 옛 창으로는 안 찾아진다 - 슬롯에서 다시
//   짚었다(슬롯을 8B 05 로 읽는 자리가 관문마다 딱 1곳). 근거: specs/2026-09-26-game-update-2976.md §9
// @build 1.0.0.2949  관문 넷 자리가 그대로다(2026-09-22) - 2949 첫 실행에서 넷 다 창
//   8바이트(명령 안의 전역 변위 바이트 포함)가 맞아 켜졌다(로그 13:05:06~07). 같은 구역의
//   호출 검증기 0x9DD420 도 경계·오류 슬롯 오프셋이 그대로다(callcheck.h).
//   (2944: 관문 넷 전부 재도출 - 오류 문구 -> 값 슬롯 -> 내는 곳)
//   근거: specs/2026-09-18-game-update-2944.md §8

#include "game/callgate.h"

#include <mutex>

#include "core/log.h"
#include "game/skillgate.h"
#include "mem/reader.h"

namespace cdtb::game {
namespace {

// `want` 는 **창 8바이트 전체**다(확인 폭 ≠ 쓰기 폭). 쓰기는 `je` 한 바이트면
// 충분하지만, `74`(je) 한 바이트는 실행 섹션 어디에나 있어 확인이 못 된다.
//
// 창은 `rva & ~7` 에서 시작한다. 그래서 `want` 의 앞쪽 바이트는 우리가 안 건드릴
// 앞 명령의 꼬리다 — 그것까지 맞아야 그 자리가 맞는 자리다.
//
//   실내 창 0x9DE7D8 +4 : 03 03 | 84 C0 | **74** 0E | 8B 05
//                          앞 명령 꼬리 · test al,al · je · 다음 명령 머리
//   지붕 창 0x9DE838 +1 : C0 | **74** 0A | 8B 05 DB 92 31
//   지역 창 0x9DD898 +1 : C0 | **74** 75 | 8B 05 7F A2 31
//   위치 창 0x9DD6C0 +4 : 06 00 | 84 C0 | **75** 0A | 8B 05
//
// 위치 관문만 `jne`(0x75)다 — 거기서는 검사 통과 쪽이 아래로 붙어 있다. 실내
// 창과 모양이 닮았지만 앞 두 바이트(`06 00` vs `03 03`)가 달라 서로 안 헷갈린다.
//
// ---- 2944 재도출 (2026-09-18)
//
// 게임이 2850 -> 2944 로 갱신되며 **넷 다 밀렸다**(전부 +0x7B1E0, 값 슬롯은
// +0x13AF58). 옛 창은 새 exe 어디에도 없어 `want` 대조가 설치를 거부했다 —
// 안전망이 설계대로 작동했다.
//
// **AOB 로 다시 찾지 않았다.** 한국어 문구에서 시작하는 경로가 갱신을 안 탄다:
//
//   문구 -> 등록 자리(lea r8) -> 그 앞 lea rcx(값 전역 슬롯)
//        -> 값 슬롯을 읽는 코드(= 오류를 내는 곳) -> 앞의 조건 점프 -> 8바이트 창
//
// (2850 자리는 실내 0x9635FC · 지붕 0x963659 · 지역 0x9626B9 · 위치 0x9624E4,
//  값 슬롯 +0x6BBCBC0/BC4/BC8/BA4. 갱신 대조용으로 남긴다.)
//
// ⚠️ 2944 에는 **"호출할 수 없는 위치입니다" 문구를 쓰는 등록이 둘**이다
// (슬롯 +0x6CF6F88 · +0x6CF7AFC). 우리 것은 뒤쪽이고, 창이 2850 과 바이트까지
// 같아 구분된다. 앞쪽은 `0F 85`(near jne)를 쓰는 다른 코드다.
constexpr GateDef kGates[kCallGateCount] = {
    {"실내에서도 호출",
     "\"실내에서는 호출할 수 없습니다\" 를 넘긴다",
     0x009DE83C,
     {0x03, 0x03, 0x84, 0xC0, 0x74, 0x0E, 0x8B, 0x05},
     {0xEB},   // je -> jmp (오류 대입을 건너뛴다)
     1},
    {"지붕 위에서도 호출",
     "\"지붕 위에서는 호출할 수 없습니다\" 를 넘긴다",
     0x009DE899,
     {0xC0, 0x74, 0x0A, 0x8B, 0x05, 0x5F, 0x92, 0x31},
     {0xEB},
     1},
    {"금지 구역에서도 호출",
     "\"호출할 수 없는 지역입니다\" 를 넘긴다(어비스 계열)",
     0x009DD8F9,
     {0xC0, 0x74, 0x75, 0x8B, 0x05, 0x03, 0xA2, 0x31},
     {0xEB},
     1},
    {"막힌 위치에서도 호출",
     "\"호출할 수 없는 위치입니다\" 를 넘긴다 — 성벽·지붕 위에서 실제로 뜨는 것",
     0x009DD724,
     {0x06, 0x00, 0x84, 0xC0, 0x75, 0x0A, 0x8B, 0x05},
     {0xEB},
     1},
};

std::mutex g_mtx;
GateSlot g_slot[kCallGateCount];

bool valid(int gate) { return gate >= 0 && gate < kCallGateCount; }

// 실패 사유를 화면에도 그대로 쓸 수 있게 한 곳에서 만든다. g_mtx 안에서 부른다.
bool finish(int gate, GateStep st, bool on, const char** why) {
    GateSlot& s = g_slot[gate];
    const GateDef& d = kGates[gate];
    if (st == kStepOk || st == kStepAlready) {
        if (why != nullptr) *why = "";
        return true;
    }
    // **`unsupported` 는 켜는 길에서만 세운다** - 끄는 도중 실패에 세우면 패치가
    // 걸린 채 화면이 체크박스를 안 그려, 끌 방법이 사라진다(skillgate 와 같다).
    if (on && (st == kStepOutOfImage || st == kStepBadWindow ||
               st == kStepForeignOriginal)) {
        s.unsupported = true;
    }
    if (why != nullptr) *why = gate_step_text(st);
    log::warnf("호출 위치 관문 '{}' {}: {} (0x{:X})", d.name,
               on ? "켜기" : "끄기", gate_step_text(st), s.site);
    return false;
}

}  // namespace

CallGateInfo callgate_info(int gate) {
    CallGateInfo out;
    if (!valid(gate)) return out;
    std::lock_guard<std::mutex> lk(g_mtx);
    const GateSlot& s = g_slot[gate];
    out.name = kGates[gate].name;
    out.what = kGates[gate].what;
    out.on = s.on;
    out.unsupported = s.unsupported;
    out.probed = s.probed;
    out.site = s.site;
    return out;
}

bool callgate_set(int gate, bool on, const char** why) {
    if (!valid(gate)) {
        if (why != nullptr) *why = "그런 관문이 없다";
        return false;
    }
    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    const GateIo io = gate_local_io(reader, kGates[gate].name);

    std::lock_guard<std::mutex> lk(g_mtx);
    GateSlot& s = g_slot[gate];
    // 못 쓰는 자리로 판정된 뒤에도 **끄는 길은 남긴다** - 켜 둔 채 갇히지 않게.
    if (s.unsupported && on) {
        if (why != nullptr) *why = "이 게임 빌드에서는 못 쓴다";
        return false;
    }
    const GateStep st = gate_apply(kGates[gate], &s, mb, ms, on, io);
    if (st == kStepOk) {
        log::infof("호출 위치 관문 '{}' {}: 0x{:X}", kGates[gate].name,
                   on ? "켬" : "끔", s.site);
    }
    return finish(gate, st, on, why);
}

void callgate_probe() {
    std::lock_guard<std::mutex> lk(g_mtx);
    // 패널이 열려 있는 동안 **매 프레임** 불린다. 볼 것이 없으면 모듈 조회도
    // 안 한다(skillgate_probe 와 같은 모양).
    bool any = false;
    for (int g = 0; g < kCallGateCount; ++g) {
        if (!g_slot[g].probed) any = true;
    }
    if (!any) return;

    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    for (int g = 0; g < kCallGateCount; ++g) {
        if (g_slot[g].probed) continue;   // 이미 결론이 난 것은 다시 안 본다
        const GateIo io = gate_local_io(reader, kGates[g].name);
        const GateStep st = gate_probe(kGates[g], &g_slot[g], mb, ms, io);
        if (st != kStepOk && st != kStepReadFailed) {
            g_slot[g].unsupported = true;
            log::warnf("호출 위치 관문 '{}' 을 이 빌드에서 못 쓴다: {}",
                       kGates[g].name, gate_step_text(st));
        }
    }
}

void callgate_remove_all() {
    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    std::lock_guard<std::mutex> lk(g_mtx);
    for (int g = 0; g < kCallGateCount; ++g) {
        if (!g_slot[g].on) continue;
        // **끄기와 같은 길을 탄다** - 혈통 검사를 포함해서. 그 사이 남이 같은
        // 창을 바꿨으면 되돌리지 않고 한 줄만 남긴다.
        const GateIo io = gate_local_io(reader, kGates[g].name);
        const GateStep st = gate_apply(kGates[g], &g_slot[g], mb, ms, false, io);
        if (st == kStepOk) {
            log::infof("호출 위치 관문 '{}' 되돌림(기능 해제)", kGates[g].name);
        } else {
            log::warnf("호출 위치 관문 '{}' 되돌리기 실패: {}", kGates[g].name,
                       gate_step_text(st));
        }
    }
}

// --------------------------------------------------------- 순수 부분(시험용)

const GateDef* callgate_def(int gate) {
    return valid(gate) ? &kGates[gate] : nullptr;
}

}  // namespace cdtb::game
