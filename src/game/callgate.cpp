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
//   실내 창 0x9635F8 +4 : FC 02 | 84 C0 | **74** 0E | 8B 05
//                          앞 명령 꼬리 · test al,al · je · 다음 명령 머리
//   지붕 창 0x963658 +1 : C0 | **74** 0A | 8B 05 63 95 25
//   지역 창 0x9626B8 +1 : C0 | **74** 75 | 8B 05 07 A5 25
constexpr GateDef kGates[kCallGateCount] = {
    {"실내에서도 호출",
     "\"실내에서는 호출할 수 없습니다\" 를 넘긴다",
     0x009635FC,
     {0xFC, 0x02, 0x84, 0xC0, 0x74, 0x0E, 0x8B, 0x05},
     {0xEB},   // je -> jmp (오류 대입을 건너뛴다)
     1},
    {"지붕 위에서도 호출",
     "\"지붕 위에서는 호출할 수 없습니다\" 를 넘긴다",
     0x00963659,
     {0xC0, 0x74, 0x0A, 0x8B, 0x05, 0x63, 0x95, 0x25},
     {0xEB},
     1},
    {"금지 구역에서도 호출",
     "\"호출할 수 없는 지역입니다\" 를 넘긴다(어비스 계열)",
     0x009626B9,
     {0xC0, 0x74, 0x75, 0x8B, 0x05, 0x07, 0xA5, 0x25},
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
    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    std::lock_guard<std::mutex> lk(g_mtx);
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
