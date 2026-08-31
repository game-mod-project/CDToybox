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

// 다중 상속이면 같은 객체가 서브객체 주소로도 잡힌다.
// 가장 작은 주소가 객체 시작이다.
std::uintptr_t lowest(const std::vector<std::uintptr_t>& v) {
    if (v.empty()) return 0;
    return *std::min_element(v.begin(), v.end());
}

// 카메라는 +0x48 에 이름 객체를 들고 있고, 이름 객체는 +0x18 에
// 짧은 문자열을 인라인으로 담는다(SSO). 이름은 참조가 아니라
// 복사되어 들어가므로 문자열 리터럴 주소로는 찾을 수 없다.
bool name_of(std::uintptr_t camera, std::string* out) {
    if (camera == 0 || out == nullptr) return false;

    std::uint64_t name_obj = 0;
    if (!mem::safe_read_bytes(camera + camera_offset::kName, &name_obj,
                              sizeof(name_obj))) {
        return false;
    }
    if (name_obj == 0) return false;

    std::uint32_t len = 0;
    if (!mem::safe_read_bytes(static_cast<std::uintptr_t>(name_obj) + 8, &len,
                              sizeof(len))) {
        return false;
    }
    if (len == 0 || len > 64) return false;

    char buf[65]{};
    if (!mem::safe_read_bytes(static_cast<std::uintptr_t>(name_obj) + 0x18,
                              buf, len)) {
        return false;
    }
    buf[len] = 0;
    out->assign(buf, len);
    return true;
}

}  // namespace

// 이미지를 이미 읽어 둔 Rtti 로 탐색한다. 재시도 루프가 350MB를
// 매번 다시 읽지 않도록 분리했다.
bool discover_with(const mem::Rtti& rtti, CameraSet* out) {
    CameraSet found;

    found.manager =
        lowest(rtti.instances_of_class(".?AVCameraManager@pa@@", 8));
    found.free_cam =
        lowest(rtti.instances_of_class(".?AVFreeCamCamera@pa@@", 8));
    found.photo_cam =
        lowest(rtti.instances_of_class(".?AVPhotoCamera@pa@@", 8));
    found.player_component =
        lowest(rtti.instances_of_class(".?AVPlayerCameraComponent@pa@@", 8));

    log::infof("카메라 탐색: manager={} freeCam={} photo={} playerComp={}",
               reinterpret_cast<void*>(found.manager),
               reinterpret_cast<void*>(found.free_cam),
               reinterpret_cast<void*>(found.photo_cam),
               reinterpret_cast<void*>(found.player_component));

    // 활성 카메라는 RTTI에 노출되지 않는다. PlayerCameraComponent 가
    // +0x88 에 그 ICamera 서브객체를 들고 있고, 객체 시작은 -0x28 이다.
    if (found.player_component != 0) {
        std::uint64_t icam = 0;
        if (mem::safe_read_bytes(found.player_component + 0x88, &icam,
                                 sizeof(icam)) &&
            icam > 0x10000) {
            found.active = static_cast<std::uintptr_t>(icam) - 0x28;
        }
    }

    // 이름으로 검증한다. 활성 카메라는 "PlayerCamera" 여야 한다.
    std::string n;
    if (found.active != 0 && name_of(found.active, &n)) {
        log::infof("활성 카메라 후보 {} 이름='{}'",
                   reinterpret_cast<void*>(found.active), n);
        if (n != "PlayerCamera") {
            log::warnf("이름이 예상과 다르다 - 활성 카메라 추정을 버린다");
            found.active = 0;
        }
    } else if (found.active != 0) {
        log::warnf("활성 카메라 후보의 이름을 읽지 못했다");
        found.active = 0;
    }

    if (found.free_cam != 0 && name_of(found.free_cam, &n)) {
        log::infof("프리카메라 이름='{}'", n);
    }

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
        if (discover_with(rtti, nullptr) && g_set.active != 0) {
            log::infof("자동 분석: {}번째 시도에 카메라 확보", attempt);
            break;
        }
        for (int i = 0; i < 100 && !g_stop.load(); ++i) {
            ::Sleep(100);   // 10초, 중단 요청에 100ms 안에 반응
        }
    }
    if (g_stop.load()) return;

    // 카메라를 찾았다. 이제 누가 값을 쓰는지 추적한다.
    const std::uintptr_t target = g_set.active + camera_offset::kFov;
    log::infof("자동 분석: FOV(0x{:X}) 쓰기 추적 시작", target);

    mem::WriteWatch watch;
    if (!watch.install(target, 4)) {
        log::errorf("자동 분석: 쓰기 감시를 걸지 못했다");
        return;
    }

    // 15초면 60fps 기준 900프레임이다. 충분하다.
    for (int i = 0; i < 150 && !g_stop.load(); ++i) ::Sleep(100);

    const auto hits = watch.hits();
    const auto base = reader.module_base();
    log::infof("자동 분석: 히트 {}회, 쓰는 명령 {}곳", watch.hit_count(),
               hits.size());
    for (const auto rip : hits) {
        log::infof("  쓰는 명령 0x{:X}  (모듈+0x{:X})", rip,
                   rip >= base ? rip - base : 0);
    }
    watch.remove();
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
    return name_of(camera, out);
}

}  // namespace cdtb::game
