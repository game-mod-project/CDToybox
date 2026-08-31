#include "game/analysis.h"

#include <windows.h>

#include <cmath>
#include <cstring>
#include <format>
#include <string>

#include "core/log.h"
#include "mem/reader.h"
#include "mem/rtti.h"
#include "mem/safe_read.h"
#include "mem/watchpoint.h"

namespace cdtb::game {
namespace {

// 값이 float 로 읽어 말이 되는 범위인지. 덤프에서 어느 칸이 좌표이고
// 어느 칸이 포인터인지 눈으로 가르기 위한 것이다.
bool plausible_float(float v) {
    if (!std::isfinite(v)) return false;
    if (v == 0.0f) return true;
    const float a = std::fabs(v);
    return a > 1e-6f && a < 1e9f;
}

const char* name_of(std::uintptr_t v, const CameraSet& set) {
    if (v == 0) return "";
    if (v == set.manager) return "  <- CameraManager";
    if (v == set.free_cam) return "  <- FreeCamCamera";
    if (v == set.photo_cam) return "  <- PhotoCamera";
    if (v == set.active) return "  <- 활성 카메라";
    if (v == set.player_component) return "  <- PlayerCameraComponent";
    if (set.free_cam != 0 && v == set.free_cam + 0x28) return "  <- FreeCamCamera+0x28";
    if (set.active != 0 && v == set.active + 0x28) return "  <- 활성 카메라+0x28";
    if (set.photo_cam != 0 && v == set.photo_cam + 0x28) return "  <- PhotoCamera+0x28";
    return "";
}

// 슬롯을 동시에 걸고 지정한 시간만큼 관찰한다.
// 감시 중 새로 생긴 스레드에도 계속 디버그 레지스터를 건다.
void observe(mem::WriteWatch& watch, int seconds) {
    if (!watch.install()) {
        log::errorf("  감시를 걸지 못했다");
        return;
    }

    int added = 0;
    for (int i = 0; i < seconds * 2; ++i) {
        ::Sleep(500);
        added += watch.refresh_threads();   // 새로 생긴 스레드 흡수
    }
    if (added > 0) {
        log::infof("  감시 중 새로 생긴 스레드 {}개에 추가로 걸었다", added);
    }

    const mem::LocalReader reader;
    const auto base = reader.module_base();
    for (const auto& s : watch.results()) {
        log::infof("  [{}] 0x{:X}  히트 {}회, 쓰는 명령 {}곳", s.label, s.addr,
                   s.total, s.rips.size());
        for (const auto& h : s.rips) {
            log::infof("      0x{:X}  (모듈+0x{:X})  {}회", h.rip,
                       h.rip >= base ? h.rip - base : 0, h.count);
        }
    }
    watch.remove();
}

}  // namespace

void log_protection(const char* what, std::uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
        log::infof("{} 0x{:X}: VirtualQuery 실패", what, addr);
        return;
    }
    const bool writable =
        (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                        PAGE_EXECUTE_WRITECOPY)) != 0;
    log::infof("{} 0x{:X}: protect=0x{:X} state=0x{:X} {}", what, addr,
               mbi.Protect, mbi.State, writable ? "쓰기 가능" : "쓰기 불가");
}

void dump_qwords(const char* what, std::uintptr_t base, int bytes,
                 const CameraSet& set) {
    if (base == 0) return;
    log::infof("=== {} 0x{:X} 덤프 ===", what, base);
    for (int off = 0; off < bytes; off += 8) {
        std::uint64_t q = 0;
        if (!mem::safe_read_bytes(base + off, &q, sizeof(q))) {
            log::infof("  +0x{:03X}  (읽기 실패)", off);
            continue;
        }
        float f0 = 0.0f, f1 = 0.0f;
        std::memcpy(&f0, reinterpret_cast<const char*>(&q), 4);
        std::memcpy(&f1, reinterpret_cast<const char*>(&q) + 4, 4);

        std::string extra;
        if (plausible_float(f0) || plausible_float(f1)) {
            extra = std::format("  f=({:.4g}, {:.4g})", f0, f1);
        }
        log::infof("  +0x{:03X}  {:016X}{}{}", off, q, extra,
                   name_of(static_cast<std::uintptr_t>(q), set));
    }
}

void dump_camera_diff(std::uintptr_t a, std::uintptr_t b, int bytes) {
    if (a == 0 || b == 0) return;
    log::infof("=== 활성 카메라(0x{:X}) vs 프리카메라(0x{:X}) 필드 차이 ===", a, b);
    log::infof("  같은 클래스 레이아웃이므로, 다른 칸이 곧 상태 플래그 후보다");

    int same = 0;
    for (int off = 0; off < bytes; off += 4) {
        std::uint32_t va = 0, vb = 0;
        if (!mem::safe_read_bytes(a + off, &va, 4)) continue;
        if (!mem::safe_read_bytes(b + off, &vb, 4)) continue;
        if (va == vb) {
            ++same;
            continue;
        }
        float fa = 0.0f, fb = 0.0f;
        std::memcpy(&fa, &va, 4);
        std::memcpy(&fb, &vb, 4);
        if (plausible_float(fa) && plausible_float(fb)) {
            log::infof("  +0x{:03X}  활성 {:08X} ({:.4g})   프리 {:08X} ({:.4g})",
                       off, va, fa, vb, fb);
        } else {
            log::infof("  +0x{:03X}  활성 {:08X}              프리 {:08X}", off,
                       va, vb);
        }
    }
    log::infof("  (동일한 칸 {}개는 생략)", same);
}

