#include "game/skillgate.h"

#include <windows.h>

#include <cstring>
#include <format>
#include <mutex>
#include <string>

#include "core/log.h"
#include "core/write_log.h"
#include "mem/reader.h"

namespace cdtb::game {
namespace {

// `want` 는 **창 8바이트 전체**다(머리 주석 참조). 관문1 은 창이 한 칸 아래
// (0x0208B638)에서 시작하므로 `6A` 로 시작한다 - 앞 명령
// `66 89 7C 24 6A`(`mov [rsp+0x6a], di`)의 변위 바이트이고, 끼워 넣기가 그 밖을
// 안 건드리므로 보존된다.
// ---- 2944 재도출 (2026-09-18)
//
// 게임이 2850 -> 2944 로 갱신되며 둘 다 밀렸다. **폭이 다르다**
// (관문0 +0x75D010 · 관문1 +0xB56C0) — 단일 델타로 밀면 안 된다.
//
// 찾은 길은 AOB 가 아니라 **오류 이름에서 되짚기**다. 이름 문자열은 갱신을
// 안 타기 때문이다:
//
//   "eErrNoCannotLearnKnowledgeByFromType"(RVA 0x5929B98)
//     -> 등록 0x2207944 -> 값 슬롯 모듈+0x6CF7BF0
//     -> 그 슬롯을 읽는 곳 0x2140AEC  (= CheckLearnOrLevelUp, 0x2140730~0x2141885)
//     -> 그 직전 `call 썽크 0x2140640` -> jmp 목적지 **0xE808870** = 관문0
//     -> 같은 함수 안 `setge byte [rsp+0x40]` **0x2140CF9** = 관문1
//
// **창 8바이트는 둘 다 2850 과 글자 하나 안 틀리고 같다.** 코드가 안 바뀌고
// 자리만 밀렸다는 뜻이다. 스크립트: 스크래치패드 `skillgate2944.py`.
//
// (2850 자리: 관문0 0x0E0AB860 · 관문1 0x0208B639)
constexpr GateDef kGates[kGateCount] = {
    {"습득 경로 무시",
     "\"특정 조건을 통해 배울 수 있습니다\" 를 넘긴다",
     0x0E808870,
     // mov [rsp+0x18], rbx | mov [rsp+0x10], dx  (프롤로그 머리, 창 8바이트)
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x66, 0x89, 0x54},
     {0xB0, 0x01, 0xC3},              // mov al,1 ; ret
     3},
    {"결속 비용 무시",
     "어비스 결속이 모자라도 판정을 통과시킨다(차감 경로는 미확인)",
     0x02140CF9,
     // (앞 명령의 변위 0x6A) setge byte [rsp+0x40] | (다음 명령 머리 41 8B)
     {0x6A, 0x0F, 0x9D, 0x44, 0x24, 0x40, 0x41, 0x8B},
     {0xC6, 0x44, 0x24, 0x40, 0x01},  // mov byte [rsp+0x40], 1
     5},
};

std::mutex g_mtx;
GateSlot g_slot[kGateCount];

// ------------------------------------------------------------- 실제 메모리

struct RealIo {
    const mem::Reader* reader = nullptr;
    const char* what = "";
};

bool real_read8(void* ctx, std::uintptr_t base, std::uint8_t out[8]) {
    // **생 포인터로 안 읽는다.** 갱신으로 이미지가 줄면 이 주소가 이미지 밖일 수
    // 있고, 여기는 렌더 스레드라 접근 위반이면 게임이 그 자리에서 죽는다.
    // Reader 의 읽기는 SEH 로 감싸여 있다(mem/safe_read.cpp).
    const auto* c = static_cast<const RealIo*>(ctx);
    return c != nullptr && c->reader != nullptr && c->reader->read(base, out, 8);
}

// 정렬된 8바이트를 한 번에 갈아 끼운다. 창은 부르는 쪽이 이미 확인했다.
// **게임 메모리를 바꾸는 유일한 자리**라 로그 한 줄도 여기서 남긴다 - 크래시 뒤
// "무엇을 썼는가" 를 되짚는 길은 그것뿐이다.
bool real_write8(void* ctx, std::uintptr_t base, std::uint64_t value) {
    const auto* c = static_cast<const RealIo*>(ctx);
    const std::string after = std::format("{:016X}", value);
    log_write(c != nullptr ? c->what : "스킬 관문", base, "코드 8바이트", after);

    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(base), 8,
                        PAGE_EXECUTE_READWRITE, &old)) {
        log::warnf("스킬 관문: VirtualProtect 실패(0x{:X})",
                   static_cast<unsigned long>(GetLastError()));
        return false;
    }
    _InterlockedExchange64(reinterpret_cast<volatile long long*>(base),
                           static_cast<long long>(value));
    DWORD back = 0;
    // **복원 실패를 버리지 않는다.** 실패하면 그 코드 페이지가 세션 내내 쓰기
    // 가능으로 남아, 무결성 검사에 걸릴 여지가 커진다. 쓰기 자체는 이미 끝났으므로
    // 참을 돌려주되 한 줄 남긴다.
    if (!VirtualProtect(reinterpret_cast<void*>(base), 8, old, &back)) {
        log::warnf("스킬 관문: 0x{:X} 의 보호를 못 되돌렸다(0x{:X}) - 그 페이지가"
                   " 쓰기 가능으로 남는다",
                   base, static_cast<unsigned long>(GetLastError()));
    }
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base), 8);
    return true;
}

