#include "render/scan_panel.h"

#include <windows.h>

#include <imgui.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <string>
#include <vector>

#include "core/guard.h"
#include "core/log.h"
#include "game/camera.h"
#include "mem/regions.h"
#include "mem/safe_read.h"
#include "mem/value_scanner.h"
#include "mem/watchpoint.h"

namespace cdtb::render {
namespace {

mem::FloatScan g_scan;

// 최초 스캔은 수 GB를 훑을 수 있다. 렌더 스레드에서 돌리면 게임이
// 멈추므로 워커로 보내고 진행률만 읽는다.
std::thread g_worker;
std::atomic<bool> g_busy{false};
std::atomic<std::uint64_t> g_total{0};

float g_target = 0.0f;
float g_eps = 0.001f;

// 관찰 고정. 후보 하나를 골라 실시간으로 값을 본다.
std::uintptr_t g_pinned = 0;
float g_write_value = 0.0f;

// 카메라 RTTI 탐색도 수 초가 걸린다. 렌더 스레드를 막지 않는다.
std::thread g_cam_worker;
std::atomic<bool> g_cam_busy{false};

// 카메라 값을 쓰는 코드를 찾기 위한 하드웨어 브레이크포인트.
mem::WriteWatch g_watch;

void start_discovery() {
    if (g_cam_busy.load()) return;
    if (g_cam_worker.joinable()) g_cam_worker.join();
    g_cam_busy.store(true);
    g_cam_worker = std::thread([]() {
        game::discover(nullptr);
        g_cam_busy.store(false);
    });
}

void start_first_scan() {
    if (g_busy.load()) return;
    if (g_worker.joinable()) g_worker.join();

    g_busy.store(true);
    const float target = g_target;
    const float eps = g_eps;
    g_worker = std::thread([target, eps]() {
        const auto regions = mem::writable_regions();
        g_total.store(mem::total_bytes(regions));
        const std::size_t n = g_scan.first(regions, target, eps);
        log::infof("첫 스캔: {} 영역, {} MB, 후보 {}개{}", regions.size(),
                   g_total.load() / (1024 * 1024), n,
                   g_scan.capped() ? " (상한 도달)" : "");
        g_busy.store(false);
    });
}

void draw_results() {
    ImGui::Text("후보 %zu개%s", g_scan.count(),
                g_scan.capped() ? "  [상한 도달 - 더 특징적인 값을 쓰세요]"
                                : "");
    if (g_scan.count() == 0) return;

    ImGui::BeginChild("results", ImVec2(0, 180), true);
    const std::size_t shown = g_scan.count() < 50 ? g_scan.count() : 50;
    for (std::size_t i = 0; i < shown; ++i) {
        const std::uintptr_t a = g_scan.results()[i];
        float v = 0.0f;
        const bool ok = mem::safe_read_float(a, &v);

        char label[64];
        std::snprintf(label, sizeof(label), "0x%llX",
                      static_cast<unsigned long long>(a));
        if (ImGui::Selectable(label, g_pinned == a)) g_pinned = a;
        ImGui::SameLine(200);
        if (ok) {
            ImGui::Text("%.6f", v);
        } else {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "읽기 실패");
        }
    }
    if (g_scan.count() > shown) {
        ImGui::TextDisabled("... %zu개 더", g_scan.count() - shown);
    }
    ImGui::EndChild();
}

void draw_pinned() {
    if (g_pinned == 0) return;
    ImGui::Separator();
    ImGui::Text("고정: 0x%llX", static_cast<unsigned long long>(g_pinned));

    float v = 0.0f;
    if (mem::safe_read_float(g_pinned, &v)) {
        ImGui::Text("현재값: %.6f", v);
    } else {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "읽을 수 없음");
    }

    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("쓸 값", &g_write_value);
    ImGui::SameLine();
    if (ImGui::Button("쓰기")) {
        const bool ok = mem::safe_write_float(g_pinned, g_write_value);
        log::infof("쓰기 0x{:X} <- {} : {}",
                   static_cast<unsigned long long>(g_pinned), g_write_value,
                   ok ? "성공" : "실패");
    }

