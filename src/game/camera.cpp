#include "game/camera.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>

#include "core/log.h"
#include "game/analysis.h"
#include "game/grant.h"
#include "game/items.h"
#include "mem/reader.h"
#include "mem/rtti.h"
#include "mem/safe_read.h"

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

    // 세션에서 플레이어 액터를 꺼내는 게임 함수를 후킹해 둔다. 게임
    // 안에서 647곳이 부르므로 가만 두어도 곧 값이 들어온다. 지금은
    // 받아 적기만 한다 - 아무것도 쓰지 않는다.
    actor_hook_install(rtti, reader);

    for (int attempt = 1; !g_stop.load(); ++attempt) {
        // 아이템 표도 여기서 읽는다. 350MB 이미지와 힙 전수 조사를
        // 두 번 할 이유가 없어 이미 그것을 한 이 루프에 얹는다.
        // 준비되면 스스로 즉시 빠진다.
        discover_items(rtti, reader);
        if (discover_with(rtti, reader, nullptr) && g_set.active != 0) {
            log::infof("자동 분석: {}번째 시도에 카메라 확보", attempt);
            break;
        }
        for (int i = 0; i < 100 && !g_stop.load(); ++i) {
            ::Sleep(100);   // 10초, 중단 요청에 100ms 안에 반응
        }
    }
    if (g_stop.load()) return;
    // 분석 절차 본체는 analysis.cpp 에 있다. 이 루프는 카메라를
    // 확보할 때까지 기다리는 일만 한다.
    run_analysis(rtti, g_set);

    log::infof("자동 분석 완료");

    // 아이템 이름은 현지화가 올라온 뒤에야 풀린다. 게임은 아이템 표를
    // 먼저 올리므로 위 루프가 도는 동안에는 이름이 비어 있는 것이
    // 정상이다. 예전에는 카메라를 찾는 순간 이 루프를 빠져나가 이름이
    // 영영 비었다 - 로그에 "이름 풀린 것 0개" 로 남았다.
    for (int i = 0; i < 120 && !g_stop.load(); ++i) {
        if (discover_items(rtti, reader)) break;
        for (int j = 0; j < 50 && !g_stop.load(); ++j) {
            ::Sleep(100);   // 5초, 중단 요청에 100ms 안에 반응
        }
    }
    if (!items_named() && !g_stop.load()) {
        log::warnf("아이템 표: 현지화를 끝내 못 봤다 - 이름 없이 키만 낸다");
    }

    // 조회 함수가 무엇을 돌려주는지 전부 남긴다. 클라이언트 쪽과
    // 서버 쪽 인벤토리 컴포넌트가 둘 다 살아 있어서, 치트 경로가
    // 어느 쪽을 받는지 이걸로 가린다. 읽기만 한다.
    std::uintptr_t seen[16]{};
    int shown = 0;
    for (int i = 0; i < 90 && !g_stop.load(); ++i) {
        const int n = seen_actors(seen, 16);
        for (; shown < n; ++shown) {
            log::infof("액터 후보 {} 0x{:X} ({})", shown + 1, seen[shown],
                       rtti.class_of_object(seen[shown]));
        }
        for (int j = 0; j < 20 && !g_stop.load(); ++j) ::Sleep(100);
    }
    if (shown == 0 && !g_stop.load()) {
        log::warnf("액터를 한 번도 못 봤다 - 후킹이 안 걸렸을 수 있다");
    }
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

bool read_world_position(std::uintptr_t component, float out[3]) {
    if (component == 0 || out == nullptr) return false;
    return mem::safe_read_bytes(component + component_offset::kWorldPosition,
                                out, sizeof(float) * 3);
}

bool read_name(std::uintptr_t camera, std::string* out) {
    mem::LocalReader reader;
    return camera_name(reader, camera, out);
}

}  // namespace cdtb::game