GateIo bind(const RealIo& c) {
    GateIo io;
    io.read8 = &real_read8;
    io.write8 = &real_write8;
    io.ctx = const_cast<RealIo*>(&c);
    return io;
}

}  // namespace

// 다른 관문 모듈(callgate 등)이 같은 쓰기 기법을 쓰도록 내놓는다. 문맥은
// **스레드마다 하나**라 한 스레드가 두 관문을 연달아 걸어도 섞이지 않는다.
GateIo gate_local_io(const mem::Reader& reader, const char* what) {
    static thread_local RealIo c;
    c.reader = &reader;
    c.what = (what != nullptr) ? what : "관문";
    return bind(c);
}

namespace {

// 실패 사유를 화면에도 그대로 쓸 수 있게 한 곳에서 만든다. g_mtx 안에서 부른다.
bool finish(int gate, GateStep st, bool on, const char** why) {
    GateSlot& s = g_slot[gate];
    const GateDef& d = kGates[gate];
    if (st == kStepOk || st == kStepAlready) {
        if (why != nullptr) *why = "";
        return true;
    }
    // **`unsupported` 는 켜는 길에서만 세운다.** 끄는 도중 실패에 세우면 패치가
    // 걸린 채 화면이 체크박스를 안 그려, 끌 방법이 사라진다.
    if (on && (st == kStepOutOfImage || st == kStepBadWindow ||
               st == kStepForeignOriginal)) {
        s.unsupported = true;
    }
    if (why != nullptr) *why = gate_step_text(st);
    log::warnf("스킬 관문 '{}' {}: {} (0x{:X})", d.name, on ? "켜기" : "끄기",
               gate_step_text(st), s.site);
    return false;
}

// 창을 잡고 지금 8바이트를 읽는다. 켜기·끄기·확인이 전부 이 앞머리를 공유한다.
GateStep site_read(const GateDef& d, GateSlot* s, std::uintptr_t module_base,
                   std::size_t module_size, const GateIo& io,
                   std::uintptr_t* base_out, std::size_t* off_out,
                   std::uint64_t* cur_out, std::uint8_t cur_bytes[8]) {
    if (module_base == 0 || module_size == 0) return kStepOutOfImage;
    // 이미지 밖을 읽으려 하지 않는다. Reader 의 읽기가 SEH 로 막아 주긴 하지만,
    // 막아 주는 것과 애초에 시도하지 않는 것은 다르다.
    if (d.rva + d.len > module_size) return kStepOutOfImage;
    const std::uintptr_t site = module_base + d.rva;
    std::uintptr_t base = 0;
    std::size_t off = 0;
    // 이 갈래는 `rva` 상수를 잘못 고쳤을 때의 그물이다. Windows 는 이미지를 항상
    // 64KB 경계에 올리므로 module_base 의 정렬이 창 계산을 깨뜨릴 수는 없다.
    if (!patch_window(site, d.len, &base, &off)) return kStepBadWindow;
    s->site = site;
    std::uint8_t cur[8];
    if (!io.read8(io.ctx, base, cur)) return kStepReadFailed;
    std::memcpy(cur_bytes, cur, 8);
    std::memcpy(cur_out, cur, 8);
    *base_out = base;
    *off_out = off;
    return kStepOk;
}

}  // namespace

