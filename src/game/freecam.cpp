#include "game/freecam.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>

#include "core/log.h"
#include "game/camera.h"
#include "mem/hook.h"
#include "mem/module.h"
#include "mem/safe_read.h"
#include "mem/scanner.h"

namespace cdtb::game {
namespace {

// 카메라 좌표를 보간해 저장하는 코드.
//
//   vunpcklps xmm0, xmm1, xmm4      ; (X, Y) 묶기
//   vmovsd    [rdi+0x360], xmm0     ; X, Y 저장
//   mov       eax, [rsp+0x28]
//   mov       [rdi+0x368], eax      ; Z 저장
//
// 실행 섹션 전체에서 한 곳만 일치한다.
constexpr const char* kStorePattern =
    "C5 F0 14 C4 C5 FB 11 87 60 03 00 00 8B 44 24 28 89 87 68 03 00 00";

// 함수 시작 표식. MSVC 가 큰 스택 프레임 함수에 쓰는 프롤로그다.
//   mov rax, rsp
constexpr std::uint8_t kPrologue[3] = {0x48, 0x8B, 0xC4};

using UpdateFn = void(__fastcall*)(void*, float);

UpdateFn g_original = nullptr;
std::uintptr_t g_update_fn = 0;
std::atomic<bool> g_hooked{false};
std::atomic<bool> g_active{false};

// 후킹된 함수와 입력 스레드가 함께 만지는 값. 프레임마다 한 번씩
// 읽고 쓰므로 경합해도 한 프레임 어긋나는 것이 전부다.
float g_pos[3]{};
float g_speed = 20.0f;
std::uintptr_t g_component = 0;

// 훅이 실제로 받은 컴포넌트. 탐색이 찾은 것과 다를 수 있으므로,
// 켤 때는 추측이 아니라 이것을 쓴다.
std::atomic<std::uintptr_t> g_seen_comp{0};
std::atomic<unsigned> g_calls{0};
std::atomic<std::uintptr_t> g_script{0};

// 렌더가 실제로 읽는 카메라 변환은 컴포넌트가 아니라 그 뒤에 달린
// 스크립트 객체(PlayerCameraComponentScript)에 있다.
//
// 컴포넌트+0x360 을 우리 값으로 고정해도 화면이 전혀 변하지 않아
// 알게 된 사실이다. 그 칸은 게임이 부드럽게 따라가는 파생 상태값일
// 뿐이었다.
//
// 스크립트 객체 구조 (실측):
//   +0x328 (컴포넌트 기준) -> 스크립트 객체
//   블록이 0xA0 간격으로 두 벌 있고, 각 블록은 쿼터니언을 기준으로
//     Q-0x30, Q-0x20, Q-0x10   재기준 좌표 3벌 (뒤에 NaN 마커)
//     Q+0x00                   쿼터니언(회전)
//     Q+0x4C                   FOV,  Q+0x50 근접 평면
//     Q+0x54                   월드 좌표
//   첫 블록의 쿼터니언은 +0x38 이다.
//
// 재기준 좌표는 월드 좌표에서 지역 원점을 뺀 값이다(카메라 상대
// 렌더링). 원점은 지역마다 다르므로 상수로 박지 않고, 쓸 때마다
// 기존 값에서 차이를 구해 그대로 유지한다.
constexpr int kScriptObject = 0x328;
constexpr int kBlockStride = 0xA0;
constexpr int kBlockCount = 2;
constexpr int kFirstQuat = 0x38;
constexpr int kWorldPos = 0x54;             // 쿼터니언 기준
constexpr int kRebased[3] = {-0x30, -0x20, -0x10};

void write_camera_position(std::uintptr_t script, const float pos[3]) {
    for (int b = 0; b < kBlockCount; ++b) {
        const auto q = script + kFirstQuat + b * kBlockStride;

        float world[3]{};
        if (!mem::safe_read_bytes(q + kWorldPos, world, sizeof(world))) continue;

        // 재기준 좌표는 각자의 차이를 유지한 채 옮긴다. 먼저 차이를
        // 구하고 나서 월드를 덮어야 한다 - 순서가 바뀌면 차이가 0이 된다.
        for (const int off : kRebased) {
            float r[3]{};
            if (!mem::safe_read_bytes(q + off, r, sizeof(r))) continue;
            const float moved[3] = {pos[0] + (r[0] - world[0]),
                                    pos[1] + (r[1] - world[1]),
                                    pos[2] + (r[2] - world[2])};
            mem::safe_write_bytes(q + off, moved, sizeof(moved));
        }
        mem::safe_write_bytes(q + kWorldPos, pos, sizeof(float) * 3);
    }
}

// 원본이 보간해 둔 값을 우리 값으로 덮는다.
//
// 순서가 이 모드의 전부다. 원본보다 먼저 쓰면 그 값이 lerp 의
// '현재' 가 되어 목표 쪽으로 다시 끌려간다.
void __fastcall hooked_update(void* comp, float dt) {
    g_original(comp, dt);

    const auto addr = reinterpret_cast<std::uintptr_t>(comp);
    g_seen_comp.store(addr, std::memory_order_relaxed);
    const auto n = ++g_calls;

    // 훅이 실제로 불리는지, 어떤 컴포넌트로 불리는지 남긴다.
    // 이것을 안 남기면 "안 움직인다"의 원인이 입력인지 훅인지
    // 컴포넌트 불일치인지 구분할 수 없다.
    if (n <= 3 || n % 3000 == 0) {
        log::infof("프리캠 훅: {}번째 호출, comp={} 활성={} 대상={}", n, comp,
                   g_active.load() ? 1 : 0,
                   reinterpret_cast<void*>(g_component));
    }

    if (!g_active.load(std::memory_order_relaxed)) return;
    if (comp == nullptr) return;

    if (g_component != 0 && addr != g_component) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) {
            log::warnf("프리캠: 훅이 다른 컴포넌트로 불린다 (받은 {}, 대상 {})",
                       comp, reinterpret_cast<void*>(g_component));
        }
        return;
    }