void dump_vtable(const mem::Rtti& rtti, const char* what, std::uintptr_t object,
                 int entries) {
    if (object == 0) return;

    std::uint64_t vtable = 0;
    if (!mem::safe_read_bytes(object, &vtable, sizeof(vtable)) || vtable == 0) {
        log::infof("=== {} vtable 읽기 실패 ===", what);
        return;
    }

    const mem::LocalReader reader;
    const auto base = reader.module_base();

    const auto name = rtti.class_of_vtable(static_cast<std::uintptr_t>(vtable));
    log::infof("=== {} vtable 0x{:X}  ({}) ===", what, vtable,
               name.empty() ? std::string("이름 없음") : name);

    for (int i = 0; i < entries; ++i) {
        std::uint64_t fn = 0;
        if (!mem::safe_read_bytes(static_cast<std::uintptr_t>(vtable) + i * 8, &fn,
                                  sizeof(fn))) {
            break;
        }
        // 모듈 범위를 벗어나면 vtable 끝으로 본다.
        if (fn < base || fn - base > 0x20000000) break;
        log::infof("  [{:2}] 0x{:X}  (모듈+0x{:X})", i, fn, fn - base);
    }
}

void run_analysis(const mem::Rtti& rtti, const CameraSet& set) {
    if (set.active == 0) return;

    // --- 1단계: 읽기만 한다. 무위험. ---
    log_protection("활성 카메라", set.active);
    if (set.free_cam != 0) log_protection("프리카메라", set.free_cam);

    dump_qwords("CameraManager", set.manager, 0x100, set);
    dump_qwords("PlayerCameraComponent", set.player_component, 0x100, set);
    dump_camera_diff(set.active, set.free_cam, 0x140);

    // 카메라 전환은 CameraManager 의 가상 메서드일 가능성이 높다.
    // 함수 주소를 얻어 두면 게임을 끈 뒤 정적으로 분석할 수 있다.
    dump_vtable(rtti, "CameraManager", set.manager, 40);
    dump_vtable(rtti, "FreeCamCamera", set.free_cam, 40);
    dump_vtable(rtti, "PlayerCameraComponent", set.player_component, 40);

    // --- 2단계: 카메라 월드 좌표를 누가 쓰는가 ---
    //
    // 이 단계의 핵심 측정이다. 카메라 객체의 지역 변환은 항등이고
    // 이미지 전수 조사에서도 그 칸에 쓰는 코드가 0곳이었다. 진짜
    // 좌표는 컴포넌트 +0x360 이고, 실행 중인 게임에서 매 프레임
    // 변하는 것을 외부 도구로 확인했다. 그 값을 쓰는 함수가 카메라
    // 컨트롤러이고, 그 함수를 알아야 회전도 같이 찾을 수 있다.
    //
    // 대조군(+0xA4)을 같은 창에 넣는다. 답을 이미 안다(0x140A59516).
    // 대조군이 잡히는데 나머지가 0이면 그것은 진짜 "안 쓴다"이고,
    // 대조군까지 0이면 감시 자체가 죽은 것이다.
    {
        log::infof("=== 감시 1: 카메라 월드 좌표를 쓰는 코드 ===");
        mem::WriteWatch watch;
        watch.add(set.active + 0xA4, 4, "대조군 +0xA4 (기대 0x140A59516)");
        watch.add(set.player_component + component_offset::kWorldPosition, 4,
                  "월드좌표X 컴포넌트+0x360");
        watch.add(set.player_component + component_offset::kWorldPosition + 8, 4,
                  "월드좌표Z 컴포넌트+0x368");
        watch.add(set.active + camera_offset::kFov, 4, "FOV +0x9C");

        // FOV 쓰기 검증. 지난 실행에서 "히트 0인데 값이 바뀌었다"는
        // 모순이 나왔는데, 쓰기가 애초에 됐는지를 안 남겨서 해석이
        // 불가능했다. 이번엔 세 가지를 다 남긴다.
        const auto fov_addr = set.active + camera_offset::kFov;
        float before = 0.0f, poked = 0.0f;
        const bool got = mem::safe_read_float(fov_addr, &before);
        const bool wrote = got && mem::safe_write_float(fov_addr, before + 7.0f);
        const bool reread = mem::safe_read_float(fov_addr, &poked);
        log::infof("  FOV 쓰기 검증: 읽기={} 쓰기={} 재읽기={}", got, wrote,
                   reread);
        log::infof("  FOV 원래 {:.4g} -> 써넣은 값 {:.4g} -> 즉시 읽기 {:.4g}  [{}]",
                   before, before + 7.0f, poked,
                   (wrote && poked == before + 7.0f) ? "쓰기 반영됨"
                                                     : "쓰기가 반영되지 않았다");

        observe(watch, 12);

        float after = 0.0f;
        mem::safe_read_float(fov_addr, &after);
        log::infof("  FOV 12초 뒤 {:.4g}  [{}]", after,
                   after == before + 7.0f ? "우리 값 유지 - 게임이 안 쓴다"
                                          : "게임이 덮어썼다");
        if (got) mem::safe_write_float(fov_addr, before);
    }

    // --- 3단계: 카메라를 누가 갈아끼우고, 프리캠은 살아 있는가 ---
    //
    // +0x35C 는 좌표와 함께 매 프레임 변하는 것을 실측했다. 좌표와
    // 같은 함수가 쓰는지 보면 컨트롤러의 범위를 알 수 있다.
    // 프리캠 +0xBC(활성 플래그)는 활성 카메라에서 0x0101, 프리캠에서
    // 0 이다. 누가 그것을 세우는지가 곧 활성화 경로다.
    {
        log::infof("=== 감시 2: 전환 슬롯과 프리캠 활성 플래그 ===");
        mem::WriteWatch watch;
        if (set.player_component != 0) {
            watch.add(set.player_component + component_offset::kActiveCamera, 8,
                      "활성 슬롯 컴포넌트+0x88");
            watch.add(set.player_component + 0x35C, 4, "컴포넌트+0x35C (같이 변함)");
        }
        if (set.free_cam != 0) {
            watch.add(set.free_cam + camera_offset::kActiveFlags, 4,
                      "프리캠 활성 플래그 +0xBC");
            watch.add(set.free_cam + camera_offset::kFov, 4, "프리캠 FOV +0x9C");
        }
        observe(watch, 12);
    }

    // --- 4단계: 활성화 실험. 프로세스를 죽일 수 있으므로 맨 뒤. ---
    //
    // 컴포넌트+0x88 은 활성 카메라의 +0x28 을 가리킨다. 여기에
    // 프리카메라를 넣으면 렌더가 프리카메라를 쓰는지 본다.
    // 3초 뒤 무조건 되돌린다.
    if (set.free_cam == 0 || set.player_component == 0) return;

    const auto slot = set.player_component + component_offset::kActiveCamera;
    std::uint64_t original = 0;
    if (!mem::safe_read_bytes(slot, &original, sizeof(original))) {
        log::errorf("=== 활성화 실험: 슬롯을 읽지 못해 건너뛴다 ===");
        return;
    }

    const std::uint64_t want = set.free_cam + 0x28;
    log::infof("=== 활성화 실험: 컴포넌트+0x88 을 프리카메라로 바꾼다 ===");
    log::infof("  원래 0x{:X} -> 0x{:X} (프리캠+0x28), 3초 뒤 되돌린다", original,
               want);

    if (!mem::safe_write_bytes(slot, &want, sizeof(want))) {
        log::errorf("  쓰기 실패 - 실험 중단");
        return;
    }

    // 게임의 파라미터 복사 루틴(0x140A59230)은 [컴포넌트+0x88]+0x7C,
    // 즉 카메라 +0xA4 에 매 프레임 쓴다. 슬롯을 바꾼 뒤 프리캠의
    // +0xA4 가 변하면 그 루틴이 프리캠을 대상으로 돌고 있다는 뜻이다.
    // 활성 플래그 +0xBC 도 같이 본다.
    float before_a4 = 0.0f, after_a4 = 0.0f;
    std::uint32_t before_flags = 0, after_flags = 0;
    mem::safe_read_float(set.free_cam + 0xA4, &before_a4);
    mem::safe_read_bytes(set.free_cam + camera_offset::kActiveFlags, &before_flags,
                         4);
    ::Sleep(3000);
    mem::safe_read_float(set.free_cam + 0xA4, &after_a4);
    mem::safe_read_bytes(set.free_cam + camera_offset::kActiveFlags, &after_flags,
                         4);

    std::uint64_t now = 0;
    mem::safe_read_bytes(slot, &now, sizeof(now));
    log::infof("  3초 뒤 슬롯 = 0x{:X} [{}]", now,
               now == want ? "우리 값 유지" : "게임이 되돌렸다");
    log::infof("  프리캠 +0xA4 {:.4g} -> {:.4g} [{}]", before_a4, after_a4,
               before_a4 != after_a4 ? "갱신됨 - 복사 루틴이 프리캠을 대상으로 돈다"
                                     : "그대로 - 여전히 비활성");
    log::infof("  프리캠 +0xBC 플래그 {:08X} -> {:08X}", before_flags, after_flags);

    mem::safe_write_bytes(slot, &original, sizeof(original));
    log::infof("  원상 복구 완료");
}

}  // namespace cdtb::game