// ------------------------------------------------------------- 순수 부분

bool patch_window(std::uintptr_t addr, std::size_t len, std::uintptr_t* base,
                  std::size_t* off) {
    if (base == nullptr || off == nullptr) return false;
    if (len == 0 || len > 8) return false;
    const std::uintptr_t b = addr & ~static_cast<std::uintptr_t>(7);
    const std::size_t o = static_cast<std::size_t>(addr - b);
    if (o + len > 8) return false;   // 창 두 개에 걸친다 - 원자로 못 쓴다
    *base = b;
    *off = o;
    return true;
}

std::uint64_t patch_splice(std::uint64_t orig, std::size_t off,
                           const std::uint8_t* with, std::size_t len) {
    if (with == nullptr || len == 0 || off + len > 8) return orig;
    std::uint8_t b[8];
    std::memcpy(b, &orig, 8);
    std::memcpy(b + off, with, len);
    std::uint64_t out = 0;
    std::memcpy(&out, b, 8);
    return out;
}

const GateDef* gate_def(int gate) {
    if (gate < 0 || gate >= kGateCount) return nullptr;
    return &kGates[gate];
}

const char* gate_step_text(GateStep step) {
    switch (step) {
        case kStepOk: return "됐다";
        case kStepAlready: return "이미 그 상태다";
        case kStepOutOfImage: return "그 주소가 이미지 밖이다";
        case kStepBadWindow: return "8바이트 창에 안 들어간다";
        case kStepReadFailed: return "그 자리를 못 읽는다";
        case kStepForeignOriginal: return "원본 바이트가 우리가 아는 것과 다르다";
        case kStepForeignNow: return "지금 값이 우리가 쓴 것과 다르다";
        case kStepWriteFailed: return "쓰기에 실패했다";
    }
    return "알 수 없다";
}

GateStep gate_probe(const GateDef& d, GateSlot* s, std::uintptr_t module_base,
                    std::size_t module_size, const GateIo& io) {
    if (s == nullptr || io.read8 == nullptr) return kStepBadWindow;
    std::uintptr_t base = 0;
    std::size_t off = 0;
    std::uint64_t cur64 = 0;
    std::uint8_t cur[8];
    const GateStep st =
        site_read(d, s, module_base, module_size, io, &base, &off, &cur64, cur);
    // 못 읽은 것은 **판정이 아니다** - 다음에 다시 본다. 나머지는 결론이 났으므로
    // 확인 완료로 친다.
    if (st == kStepReadFailed) return st;
    s->probed = true;
    if (st != kStepOk) return st;
    if (std::memcmp(cur, d.want, 8) != 0) return kStepForeignOriginal;
    return kStepOk;
}

GateStep gate_apply(const GateDef& d, GateSlot* s, std::uintptr_t module_base,
                    std::size_t module_size, bool on, const GateIo& io) {
    if (s == nullptr || io.read8 == nullptr || io.write8 == nullptr) {
        return kStepBadWindow;
    }
    if (s->on == on) return kStepAlready;

    std::uintptr_t base = 0;
    std::size_t off = 0;
    std::uint64_t cur64 = 0;
    std::uint8_t cur[8];
    const GateStep st =
        site_read(d, s, module_base, module_size, io, &base, &off, &cur64, cur);
    if (st == kStepReadFailed) return st;
    s->probed = true;
    if (st != kStepOk) return st;

    if (on) {
        // **창 8바이트 전부**로 확인한다. 쓰기 폭(3바이트)으로만 보면 갱신 뒤
        // 남의 함수 프롤로그를 `mov al,1; ret` 로 덮는다.
        if (std::memcmp(cur, d.want, 8) != 0) return kStepForeignOriginal;
        const std::uint64_t next = patch_splice(cur64, off, d.with, d.len);
        if (!io.write8(io.ctx, base, next)) return kStepWriteFailed;
        s->orig = cur64;
        s->on = true;
        return kStepOk;
    }

    // 끄기: 켤 때 떠 둔 창 8바이트를 그대로 되돌린다. 그 사이 누가 같은 창을
    // 바꿨다면 되돌리는 것이 **오히려 해가 되므로**(남의 트램펄린 반쪽을 지운다),
    // 지금 값이 우리가 쓴 것인지 본다.
    const std::uint64_t mine = patch_splice(s->orig, off, d.with, d.len);
    if (cur64 != mine) return kStepForeignNow;
    if (!io.write8(io.ctx, base, s->orig)) return kStepWriteFailed;
    s->on = false;
    return kStepOk;
}

