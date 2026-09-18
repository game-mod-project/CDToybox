#include "game/camera.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cstring>

#include "core/log.h"
#include "game/analysis.h"
#include <string>

#include "game/equip.h"
#include "game/actors.h"
#include "game/clan.h"
#include "game/knowledge.h"
#include "game/skillpoint.h"
#include "game/companion.h"
#include "game/grant.h"
#include "game/inventory.h"
#include "game/items.h"
#include "game/nofall.h"
#include "game/specguard.h"
#include "game/spec_heal.h"
#include "game/player.h"
#include "game/roster.h"
#include "game/wanted.h"
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
std::vector<std::string> camera_scan_classes() {
    return {".?AVFreeCamCamera@pa@@", ".?AVPhotoCamera@pa@@", ".?AVCameraManager@pa@@",
            ".?AVPlayerCameraComponent@pa@@"};
}

// 한 통과의 단계들(아이템표·인벤토리·로스터·액터 매니저·카메라·명부)이 힙에서 찾는 클래스
// 전부. 통과 시작에 한 번 훑어 두면(prefetch_instances) 단계들은 캐시에서 낸다.
std::vector<std::string> pass_scan_classes() {
    std::vector<std::string> all;
    for (auto part : {&items_scan_classes, &inventory_scan_classes,
                             &roster_scan_classes, &actors_scan_classes,
                             &camera_scan_classes, &clan_scan_classes}) {
        const auto names = part();
        all.insert(all.end(), names.begin(), names.end());
    }
    return all;
}

// 통과 한 바퀴의 미리 훑기 창. 만들 때 훑고(로그 한 줄), 블록을 어떻게 나가든(break·return·
// 예외) 소멸자가 비운다 - 손으로 짝지으면 조기 반환이 생길 때 스냅숏이 본 루프로 샌다
// (리뷰 O-1).
namespace {
struct PrefetchScope {
    const mem::Rtti& rtti;
    PrefetchScope(const mem::Rtti& r, const char* what) : rtti(r) {
        rtti.prefetch_instances(pass_scan_classes(), kPassScanPerClass);
        const auto ps = rtti.prefetch_stats();
        if (ps.capped == 0) {
            log::infof("힙 훑기({}): 클래스 {}개에서 객체 {}개", what, ps.names, ps.objects);
        } else {
            log::infof("힙 훑기({}): 클래스 {}개에서 객체 {}개 - 그중 {}개 클래스는 상한 {}에 "
                       "닿아 그 단계는 다시 걷는다",
                       what, ps.names, ps.objects, ps.capped, kPassScanPerClass);
        }
    }
    ~PrefetchScope() { rtti.clear_prefetch(); }
    PrefetchScope(const PrefetchScope&) = delete;
    PrefetchScope& operator=(const PrefetchScope&) = delete;
};
}  // namespace