    // 컴포넌트의 좌표도 맞춰 둔다. 렌더는 안 읽지만 게임의 다른
    // 계산이 이 값을 쓰므로, 놔두면 우리 카메라와 어긋난 상태가 된다.
    mem::safe_write_bytes(addr + component_offset::kWorldPosition, g_pos,
                          sizeof(g_pos));

    // 렌더가 실제로 읽는 값. 이쪽이 본체다.
    std::uint64_t script = 0;
    if (!mem::safe_read_bytes(addr + kScriptObject, &script, sizeof(script))) {
        return;
    }
    if (script < 0x10000) return;
    g_script.store(static_cast<std::uintptr_t>(script),
                   std::memory_order_relaxed);
    write_camera_position(static_cast<std::uintptr_t>(script), g_pos);
}

// 저장 명령에서 앞으로 훑어 함수 시작을 찾는다.
// 함수 사이는 int3(0xCC) 로 채워져 있다.
std::uintptr_t function_start_before(std::uintptr_t inside) {
    constexpr int kMaxBack = 8192;
    for (int i = 0; i < kMaxBack; ++i) {
        const auto a = inside - i;
        std::uint8_t b[3]{};
        if (!mem::safe_read_bytes(a - 3, b, sizeof(b))) return 0;
        if (b[0] == 0xCC && b[1] == 0xCC && b[2] == 0xCC) return a;
    }
    return 0;
}

std::uintptr_t find_update_function() {
    const auto mod = mem::find_module(nullptr);
    if (!mod) {
        log::errorf("프리캠: 모듈을 찾지 못했다");
        return 0;
    }
    const auto pattern = mem::parse_pattern(kStorePattern);
    if (!pattern) {
        log::errorf("프리캠: 패턴 파싱 실패");
        return 0;
    }

    std::vector<const std::uint8_t*> hits;
    for (const auto& r : mem::executable_ranges(*mod)) {
        for (const auto* p : mem::find_all(r, *pattern, 4)) hits.push_back(p);
        if (hits.size() > 1) break;
    }
    if (hits.size() != 1) {
        log::errorf("프리캠: 패턴이 {}곳에서 일치한다. 1곳이어야 한다",
                    hits.size());
        return 0;
    }

    const auto store = reinterpret_cast<std::uintptr_t>(hits[0]);
    const auto start = function_start_before(store);
    if (start == 0) {
        log::errorf("프리캠: 저장 0x{:X} 앞에서 함수 시작을 못 찾았다", store);
        return 0;
    }

    // 프롤로그가 맞는지 확인한다. 아니면 패딩을 잘못 짚은 것이다.
    std::uint8_t head[3]{};
    if (!mem::safe_read_bytes(start, head, sizeof(head)) ||
        std::memcmp(head, kPrologue, sizeof(head)) != 0) {
        log::errorf("프리캠: 0x{:X} 가 함수 시작으로 보이지 않는다 "
                    "({:02X} {:02X} {:02X})",
                    start, head[0], head[1], head[2]);
        return 0;
    }

    const auto base = reinterpret_cast<std::uintptr_t>(mod->base);
    log::infof("프리캠: 갱신 함수 0x{:X} (모듈+0x{:X}), 저장 0x{:X}", start,
               start - base, store);
    return start;
}