// ------------------------------------------------------------- 바깥 API

SkillGateInfo skillgate_info(int gate) {
    SkillGateInfo i;
    if (gate < 0 || gate >= kGateCount) return i;
    i.name = kGates[gate].name;
    i.what = kGates[gate].what;
    std::lock_guard<std::mutex> lk(g_mtx);
    i.on = g_slot[gate].on;
    i.unsupported = g_slot[gate].unsupported;
    i.probed = g_slot[gate].probed;
    i.site = g_slot[gate].site;
    return i;
}

void skillgate_probe() {
    std::lock_guard<std::mutex> lk(g_mtx);
    // 패널이 열려 있는 동안 **매 프레임** 불린다. 볼 것이 없으면 모듈 조회도 안 한다.
    bool any = false;
    for (int g = 0; g < kGateCount; ++g) {
        if (!g_slot[g].probed && !g_slot[g].on) any = true;
    }
    if (!any) return;

    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    for (int g = 0; g < kGateCount; ++g) {
        GateSlot& s = g_slot[g];
        if (s.probed || s.on) continue;
        const RealIo c{&reader, kGates[g].name};
        const GateStep st = gate_probe(kGates[g], &s, mb, ms, bind(c));
        if (st == kStepOk) continue;
        // **못 읽은 것은 판정이 아니라 재시도다** - `gate_probe` 가 그때만
        // `probed` 를 안 세우므로 다음 프레임에 다시 온다. 그 갈래에서 warn 을
        // 내면 **매 프레임 같은 줄**이 찍혀 로그를 삼킨다(2026-09-18 실측:
        // dragondiag 의 같은 모양이 38,225줄 · 로그의 99.9% 를 먹었다).
        // 결론이 난 것만 남긴다.
        if (st == kStepReadFailed) continue;
        if (st == kStepOutOfImage || st == kStepBadWindow ||
            st == kStepForeignOriginal) {
            s.unsupported = true;
        }
        log::warnf("스킬 관문 '{}' 확인: {} (0x{:X})", kGates[g].name,
                   gate_step_text(st), s.site);
    }
}

bool skillgate_set(int gate, bool on, const char** why) {
    if (gate < 0 || gate >= kGateCount) {
        if (why != nullptr) *why = "그런 관문이 없다";
        return false;
    }
    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    const RealIo c{&reader, kGates[gate].name};

    std::lock_guard<std::mutex> lk(g_mtx);
    GateSlot& s = g_slot[gate];
    const GateDef& d = kGates[gate];
    // 못 쓰는 자리로 판정된 뒤에도 **끄는 길은 남긴다** - 켜 둔 채 갇히지 않게.
    if (s.unsupported && on) {
        if (why != nullptr) *why = "이 게임 빌드에서는 못 쓴다";
        return false;
    }
    const GateStep st = gate_apply(d, &s, mb, ms, on, bind(c));
    if (st == kStepOk) {
        log::infof("스킬 관문 '{}' {}: 0x{:X} ({}바이트)", d.name,
                   on ? "켬" : "끔", s.site, d.len);
    }
    return finish(gate, st, on, why);
}

void skillgate_remove_all() {
    const mem::LocalReader reader;
    const std::uintptr_t mb = reader.module_base();
    const std::size_t ms = reader.module_size();
    std::lock_guard<std::mutex> lk(g_mtx);
    for (int g = 0; g < kGateCount; ++g) {
        GateSlot& s = g_slot[g];
        if (!s.on) continue;
        // **끄기와 같은 길을 탄다** - 혈통 검사를 포함해서. 그 사이 남이 같은 창을
        // 바꿨으면 되돌리지 않고 한 줄만 남긴다. 남의 바이트를 부수는 것보다 낫다.
        const RealIo c{&reader, kGates[g].name};
        const GateStep st = gate_apply(kGates[g], &s, mb, ms, false, bind(c));
        if (st == kStepOk) {
            log::infof("스킬 관문 '{}' 되돌림(기능 해제)", kGates[g].name);
        } else {
            log::warnf("스킬 관문 '{}' 되돌리기 실패: {}", kGates[g].name,
                       gate_step_text(st));
        }
    }
}

}  // namespace cdtb::game