    // 고정 주소 주변을 float 격자로 본다. 구조체 필드를 눈으로
    // 찾을 때 쓴다.
    if (ImGui::CollapsingHeader("주변 덤프 (float)")) {
        for (int row = -4; row < 8; ++row) {
            const std::uintptr_t rowaddr =
                g_pinned + static_cast<std::intptr_t>(row) * 16;
            ImGui::Text("%+5d", row * 16);
            for (int col = 0; col < 4; ++col) {
                float f = 0.0f;
                const std::uintptr_t a = rowaddr + col * 4;
                ImGui::SameLine(60.0f + col * 110.0f);
                if (mem::safe_read_float(a, &f)) {
                    if (a == g_pinned) {
                        ImGui::TextColored(ImVec4(1, 1, 0.4f, 1), "%.4f", f);
                    } else {
                        ImGui::Text("%.4f", f);
                    }
                } else {
                    ImGui::TextDisabled("----");
                }
            }
        }
    }
}

}  // namespace

void draw_scan_panel() {
    ImGui::SetNextWindowSize(ImVec2(620, 620), ImGuiCond_FirstUseEver);
    ImGui::Begin("메모리 스캔");

    // 다음에 무엇을 해야 하는지 항상 한 줄로 말한다.
    if (g_scan.count() == 0 && !g_busy.load()) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1),
                           "1) FOV 찾기: 찾을 값 60 / 오차 30 으로 첫 스캔");
    } else if (g_pinned == 0 && !g_busy.load()) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1),
                           "2) 시야각을 바꾸며 '변함', 가만히 두고 '안 변함'을 "
                           "번갈아 눌러 좁히세요");
    } else if (!g_busy.load()) {
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 1, 1),
                           "3) 카메라 창의 RTTI 탐색이 더 빠릅니다 "
                           "(값 스캔은 보조 수단입니다)");
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(140);
    ImGui::InputFloat("찾을 값", &g_target);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputFloat("오차", &g_eps, 0.0f, 0.0f, "%.5f");

    if (g_busy.load()) {
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1),
                           "스캔 중...  %llu / %llu MB",
                           g_scan.scanned_bytes() / (1024 * 1024),
                           g_total.load() / (1024 * 1024));
    } else {
        if (ImGui::Button("첫 스캔")) start_first_scan();
        ImGui::SameLine();
        if (ImGui::Button("초기화")) {
            g_scan.reset();
            g_pinned = 0;
        }

        if (g_scan.count() > 0) {
            if (ImGui::Button("같음")) g_scan.narrow_equals(g_target, g_eps);
            ImGui::SameLine();
            if (ImGui::Button("변함")) g_scan.narrow_changed();
            ImGui::SameLine();
            if (ImGui::Button("안 변함")) g_scan.narrow_unchanged();
            ImGui::SameLine();
            if (ImGui::Button("증가")) g_scan.narrow_increased();
            ImGui::SameLine();
            if (ImGui::Button("감소")) g_scan.narrow_decreased();
        }
    }

    ImGui::Separator();
    draw_results();
    draw_pinned();
    ImGui::End();
}


