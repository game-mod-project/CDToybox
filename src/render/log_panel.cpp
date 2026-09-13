#include "render/log_panel.h"

#include <imgui.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/log.h"
#include "render/colors.h"
#include "render/layout.h"
#include "render/log_filter.h"

namespace cdtb::render {
namespace {

// 창이 들고 있는 사본. 고리에서 **새 줄만** 받는다 - 매 프레임 2000줄을 복사하면
// 그리는 값보다 옮기는 값이 커진다.
std::vector<log::RingLine> g_lines;
std::uint64_t g_seen = 0;      // 마지막으로 받은 일련번호
std::uint64_t g_missed = 0;    // 덮여서 사라진 줄 수(누적)

// 거르개를 통과한 g_lines 색인. 매 프레임 전체를 다시 거르지 않으려고 들고 있는다.
// 새 줄이 붙으면 그것만 이어 붙이고, 거르개가 바뀔 때만 통째로 다시 만든다.
std::vector<int> g_view;

LogFilter g_filter;
bool g_paused = false;
bool g_follow = true;   // 자동 스크롤

// 창이 들고 있을 줄 수의 상한. 고리(2000)보다 넉넉히 잡아, 창을 열어 둔 동안에는
// 고리가 버린 뒤에도 한동안 볼 수 있게 한다.
constexpr std::size_t kViewMax = 5000;
// 상한에 닿을 때마다 한 줄씩 지우면 매번 색인을 다시 만들게 된다. 뭉텅이로 자른다.
constexpr std::size_t kTrimChunk = 1000;

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

void rebuild_view() {
    g_view.clear();
    g_view.reserve(g_lines.size());
    for (std::size_t i = 0; i < g_lines.size(); ++i) {
        if (log_line_matches(g_filter, g_lines[i].level, g_lines[i].text)) {
            g_view.push_back(static_cast<int>(i));
        }
    }
}

// 앞쪽을 잘라 낸다. 색인이 통째로 밀리므로 살아남은 것만 당겨 온다 - 여기서
// 안 맞추면 스크롤이 엉뚱한 줄을 그린다.
void trim_front() {
    if (g_lines.size() <= kViewMax) return;
    const std::size_t drop = g_lines.size() - kViewMax + kTrimChunk;
    g_lines.erase(g_lines.begin(),
                  g_lines.begin() + static_cast<std::ptrdiff_t>(drop));
    std::vector<int> kept;
    kept.reserve(g_view.size());
    for (const int i : g_view) {
        if (static_cast<std::size_t>(i) >= drop) {
            kept.push_back(i - static_cast<int>(drop));
        }
    }
    g_view.swap(kept);
}

void pull_new_lines() {
    const std::size_t before = g_lines.size();
    g_missed += log::ring_since(g_seen, &g_lines);
    if (g_lines.size() == before) return;
    g_seen = g_lines.back().seq;
    for (std::size_t i = before; i < g_lines.size(); ++i) {
        if (log_line_matches(g_filter, g_lines[i].level, g_lines[i].text)) {
            g_view.push_back(static_cast<int>(i));
        }
    }
    trim_front();
}

void copy_visible_to_clipboard() {
    std::string all;
    all.reserve(g_view.size() * 80);
    for (const int i : g_view) {
        const log::RingLine& l = g_lines[static_cast<std::size_t>(i)];
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

    ImGui::SetNextItemWidth(-160.0f);
    refilter |= ImGui::InputTextWithHint("##검색", "글자로 거르기",
                                         g_filter.query,
                                         sizeof(g_filter.query));
    ImGui::SameLine();
    if (ImGui::SmallButton("지우기")) {
        g_lines.clear();
        g_view.clear();
        g_missed = 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("화면만 비웁니다. 로그 파일은 그대로입니다.");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("복사")) copy_visible_to_clipboard();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("지금 보이는 줄을 클립보드로 옮깁니다.");
    }

    if (refilter) rebuild_view();

    ImGui::TextDisabled("보이는 줄 %d / 받은 줄 %d / 전체 %llu",
                        static_cast<int>(g_view.size()),
                        static_cast<int>(g_lines.size()),
                        static_cast<unsigned long long>(log::ring_count()));
    if (g_missed > 0) {
        ImGui::SameLine();
        // 중간이 비었다는 사실을 말하지 않으면, 사용자가 로그를 그대로 믿는다.
        ImGui::TextColored(col::kWarn, "(놓친 줄 %llu)",
                           static_cast<unsigned long long>(g_missed));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "창을 닫아 두었거나 줄이 너무 빨리 쌓여 그 사이가 덮였습니다.\n"
                "빠진 줄은 로그 파일(bin64/CDToybox.log)에 그대로 있습니다.");
        }
    }

    ImGui::Separator();
    // 가로 스크롤을 켠다. 긴 줄(주소·스캔 결과)을 창 폭에 맞춰 접으면 표처럼
    // 읽던 것이 무너진다.
    if (ImGui::BeginChild("##본문", ImVec2(0.0f, 0.0f), false,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        // 사용자가 위로 굴리면 따라가기를 놓아 준다. 안 그러면 읽으려 할 때마다
        // 바닥으로 끌려가고, 왜 그런지는 화면에 안 적혀 있다.
        if (g_follow && ImGui::IsWindowHovered() &&
            ImGui::GetIO().MouseWheel > 0.0f) {
            g_follow = false;
        }
        // 보이는 줄만 그린다. 수천 줄을 매 프레임 다 그리면 프레임이 무너진다.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(g_view.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd;
                 ++row) {
                const log::RingLine& l =
                    g_lines[static_cast<std::size_t>(
                        g_view[static_cast<std::size_t>(row)])];
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
