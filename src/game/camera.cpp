#include "game/camera.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>

#include "core/log.h"
#include "game/analysis.h"
#include <string>

#include "game/grant.h"
#include "game/inventory.h"
#include "game/items.h"
#include "game/roster.h"
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
// 조회 함수가 무엇을 돌려주는지 남긴다. 클라이언트 쪽과 서버 쪽
// 인벤토리 컴포넌트가 둘 다 살아 있어서, 치트 경로가 어느 쪽을
// 받는지 이걸로 가린다. 읽기만 한다.
//
// 카메라 확보 뒤에 두었더니 카메라를 못 찾는 동안 이 로그도 같이
// 막혔다. 카메라와 무관하게 매 시도마다 낸다.
void log_new_actors(const mem::Rtti& rtti, const mem::Reader& reader) {
    std::uintptr_t seen[16]{};
    std::uint32_t hits[16]{};
    const int n = seen_sessions(seen, hits, 16);
    for (int i = 0; i < n; ++i) {
        const std::uintptr_t actor = session_actor(i);
        if (actor == 0 || session_class(i)[0] != 0) continue;
        const std::string cls = rtti.class_of_object(actor);
        if (cls.empty()) continue;
        set_session_class(i, cls.c_str());
        log::infof("세션 {} 0x{:X} -> 액터 0x{:X} ({})", i + 1, seen[i], actor,
                   cls);
        // 치트 35개가 전부 지나는 문. 세션마다 한 번만 본다.
        log_gate(rtti, reader, seen[i]);
    }
}

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
    tick_hook_install(rtti, reader);
    // 프레임 경계 조사용 계측 훅(펌프·작업 래퍼)은 조사가 끝나 끈다.
    // 이 훅들은 전용 워커 스레드의 장기 실행 루프에 걸려 메시지
    // 파이프라인 타이밍을 흔들 위험이 있다(2026-09-04). 코드는 남겨
    // 두되 설치하지 않는다.
    //   pump_hook_install(rtti, reader);
    //   taskrun_hook_install(rtti, reader);
    spawn_resolve_message(rtti, reader);
    spawn_resolve(rtti, reader);
    entity_hook_install(rtti, reader);
    socket_capture_install(rtti, reader);
    // 인벤토리 레코드 +0x08 의 값이 어느 표에서 조회되는지 잡는다.
    // 늑대의 한손검. 인벤토리 첫 칸이고 현지화에 이름이 있다.
    table_probe_install(rtti, reader, 1163042);
    spawn_trace_install();

    for (int attempt = 1; !g_stop.load(); ++attempt) {
        // 아이템 표도 여기서 읽는다. 350MB 이미지와 힙 전수 조사를
        // 두 번 할 이유가 없어 이미 그것을 한 이 루프에 얹는다.
        // 준비되면 스스로 즉시 빠진다.
        discover_items(rtti, reader);
        // 소켓 지급이 키 -> 순번 대응표를 쓴다. 아이템 표가 선 뒤에
        // 한 번만 읽고 스스로 빠진다.
        discover_item_ids(rtti, reader);
        // 인벤토리 창이 쓴다. 찾으면 스스로 빠진다.
        discover_inventory(rtti, reader);
        // 탈것·용병·캐릭터 카탈로그. 아이템 표와 같은 인프라라 여기
        // 얹는다. 이름까지 풀리면 스스로 빠진다.
        discover_roster(rtti, reader);
        log_new_actors(rtti, reader);
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
        discover_item_ids(rtti, reader);
        discover_inventory(rtti, reader);
        discover_roster(rtti, reader);
        if (discover_items(rtti, reader) && inventory_ready()) break;
        for (int j = 0; j < 50 && !g_stop.load(); ++j) {
            ::Sleep(100);   // 5초, 중단 요청에 100ms 안에 반응
        }
    }
    if (!items_named() && !g_stop.load()) {
        log::warnf("아이템 표: 현지화를 끝내 못 봤다 - 이름 없이 키만 낸다");
    }

    // 세션은 플레이하는 내내 새로 생긴다. 위의 두 루프는 각각 카메라와
    // 아이템 이름을 얻으면 끝나므로, 그 뒤에 잡힌 세션에는 이름표가
    // 붙지 않았다 - 실측에서 목록이 전부 "확인 중" 이었다. 종료할
    // 때까지 계속 붙인다. 아직 안 붙은 것만 보므로 값싸다.
    // 엔티티 ID 는 처음 30초만 봐도 충분히 갈린다. 플레이어 것은
    // 게임이 계속 조회하므로 횟수가 압도적이다.
    int ent_reports = 0;
    int spin = 0;
    while (!g_stop.load()) {
        log_new_actors(rtti, reader);

        // 인벤토리는 월드에 들어간 뒤에야 생긴다. 카메라와 아이템
        // 표보다 늦어서, 앞의 루프들이 먼저 끝나면 못 잡은 채로
        // 남았다 - 실측에서 창이 "컴포넌트를 아직 못 찾았습니다"
        // 에서 멈췄다. 여기서 계속 찾는다. 화면의 "다시 찾기" 가
        // 캐시를 비우면 여기서 다시 잡는다.
        //
        // 못 찾은 동안에만 훑는다. RTTI 인스턴스 탐색은 힙 전수라
        // 값싸지 않아 10초에 한 번으로 줄인다. 다만 화면에서 "다시
        // 찾기" 를 누른 사람에게 10초는 "눌렀는데 아무 일도 안 난다"
        // 라, 요청이 남아 있으면 주기를 기다리지 않고 지금 훑는다.
        const bool asked = take_inventory_rescan();
        if (!inventory_ready() && (asked || (spin % 5) == 0)) {
            discover_inventory(rtti, reader);
        }
        ++spin;

        if (ent_reports < 6) {
            std::uint32_t ids[32]{};
            std::uint32_t hits[32]{};
            const int n = seen_entities(ids, hits, 32);
            if (n > 0) {
                // 많이 불린 순으로 위쪽 몇 개만 낸다.
                int order[32];
                for (int i = 0; i < n; ++i) order[i] = i;
                for (int i = 0; i < n; ++i) {
                    for (int j = i + 1; j < n; ++j) {
                        if (hits[order[j]] > hits[order[i]]) {
                            const int t = order[i];
                            order[i] = order[j];
                            order[j] = t;
                        }
                    }
                }
                std::string line;
                for (int i = 0; i < n && i < 6; ++i) {
                    line += std::to_string(ids[order[i]]) + "(" +
                            std::to_string(hits[order[i]]) + "회) ";
                }
                log::infof("엔티티 ID 후보 {}개: {}", n, line);
                ++ent_reports;
            }
        }

        for (int i = 0; i < 20 && !g_stop.load(); ++i) ::Sleep(100);
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