void draw_camera_panel() {
    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
    ImGui::Begin("카메라");

    if (g_cam_busy.load()) {
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1),
                           "RTTI로 카메라 탐색 중... (수 초)");
        ImGui::End();
        return;
    }

    if (!game::discovered()) {
        ImGui::TextWrapped(
            "카메라 객체를 RTTI로 찾습니다. 값 스캔과 달리 게임을 "
            "조작할 필요가 없습니다. 월드에 진입한 상태에서 누르세요.");
        if (ImGui::Button("카메라 탐색")) start_discovery();
        ImGui::End();
        return;
    }

    const auto& c = game::cameras();
    if (ImGui::Button("다시 탐색")) {
        start_discovery();
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("주소는 실행마다 바뀝니다");
    ImGui::Separator();

    struct Row { const char* label; std::uintptr_t addr; };
    const Row rows[] = {
        {"CameraManager", c.manager},
        {"FreeCamCamera", c.free_cam},
        {"PhotoCamera", c.photo_cam},
        {"PlayerCameraComponent", c.player_component},
        {"활성 카메라", c.active},
    };

    for (const auto& row : rows) {
        ImGui::Text("%-22s 0x%llX", row.label,
                    static_cast<unsigned long long>(row.addr));
    }

    ImGui::Separator();
    ImGui::TextDisabled("카메라 필드 (실시간)");

    const Row cams[] = {
        {"활성", c.active},
        {"프리캠", c.free_cam},
    };
    for (const auto& cam : cams) {
        if (cam.addr == 0) continue;
        float fov = 0.0f, pos[3]{}, rot[4]{};
        std::string name;
        game::read_fov(cam.addr, &fov);
        game::read_position(cam.addr, pos);
        game::read_rotation(cam.addr, rot);
        game::read_name(cam.addr, &name);

        ImGui::Text("[%s] %s", cam.label, name.c_str());
        ImGui::Indent();
        ImGui::Text("fov %8.3f   pos %9.2f %9.2f %9.2f", fov, pos[0], pos[1],
                    pos[2]);
        ImGui::Text("rot %7.4f %7.4f %7.4f %7.4f", rot[0], rot[1], rot[2],
                    rot[3]);
        ImGui::Unindent();
    }

    ImGui::Separator();
    ImGui::TextDisabled("쓰기 감시 - 어느 코드가 이 값을 바꾸는가");
    ImGui::TextWrapped(
        "게임은 카메라 값을 매 프레임 덮어씁니다. 하드웨어 브레이크포인트로 "
        "쓰는 명령의 주소를 알아내면, 그 지점을 후킹해 우리 값을 유지할 수 "
        "있습니다.");

    static int watch_target = 0;
    ImGui::RadioButton("활성 FOV", &watch_target, 0);
    ImGui::SameLine();
    ImGui::RadioButton("활성 위치", &watch_target, 1);
    ImGui::SameLine();
    ImGui::RadioButton("컴포넌트 위치", &watch_target, 2);

    std::uintptr_t addr = 0;
    switch (watch_target) {
        case 0: addr = c.active ? c.active + game::camera_offset::kFov : 0;
                break;
        case 1: addr = c.active ? c.active + game::camera_offset::kPosition : 0;
                break;
        default:
            addr = c.player_component ? c.player_component + 0x360 : 0;
            break;
    }
    ImGui::Text("감시 대상 0x%llX", static_cast<unsigned long long>(addr));

    if (!g_watch.active()) {
        if (ImGui::Button("감시 시작") && addr != 0) {
            g_watch.install(addr, 4);
        }
    } else {
        if (ImGui::Button("감시 중지")) g_watch.remove();
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.9f, 0.4f, 1), "감시 중  히트 %zu회",
                           g_watch.hit_count());
    }

    const auto hits = g_watch.hits();
    if (!hits.empty()) {
        ImGui::Text("쓰는 명령 %zu곳:", hits.size());
        ImGui::Indent();
        const auto base = reinterpret_cast<std::uintptr_t>(
            ::GetModuleHandleW(nullptr));
        for (std::size_t i = 0; i < hits.size() && i < 12; ++i) {
            ImGui::Text("0x%llX   (모듈+0x%llX)",
                        static_cast<unsigned long long>(hits[i]),
                        static_cast<unsigned long long>(hits[i] - base));
        }
        ImGui::Unindent();
    }

    ImGui::End();
}

void shutdown_scan_panel() {
    if (g_worker.joinable()) g_worker.join();
    if (g_cam_worker.joinable()) g_cam_worker.join();
    g_scan.reset();
    g_pinned = 0;
}

}  // namespace cdtb::render