bool key_down(int vk) { return (::GetAsyncKeyState(vk) & 0x8000) != 0; }

}  // namespace

bool freecam_install() {
    if (g_hooked.load()) return true;

    g_update_fn = find_update_function();
    if (g_update_fn == 0) return false;

    if (!mem::hook_install(reinterpret_cast<void*>(g_update_fn),
                           reinterpret_cast<void*>(&hooked_update),
                           reinterpret_cast<void**>(&g_original))) {
        log::errorf("프리캠: 훅 설치 실패");
        return false;
    }
    g_hooked.store(true);
    log::infof("프리캠 준비됨 - F9 로 켜고 끈다");
    return true;
}

void freecam_uninstall() {
    if (!g_hooked.load()) return;
    g_active.store(false);
    mem::hook_remove(reinterpret_cast<void*>(g_update_fn));
    g_hooked.store(false);
    g_original = nullptr;
    log::infof("프리캠 훅 해제");
}

void freecam_toggle() {
    if (!g_hooked.load()) return;

    if (g_active.load()) {
        g_active.store(false);
        // 되돌리는 일은 게임이 한다. 우리가 안 쓰면 lerp 가 원래
        // 자리로 부드럽게 데려간다.
        log::infof("프리카메라 끔 - 게임이 원래 위치로 되돌린다");
        return;
    }

    // 훅이 실제로 받은 컴포넌트를 우선한다. 탐색이 찾은 것과 다르면
    // 탐색 쪽을 믿을 이유가 없다 - 갱신 함수가 매 프레임 넘겨받는
    // 포인터가 곧 게임이 쓰는 그 객체다.
    const auto seen = g_seen_comp.load();
    g_component = seen != 0 ? seen : cameras().player_component;
    if (g_component == 0) {
        log::warnf("프리캠: 대상 컴포넌트를 모른다 (훅 호출 {}회)",
                   g_calls.load());
        return;
    }
    if (seen != 0 && seen != cameras().player_component) {
        log::warnf("프리캠: 훅이 받은 컴포넌트({})가 탐색 결과({})와 다르다. "
                   "훅 쪽을 쓴다",
                   reinterpret_cast<void*>(seen),
                   reinterpret_cast<void*>(cameras().player_component));
    }
    if (!mem::safe_read_bytes(g_component + component_offset::kWorldPosition,
                              g_pos, sizeof(g_pos))) {
        log::errorf("프리캠: 현재 좌표를 읽지 못했다");
        return;
    }
    // 켤 때 좌표는 렌더가 쓰는 값에서 읽는다. 컴포넌트+0x360 은
    // 파생 상태값이라 실제 카메라와 어긋나 있을 수 있다.
    std::uint64_t script = 0;
    if (mem::safe_read_bytes(g_component + kScriptObject, &script,
                             sizeof(script)) && script > 0x10000) {
        float w[3]{};
        if (mem::safe_read_bytes(
                static_cast<std::uintptr_t>(script) + kFirstQuat + kWorldPos,
                w, sizeof(w))) {
            g_pos[0] = w[0];
            g_pos[1] = w[1];
            g_pos[2] = w[2];
        }
        g_script.store(static_cast<std::uintptr_t>(script));
    } else {
        log::warnf("프리캠: 스크립트 객체를 못 찾았다 (컴포넌트+0x328)");
    }
    g_active.store(true);
    log::infof("프리카메라 켬 - 시작 좌표 ({:.1f}, {:.1f}, {:.1f})", g_pos[0],
               g_pos[1], g_pos[2]);
}

