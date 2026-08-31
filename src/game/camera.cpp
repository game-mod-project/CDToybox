#include "game/camera.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>

#include "core/log.h"
#include "mem/reader.h"
#include "mem/rtti.h"
#include "mem/safe_read.h"
#include "mem/watchpoint.h"

namespace cdtb::game {
namespace {

CameraSet g_set;
bool g_done = false;

// 후보 중 이름이 맞는 것을 고른다.
//
// 가장 작은 주소를 고르는 방식은 틀렸다. vtable 값을 우연히 담고
// 있는 메모리가 가짜 후보로 잡히고, 그게 진짜보다 낮은 주소면
// 그것을 집는다. 실측에서 실제로 그렇게 어긋났다.
//
// 카메라는 자기 이름을 들고 있으므로 그것으로 검증한다.
std::uintptr_t pick_named(const mem::Reader& reader,
                          const std::vector<std::uintptr_t>& candidates,
                          const char* expected) {
    std::string n;
    for (const auto a : candidates) {
        if (!camera_name(reader, a, &n)) continue;
        if (n == expected) return a;
    }
    return 0;
}

}  // namespace

bool camera_name(const mem::Reader& reader, std::uintptr_t camera,
                 std::string* out) {
    if (camera == 0 || out == nullptr) return false;

    std::uint64_t name_obj = 0;
    if (!reader.read(camera + camera_offset::kName, &name_obj,
                     sizeof(name_obj))) {
        return false;
    }
    if (name_obj < 0x10000) return false;

    std::uint32_t len = 0;
    if (!reader.read(static_cast<std::uintptr_t>(name_obj) + 8, &len,
                     sizeof(len))) {
        return false;
    }
    if (len == 0 || len > 64) return false;

    char buf[65]{};
    if (!reader.read(static_cast<std::uintptr_t>(name_obj) + 0x18, buf, len)) {
        return false;
    }
    buf[len] = 0;
    out->assign(buf, len);
    return true;
}

// 이미지를 이미 읽어 둔 Rtti 로 탐색한다. 재시도 루프가 350MB를
// 매번 다시 읽지 않도록 분리했고, Reader 를 받으므로 probe 도
// 같은 로직을 돌려 배포 전에 검증할 수 있다.
bool discover_with(const mem::Rtti& rtti, const mem::Reader& reader,
                   CameraSet* out) {
    CameraSet found;

    // 이름으로 진짜를 고른다. 후보에는 vtable 값을 우연히 담은
    // 메모리가 섞여 있다.
    found.free_cam = pick_named(
        reader, rtti.instances_of_class(".?AVFreeCamCamera@pa@@", 16),
        "FreeCamera");
    found.photo_cam = pick_named(
        reader, rtti.instances_of_class(".?AVPhotoCamera@pa@@", 16),
        "PhotoCamera");

    // CameraManager 는 이름이 없다. +0x18 이 방금 찾은 프리카메라를
    // 가리키는지로 검증한다.
    for (const auto a :
         rtti.instances_of_class(".?AVCameraManager@pa@@", 16)) {
        std::uint64_t slot = 0;
        if (!reader.read(a + 0x18, &slot, sizeof(slot))) continue;
        if (found.free_cam != 0 && slot == found.free_cam) {
            found.manager = a;
            break;
        }
    }

    // PlayerCameraComponent 도 이름이 없다. +0x88 이 가리키는 곳에서
    // 0x28 을 빼면 활성 카메라이고, 그 이름이 "PlayerCamera" 여야 한다.
    const auto comps =
        rtti.instances_of_class(".?AVPlayerCameraComponent@pa@@", 16);
    log::infof("PlayerCameraComponent 후보 {}개", comps.size());
    for (const auto a : comps) {
        std::uint64_t icam = 0;
        if (!reader.read(a + 0x88, &icam, sizeof(icam))) {
            log::infof("  {} +0x88 읽기 실패", reinterpret_cast<void*>(a));
            continue;
        }
        if (icam < 0x10000 + 0x28) {
            log::infof("  {} +0x88={} 범위 밖", reinterpret_cast<void*>(a),
                       reinterpret_cast<void*>(icam));
            continue;
        }

        const auto cam = static_cast<std::uintptr_t>(icam) - 0x28;
        std::string n;
        const bool got = camera_name(reader, cam, &n);
        log::infof("  {} -> cam {} 이름 {}", reinterpret_cast<void*>(a),
                   reinterpret_cast<void*>(cam), got ? n : std::string("(실패)"));
        if (!got || n != "PlayerCamera") continue;

        found.player_component = a;
        found.active = cam;
        break;
    }

    log::infof("카메라 탐색: manager={} freeCam={} photo={} playerComp={} "
               "active={}",
               reinterpret_cast<void*>(found.manager),
               reinterpret_cast<void*>(found.free_cam),
               reinterpret_cast<void*>(found.photo_cam),
               reinterpret_cast<void*>(found.player_component),
               reinterpret_cast<void*>(found.active));

    g_set = found;
    g_done = found.complete();
    if (out != nullptr) *out = found;

    log::infof("카메라 탐색 {}", g_done ? "완료" : "불완전");
    return g_done;
}

namespace {

std::thread g_auto;
std::atomic<bool> g_stop{false};

// 게임을 켜면 알아서 돈다. 월드 진입 전에는 카메라가 기본값이라
// 찾아도 쓸모가 없으므로, 찾을 때까지 주기적으로 재시도한다.
void auto_analysis_loop() {
    mem::LocalReader reader;
    mem::Rtti rtti(reader);

    // 모듈 이미지는 한 번만 읽는다. 재시도마다 350MB를 다시 읽을
    // 이유가 없다.
    if (!rtti.load_image()) {
        log::errorf("자동 분석: 모듈 이미지를 읽지 못했다");
        return;
    }
    log::infof("자동 분석 시작 - 월드 진입을 기다린다");

    for (int attempt = 1; !g_stop.load(); ++attempt) {
        if (discover_with(rtti, reader, nullptr) && g_set.active != 0) {
            log::infof("자동 분석: {}번째 시도에 카메라 확보", attempt);
            break;
        }
        for (int i = 0; i < 100 && !g_stop.load(); ++i) {
            ::Sleep(100);   // 10초, 중단 요청에 100ms 안에 반응
        }
    }
    if (g_stop.load()) return;

    // 누가 값을 쓰는지 추적한다.
    //
    // 히트가 0이면 세 가지가 구분되지 않는다 - 그 값을 안 쓰는 것인지,
    // 하드웨어 브레이크포인트가 동작하지 않는 것인지, 쓰는 스레드가
    // 나중에 생겼는지. 그래서 대조군을 함께 건다.
    //
    // +0xA4 는 실측에서 매 프레임 변하는 것이 확인된 값이다. 여기서
    // 히트가 잡히면 브레이크포인트는 동작하는 것이고, FOV 히트가
    // 0인 것은 "안 쓴다"는 뜻이 된다.
    struct Target {
        const char* label;
        std::uintptr_t addr;
        bool poke;          // 값을 바꿔 게임이 되돌리도록 유도할지
    };
    const Target targets[] = {
        {"대조군 +0xA4 (매 프레임 변동 확인된 값)", g_set.active + 0xA4,
         false},
        {"FOV +0x9C (값을 바꿔 되돌림 유도)",
         g_set.active + camera_offset::kFov, true},
        {"컴포넌트 위치 +0x360",
         g_set.player_component != 0 ? g_set.player_component + 0x360 : 0,
         false},
    };

    const auto base = reader.module_base();
    for (const auto& t : targets) {
        if (g_stop.load()) break;
        if (t.addr == 0) continue;

        float saved = 0.0f;
        const bool had = mem::safe_read_float(t.addr, &saved);
        if (t.poke && had) {
            // 원래 값과 다른 값을 써 둔다. 게임이 이 값을 관리한다면
            // 되돌리려 쓸 것이고, 그 쓰기가 잡힌다.
            mem::safe_write_float(t.addr, saved + 7.0f);
        }

        log::infof("추적 [{}] 0x{:X}", t.label, t.addr);
        mem::WriteWatch watch;
        if (!watch.install(t.addr, 4)) {
            log::errorf("  감시를 걸지 못했다");
            continue;
        }
        for (int i = 0; i < 80 && !g_stop.load(); ++i) ::Sleep(100);

        const auto hits = watch.hits();
        log::infof("  히트 {}회, 쓰는 명령 {}곳", watch.hit_count(),
                   hits.size());
        for (const auto rip : hits) {
            log::infof("    0x{:X}  (모듈+0x{:X})", rip,
                       rip >= base ? rip - base : 0);
        }
        watch.remove();

        if (t.poke && had) {
            float now = 0.0f;
            mem::safe_read_float(t.addr, &now);
            log::infof("  써둔 값 {} -> 현재 {} ({})", saved + 7.0f, now,
                       now == saved + 7.0f ? "그대로 - 게임이 안 쓴다"
                                           : "바뀜 - 게임이 썼다");
            mem::safe_write_float(t.addr, saved);
        }
    }
    log::infof("자동 분석 완료");
}

}  // namespace

void start_auto_analysis() {
    if (g_auto.joinable()) return;
    g_stop.store(false);
    g_auto = std::thread(auto_analysis_loop);
}

void stop_auto_analysis() {
    g_stop.store(true);
    if (g_auto.joinable()) g_auto.join();
}

const CameraSet& cameras() { return g_set; }
bool discovered() { return g_done; }

bool read_fov(std::uintptr_t camera, float* out) {
    if (camera == 0 || out == nullptr) return false;
    return mem::safe_read_float(camera + camera_offset::kFov, out);
}

bool read_position(std::uintptr_t camera, float out[3]) {
    if (camera == 0 || out == nullptr) return false;
    return mem::safe_read_bytes(camera + camera_offset::kPosition, out,
                                sizeof(float) * 3);
}

bool read_rotation(std::uintptr_t camera, float out[4]) {
    if (camera == 0 || out == nullptr) return false;
    return mem::safe_read_bytes(camera + camera_offset::kRotation, out,
                                sizeof(float) * 4);
}

bool read_name(std::uintptr_t camera, std::string* out) {
    mem::LocalReader reader;
    return camera_name(reader, camera, out);
}

}  // namespace cdtb::game