bool discover_with(const mem::Rtti& rtti, const mem::Reader& reader,
                   CameraSet* out) {
    CameraSet found;

    // 네 클래스의 인스턴스를 힙 한 번 훑기로 모은다. instances_of_class 는 vtable 마다 힙
    // 전체를 읽으므로(이 넷은 vtable 7개 = 힙 전수 7회) 월드 안에서 122초가 걸렸다(실측
    // 2026-09-12) - 카메라를 못 잡은 통과가 한 바퀴 더 돌면 그 사이 이름 채우기가 뒤로
    // 밀려 창마다 "불러오는 중" 이 길어졌다. 후보에는 vtable 값을 우연히 담은 메모리가
    // 섞여 있어 이름으로 진짜를 고른다. 상한은 네 클래스가 나눠 쓰고 낮은 주소부터 채우므로
    // (실측 후보 합계 14개) 닿으면 경고를 남긴다 - 미확보가 이어질 때 "객체가 아직 없다" 와
    // 가르기 위해서다(리뷰 C-2).
    constexpr std::size_t kScanMax = 1024;
    const std::vector<std::string> kClasses = camera_scan_classes();
    std::vector<std::uintptr_t> free_cams, photo_cams, managers, comps;
    const auto scanned = rtti.find_objects_of(kClasses, kScanMax);
    for (const auto& f : scanned) {
        if (f.cls == kClasses[0]) free_cams.push_back(f.address);
        else if (f.cls == kClasses[1]) photo_cams.push_back(f.address);
        else if (f.cls == kClasses[2]) managers.push_back(f.address);
        else if (f.cls == kClasses[3]) comps.push_back(f.address);
    }
    log::infof("카메라 후보(힙 한 번 훑기): 프리캠 {} 포토캠 {} 매니저 {} 플레이어 컴포넌트 {}",
               free_cams.size(), photo_cams.size(), managers.size(), comps.size());
    if (scanned.size() >= kScanMax) {
        log::warnf("카메라 후보가 상한 {}개에 닿았다 - 가짜 후보가 많아 진짜를 놓쳤을 수 있다",
                   kScanMax);
    }
    found.free_cam = pick_named(reader, free_cams, "FreeCamera");
    found.photo_cam = pick_named(reader, photo_cams, "PhotoCamera");

    // CameraManager 는 이름이 없다. +0x18 이 방금 찾은 프리카메라를
    // 가리키는지로 검증한다.
    for (const auto a : managers) {
        std::uint64_t slot = 0;
        if (!reader.read(a + 0x18, &slot, sizeof(slot))) continue;
        if (found.free_cam != 0 && slot == found.free_cam) {
            found.manager = a;
            break;
        }
    }

    // PlayerCameraComponent 도 이름이 없다. +0x88 이 가리키는 곳에서
    // 0x28 을 빼면 활성 카메라이고, 그 이름이 "PlayerCamera" 여야 한다.
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

    // RTTI 색인도 여기서 한 번에 만든다. 이미지는 안 바뀌므로 색인은
    // 이미지의 순수 함수다. 예전에는 조회마다 350MB 를 다시 훑어
    // (find_types 는 1바이트씩) 탐색 통과 한 번에 2분 30초가 걸렸다.
    {
        const auto t0 = ::GetTickCount64();
        rtti.build_index();
        const auto st = rtti.index_stats();
        log::infof("RTTI 색인: 타입 {}개, vtable {}개 ({}ms)", st.types,
                   st.vtables, ::GetTickCount64() - t0);
    }
    log::infof("자동 분석 시작 - 월드 진입을 기다린다");

    // 세션에서 플레이어 액터를 꺼내는 게임 함수를 후킹해 둔다. 게임
    // 안에서 647곳이 부르므로 가만 두어도 곧 값이 들어온다. 지금은
    // 받아 적기만 한다 - 아무것도 쓰지 않는다.
    actor_hook_install(rtti, reader);
    tick_hook_install(rtti, reader);
    // 낙사 방지 훅도 여기서 건다. RTTI 이미지와 모듈 베이스만 있으면 되고
    // 월드·카메라·아이템 표를 기다릴 이유가 없다 - 예전에는 아래 세 번째
    // 루프에 있어 **월드 진입 뒤 1~2분**이 지나야 설치됐다(실측 2026-09-12:
    // 08:24 실행 -> 08:25:59 설치). 플레이어를 보호하는 훅이 아이템 이름
    // 채우기 뒤에 줄 설 이유가 없다.
    nofall_install(rtti, reader);
    // 프레임 경계 조사용 계측 훅(펌프·작업 래퍼)은 조사가 끝나 끈다.
    // 이 훅들은 전용 워커 스레드의 장기 실행 루프에 걸려 메시지
    // 파이프라인 타이밍을 흔들 위험이 있다(2026-09-04). 코드는 남겨
    // 두되 설치하지 않는다.
    //   pump_hook_install(rtti, reader);
    //   taskrun_hook_install(rtti, reader);
    spawn_resolve_message(rtti, reader);
    entity_hook_install(rtti, reader);
    // 동반자 획득 경로 메시지 캡처(Phase 1, 진단). 길들이기 때 뜬다.
    companion_capture_install(rtti, reader);
    // 2976(아이템 사용) 구동 준비 + 명령 파일 감시(밖에서 실험을 건다).
    companion_use_item_resolve(rtti, reader);
    companion_resolve_messages(rtti, reader);
    // 수배 해제(2646). 해석만 한다 - 보내는 것은 화면에서 누를 때다.
    wanted_resolve(rtti, reader);
    // 벌금 사슬의 컴포넌트. 힙 전수 탐색이라 여기서 한 번만 한다
    // (세이브를 다시 부르면 죽고, 그때는 화면 쪽에서 다시 찾는다).
    wanted_component_find(rtti, reader);
    companion_hire_trace_install(reader);
    companion_spawn_trace_install(reader);
    // 좌표는 카메라 쪽만 안다. 동반자 코드에 넣어 준다.
    companion_set_position_source([](float out[3]) {
        const CameraSet& set = cameras();
        if (set.player_component == 0) return false;
        return read_world_position(set.player_component, out);
    });
    companion_command_start(reader);

    for (int attempt = 1; !g_stop.load(); ++attempt) {
        // 한 통과가 얼마나 걸리는지 남긴다. 어디가 느린지 로그만 보고
        // 가릴 수 있어야 한다 - 실측 2026-09-08 에 통과 한 번이 2분
        // 30초였는데 로그에 이정표가 없어 짐작으로만 봤다.
        const auto pass_t0 = ::GetTickCount64();
        auto step = [](const char* what, unsigned long long t0) {
            const auto ms = ::GetTickCount64() - t0;
            if (ms >= 500) log::infof("탐색 [{}] {}ms", what, ms);
            return ::GetTickCount64();
        };
        auto t = pass_t0;

        // 이 통과의 단계들이 찾는 클래스를 힙 한 번 훑기로 미리 모은다(mem/rtti.h
        // prefetch_instances). 단계마다 vtable 수만큼 힙을 읽던 것(한 통과에 10회 넘게,
        // 월드 안에서 단계마다 10~30초)이 한 번이 된다. 준비된 단계는 어차피 안 찾고, 아직
        // 없는 객체는 빈 결과로 끝난다(다음 통과가 다시 훑는다). 통과 끝에 비운다 - 힙은
        // 바뀐다.
        const PrefetchScope prefetch_scope(rtti, "통과");
        t = step("힙 훑기", t);

        // 벌금 사슬의 컴포넌트. 월드 밖에서는 아직 없으므로 통과마다
        // 다시 본다 - 안에서 스스로 15초에 한 번으로 막는다.
        wanted_component_find(rtti, reader);
        t = step("수배 컴포넌트", t);

        // 아이템 표도 여기서 읽는다. 350MB 이미지와 힙 전수 조사를
        // 두 번 할 이유가 없어 이미 그것을 한 이 루프에 얹는다.
        // 준비되면 스스로 즉시 빠진다.
        discover_items(rtti, reader);
        t = step("아이템표", t);
        // 소켓 지급이 키 -> 순번 대응표를 쓴다. 아이템 표가 선 뒤에
        // 한 번만 읽고 스스로 빠진다.
        discover_item_ids(rtti, reader);
        t = step("아이템 대응표", t);
        // 인벤토리 창이 쓴다. 찾으면 스스로 빠진다.
        discover_inventory(rtti, reader);
        t = step("인벤토리", t);
        // 탈것·용병·캐릭터 카탈로그. 아이템 표와 같은 인프라라 여기
        // 얹는다. 이름까지 풀리면 스스로 빠진다.
        discover_roster(rtti, reader);
        t = step("로스터", t);
        discover_actor_manager(rtti, reader);
        t = step("액터 매니저", t);
        log_new_actors(rtti, reader);
        t = step("액터 목록", t);
        const bool got_cam = discover_with(rtti, reader, nullptr);
        t = step("카메라", t);
        // 내 동반자 명부(용병단 컴포넌트)는 **맨 뒤에서** 잡는다.
        //
        // RTTI 인스턴스 스캔이라 실측 10.7초가 든다. 앞에 두면 그만큼
        // 아이템 대응표가 늦어진다 - 사용자가 "인벤토리 로드가 너무
        // 오래 걸린다" 고 짚은 것이 이것이다(2026-09-09). 명부는 월드에
        // 들어가 동반자 기능을 쓸 때나 필요하므로 급하지 않다.
        //
        // 그래도 배경에서 미리 잡아 두기는 해야 한다 - 그리는 스레드가
        // 이 스캔을 돌면 종 바꾸기에서 게임이 멈춘다(game/clan.cpp).
        discover_clan(rtti, reader);
        step("동반자 명부", t);
        rtti.clear_prefetch();   // 통과 사이(10초)엔 캐시를 비워 둔다 - 소멸자는 안전망
        log::infof("탐색 {}번째 통과: {}ms", attempt,
                   ::GetTickCount64() - pass_t0);
        if (got_cam && g_set.active != 0) {
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
        const PrefetchScope prefetch_scope(rtti, "이름 채우기");   // 위 루프와 같은 이유
        discover_item_ids(rtti, reader);
        discover_inventory(rtti, reader);
        discover_roster(rtti, reader);
        discover_actor_manager(rtti, reader);
        // 명부 탐색은 아이템 이름보다 뒤다 - 앞에 두면 매 바퀴 10초 넘게
        // 잡아먹어 대응표가 그만큼 늦는다(2026-09-09).
        const bool items_done = discover_items(rtti, reader);
        // 아이템 **대응표**는 아이템 표가 끝난 뒤에야 된다. 위에서 한 번
        // 부르지만 그때는 아직 표가 없어 실패하고, 바로 아래 break 로
        // 빠져나가 다시 시도할 기회가 없었다 - 그래서 인벤토리 패널이
        // "대응표를 아직 못 읽었습니다" 로 남았다(실측 2026-09-09).
        // 표가 방금 완성됐을 수 있으니 여기서 한 번 더 부른다.
        if (items_done) discover_item_ids(rtti, reader);
        // 명부: 값싼 세션 사슬 먼저, RTTI 스캔은 월드 안에서만(clan.cpp 가 가른다).
        discover_clan(rtti, reader);
        rtti.clear_prefetch();   // 반복 사이(5초)엔 캐시를 비워 둔다 - 소멸자는 안전망
        if (items_done && inventory_ready() && item_ids_ready()) break;
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

        // 명부 컴포넌트도 월드에 들어가야 생기고, 앞의 루프들이 먼저 끝나면 못 잡은
        // 채 남았다(실측 2026-09-11: 2번 루프 종료 21:38:47, 세션 21:39:05). 여기서
        // 계속 본다 - 세션 사슬 두 번 읽기라 값싸고, RTTI 스캔은 clan.cpp 가 월드
        // 안·30초 간격으로 막는다. 별도 대기 루프로 두면 이 루프(세션 이름표·인벤·
        // 장비·치트)를 막아 스스로를 굶긴다(리뷰 C1·C2).
        if (!clan_ready() || clan_rtti() == nullptr) discover_clan(rtti, reader);

        // 스킬 포인트(어비스 결속). 지식 컴포넌트도 월드에 들어가야 생긴다.
        // **월드 게이트 + 스로틀이 둘 다 필요하다** - find_objects_of 는 프리페치
        // 밖이라 부를 때마다 힙 전수를 읽는데, 이 루프는 2초에 한 바퀴다. 인벤토리
        // 탐색이 같은 이유로 5바퀴 주기와 포기 카운터를 두고 있다.
        // 장비가 잡혔다는 것이 곧 월드 안이라는 뜻이라 그것을 게이트로 쓴다.
        if (equip_ready()) {
            knowledge_check_alive(reader);
            if (!knowledge_ready()) {
                discover_knowledge(rtti, reader, spin, player_char());
            }
        }
        // 리로드는 컴포넌트를 새로 만들고, 저장을 잊고 끄면 쓴 값이 안 남는다.
        // 기억해 둔 것이 모자라면 다시 건다(건 것이 없으면 즉시 반환).
        // **장비 게이트 밖에 둔다** - 재적용은 탐색과 달리 힙을 안 훑어 값싸고,
        // 장비를 못 읽는 동안에도 지식 컴포넌트만 살아 있으면 걸 수 있다.
        know_auto_tick(reader);

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
        // **세이브를 불러오면 게임이 인벤토리를 통째로 새로 만든다.** 예전에는
        // 캐시가 죽은 포인터를 든 채 남아, 패널이 낡은 값을 보이고 가방 확장이
        // 조용히 아무것도 안 했다(2026-09-13 "세이브-로드 후 300 유지 안 됨").
        inventory_check_alive(reader);

        const bool asked = take_inventory_rescan();
        // **두 realm 을 다** 잡을 때까지 본다. 서버만 보고 멈추면 클라를 영영
        // 못 찾아 가방 확장이 한쪽에만 간다(리뷰 지적 4).
        if (!inventory_both_ready() && (asked || (spin % 5) == 0)) {
            discover_inventory(rtti, reader);
        }
        // 새 인벤토리가 잡혔으면, 이번 실행에서 사용자가 걸어 둔 가방 확장을
        // 다시 건다(무장돼 있을 때만 - 적용을 누른 적이 없으면 아무 일도 없다).
        bag_auto_tick(reader);
        ++spin;

        // 장비 에디터(힙 스캔은 못 잡았을 때만). 플레이어 = 정신력 풀 + 착용
        // 최다로 고른다.
        if (equip_take_refresh() || !equip_ready()) {
            equip_discover(rtti, reader);
        } else {
            equip_refresh_pieces(reader);
        }

        // 플레이어 치트(B-1). **힙 스캔 없음** - equip 이 고른 플레이어 comp 에서
        // 게이지 배열을 값싸게 잡아 고정. 실제 freeze 는 렌더 프레임(~16ms).
        player_discover(reader);

        // 낙사 방지 훅은 위(분석 스레드 머리)에서 이미 걸었다. 여기 남긴 것은
        // **재시도**다 - alloc_near 가 한 번 실패하면 그때는 미지원으로 갈리지
        // 않으므로 다시 볼 기회가 있어야 한다. 설치됐거나 미지원으로 갈렸으면
        // 원자 적재 한 번으로 곧장 돌아온다(이중 설치는 CAS 가 막는다).
        // 내 root 갱신은 **렌더 틱**(overlay.cpp)이 맡는다 - 통과 한 바퀴는 수십
        // 초라, 캐릭터를 바꾸면 낡은 root 로 남는 창이 너무 길다.
        nofall_install(rtti, reader);
        specguard_install(reader);   // 백업(렌더 루프가 먼저 설치)
        heal_special_items(reader);   // 특수아이템 표시 보정

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
    companion_command_stop();
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