FreeCamState freecam_state() {
    FreeCamState s;
    s.hooked = g_hooked.load();
    s.active = g_active.load();
    s.speed = g_speed;
    s.update_fn = g_update_fn;
    s.script = g_script.load();
    std::memcpy(s.pos, g_pos, sizeof(s.pos));
    return s;
}

void freecam_tick() {
    // 카메라를 찾은 뒤에 한 번만 건다. 여기서 거는 이유는 이 함수가
    // 프레임마다 불리는 유일한 지점이고, 훅 설치가 카메라 탐색보다
    // 뒤여야 하기 때문이다. 실패하면 잠시 쉬었다 다시 본다.
    if (!g_hooked.load()) {
        static int cooldown = 0;
        if (cooldown > 0) {
            --cooldown;
            return;
        }
        if (cameras().player_component == 0) return;
        if (!freecam_install()) {
            cooldown = 120;   // 약 2초 뒤 재시도
            return;
        }
    }

    // 델타타임. 이동 속도가 프레임률에 휘둘리지 않게 한다.
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER last{};
    if (freq.QuadPart == 0) {
        ::QueryPerformanceFrequency(&freq);
        ::QueryPerformanceCounter(&last);
    }
    LARGE_INTEGER now{};
    ::QueryPerformanceCounter(&now);
    float dt = static_cast<float>(now.QuadPart - last.QuadPart) /
               static_cast<float>(freq.QuadPart);
    last = now;
    dt = std::clamp(dt, 0.0f, 0.1f);

    // F9 토글. 눌린 순간에만 반응한다.
    static bool prev = false;
    const bool cur = key_down(VK_F9);
    if (cur && !prev) freecam_toggle();
    prev = cur;

    if (!g_active.load()) return;

    // 속도. 넘패드 +/- 로 조절한다.
    if (key_down(VK_ADD)) g_speed *= 1.0f + 2.0f * dt;
    if (key_down(VK_SUBTRACT)) g_speed /= 1.0f + 2.0f * dt;
    g_speed = std::clamp(g_speed, 0.5f, 2000.0f);

    // 이동. 넘패드를 쓰는 이유는 WASD 가 캐릭터를 움직이기 때문이다.
    //
    // NumLock 이 꺼져 있으면 넘패드는 VK_NUMPAD8 이 아니라 VK_UP 을
    // 보낸다. 어느 쪽이든 받도록 두 조합을 다 본다. 방향키 묶음도
    // 같은 가상 키라 그대로 쓸 수 있다.
    //
    // 회전을 아직 못 찾아 카메라 기준이 아니라 월드 축 기준이다.
    float step = g_speed * dt;
    if (key_down(VK_SHIFT)) step *= 5.0f;
    if (key_down(VK_CONTROL)) step *= 0.2f;

    const bool fwd  = key_down(VK_NUMPAD8) || key_down(VK_UP);
    const bool back = key_down(VK_NUMPAD2) || key_down(VK_DOWN);
    const bool rgt  = key_down(VK_NUMPAD6) || key_down(VK_RIGHT);
    const bool lft  = key_down(VK_NUMPAD4) || key_down(VK_LEFT);
    const bool up   = key_down(VK_NUMPAD9) || key_down(VK_PRIOR);   // PgUp
    const bool dn   = key_down(VK_NUMPAD3) || key_down(VK_NEXT);    // PgDn

    float d[3]{};
    if (fwd)  d[2] += step;   // +Z
    if (back) d[2] -= step;
    if (rgt)  d[0] += step;   // +X
    if (lft)  d[0] -= step;
    if (up)   d[1] += step;   // +Y (위)
    if (dn)   d[1] -= step;

    if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) return;

    // 입력이 도달했다는 것을 로그로 못박는다. 이게 없으면 "안
    // 움직인다"의 원인이 입력인지 훅인지 구분할 수 없다.
    static int throttle = 0;
    if (throttle++ % 120 == 0) {
        log::infof("프리캠 입력: dxyz=({:.2f}, {:.2f}, {:.2f}) 좌표 "
                   "({:.1f}, {:.1f}, {:.1f}) 속도 {:.1f}",
                   d[0], d[1], d[2], g_pos[0], g_pos[1], g_pos[2], g_speed);
    }

    g_pos[0] += d[0];
    g_pos[1] += d[1];
    g_pos[2] += d[2];
}

}  // namespace cdtb::game
