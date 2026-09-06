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
    // Type 도 남긴다. MEM_MAPPED(0x40000) 면 같은 물리 페이지가
    // 다른 가상 주소로도 매핑돼 있을 수 있고, 그 별칭 주소로
    // 쓰면 이 주소에 건 하드웨어 브레이크포인트는 안 걸린다.
    log::infof("{} 0x{:X}: protect=0x{:X} state=0x{:X} type=0x{:X} {}",
               what, addr, mbi.Protect, mbi.State, mbi.Type,
               writable ? "쓰기 가능" : "쓰기 불가");
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

    // --- 2단계는 뺐다: 하드웨어 브레이크포인트는 이 게임에서 못 쓴다 ---
    //
    // 보호 코드가 250ms 마다 121개 스레드 중 47개가량의 디버그
    // 레지스터를 지운다(실측: 한 창에서 2274회 다시 걸었다). 어떤
    // 값은 잡히고 어떤 값은 영원히 히트 0으로 보여, 결과를 신뢰할 수
    // 없다. 게다가 감시 창마다 24초씩 전 스레드를 정지시킨다.
    //
    // 찾으려던 답은 정적 분석으로 얻었다 - 카메라 위치 갱신 함수는
    // 0x140A58540 이고 현재 = lerp(현재, 목표, t) 이다. 앞서 못 찾은
    // 것은 내 패턴 스캐너가 VEX 접두사 바이트 순서를 뒤집어 써서
    // AVX 저장을 전부 놓쳤기 때문이었다.
    //
    // WriteWatch 클래스와 테스트는 남겨 둔다. 보호가 없는 대상에는
    // 여전히 유효한 도구다.
    log::infof("자동 분석 완료 - 프리카메라는 F9");
}

}  // namespace cdtb::game
