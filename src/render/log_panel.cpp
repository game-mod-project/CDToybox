#include "render/log_panel.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/log.h"
#include "render/colors.h"
#include "render/layout.h"
#include "render/log_filter.h"
#include "render/log_view.h"

namespace cdtb::render {
namespace {

// 받은 줄과 색인. 색인을 옮기는 산술은 전부 log_view 에 있다(시험이 덮는다).
LogView g_view;
std::uint64_t g_total = 0;   // 지금까지 쓴 줄 수(머리글에 낸다)

LogFilter g_filter;
bool g_paused = false;
bool g_follow = true;   // 자동 스크롤

ImVec4 level_color(log::Level l) {
    switch (l) {
        case log::Level::Warn: return col::kWarn;
        case log::Level::Error: return col::kBad;
        default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

const char* level_tag(log::Level l) {
    switch (l) {
        case log::Level::Warn: return "WARN ";
        case log::Level::Error: return "ERROR";
        default: return "INFO ";
    }
}

void pull_new_lines() {
    const std::size_t before = g_view.lines.size();
    // **잠금을 프레임당 한 번만 잡는다.** log::write 는 잠금을 쥔 채 줄마다 디스크
    // flush 를 하므로(크래시 진단을 위한 의도된 설계다), 렌더 스레드가 프레임마다
    // 두 번 잡으면 로그가 몰아칠 때 프레임이 튄다. 전체 줄 수도 같이 받아 온다.
    const std::uint64_t missed =
        log::ring_since(g_view.seen, &g_view.lines, &g_total);
    log_view_absorb(g_view, before, missed, g_filter);
}

void copy_visible_to_clipboard() {
    std::string all;
    all.reserve(g_view.view.size() * 80);
    for (const int i : g_view.view) {
        const log::RingLine& l = g_view.lines[static_cast<std::size_t>(i)];
        all += '[';
        all += l.time;
        all += "] ";
        all += level_tag(l.level);
        all += ' ';
        all += l.text;
        all += '\n';
    }
    ImGui::SetClipboardText(all.c_str());
}

}  // namespace

void draw_log_panel(bool* open) {
    if (!begin_window(Win::Log, open)) {
        ImGui::End();
        return;
    }

    // 멈춤이면 새 줄을 아예 안 받는다. 읽는 도중에 목록이 밀리지 않게 하는 것이
    // 이 칸의 전부다 - 파일에는 그대로 쌓이고, 풀면 그 사이 것도 따라온다
    // (고리가 버티는 2000줄까지).
    if (!g_paused) pull_new_lines();

    bool refilter = false;
    refilter |= ImGui::Checkbox("정보", &g_filter.info);
    ImGui::SameLine();
    refilter |= ImGui::Checkbox("경고", &g_filter.warn);
    ImGui::SameLine();
    refilter |= ImGui::Checkbox("오류", &g_filter.error);
    ImGui::SameLine();
    ImGui::Checkbox("멈춤", &g_paused);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "새 줄을 받지 않습니다. 로그 파일에는 그대로 쌓이고,\n"
            "풀면 그 사이 것도 따라옵니다(최근 2000줄까지).");
    }
    ImGui::SameLine();
    ImGui::Checkbox("자동 스크롤", &g_follow);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "위로 올리면 저절로 꺼집니다 - 휠·스크롤바·키 어느 쪽이든.");
    }

    ImGui::SetNextItemWidth(-160.0f);
    refilter |= ImGui::InputTextWithHint("##검색", "글자로 거르기",
                                         g_filter.query,
                                         sizeof(g_filter.query));
    ImGui::SameLine();
    if (ImGui::SmallButton("지우기")) log_view_clear(g_view);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("화면만 비웁니다. 로그 파일은 그대로입니다.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("복사")) copy_visible_to_clipboard();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("지금 보이는 줄을 클립보드로 옮깁니다.");
    }

    if (refilter) log_view_rebuild(g_view, g_filter);

    ImGui::TextDisabled("보이는 줄 %d / 받은 줄 %d / 전체 %llu",
                        static_cast<int>(g_view.view.size()),
                        static_cast<int>(g_view.lines.size()),
                        static_cast<unsigned long long>(g_total));
    if (g_view.missed > 0) {
        ImGui::SameLine();
        // 중간이 비었다는 사실을 말하지 않으면, 사용자가 로그를 그대로 믿는다.
        ImGui::TextColored(col::kWarn, "(놓친 줄 %llu)",
                           static_cast<unsigned long long>(g_view.missed));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "창을 연 뒤로 줄이 너무 빨리 쌓여 그 사이가 덮였습니다.\n"
                "빠진 줄은 로그 파일(bin64/CDToybox.log)에 그대로 있습니다.");
        }
    }

    ImGui::Separator();
    // 가로 스크롤을 켠다. 긴 줄(주소·스캔 결과)을 창 폭에 맞춰 접으면 표처럼
    // 읽던 것이 무너진다.
    if (ImGui::BeginChild("##본문", ImVec2(0.0f, 0.0f), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // **줄을 그리기 전에** 지금 바닥인지 본다. 이 값은 직전 프레임의 내용
        // 높이로 계산돼 있어 클리퍼와도 맞는다.
        //
        // 예전에는 "휠을 위로 굴렸는가" 로만 자동 스크롤을 놓아 주었다. 그러면
        // 스크롤바 손잡이를 끌어 올려도 손을 떼는 순간 SetScrollHereY 가 바닥으로
        // 끌어내려, 사용자에게는 스크롤바가 고장난 것으로 보인다 - 트랙 클릭·
        // PageUp·Home·터치패드 두 손가락도 전부 무효였다. "바닥에서 떨어졌는가"
        // 로 보면 그 길이 한꺼번에 풀린다(리뷰 중대 1).
        const bool at_bottom =
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
        if (g_follow && !at_bottom) g_follow = false;

        // 보이는 줄만 그린다. 수천 줄을 매 프레임 다 그리면 프레임이 무너진다.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_view.view.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const log::RingLine& l =
                    g_view.lines[static_cast<std::size_t>(
                        g_view.view[static_cast<std::size_t>(row)])];
                ImGui::PushStyleColor(ImGuiCol_Text, level_color(l.level));
                ImGui::Text("[%s] %s %s", l.time.c_str(), level_tag(l.level),
                            l.text.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (g_follow) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace cdtb::render
