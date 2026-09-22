#include <cmath>
#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "harness.h"
#include "render/table_order.h"

namespace {

// 오버레이의 정렬 표와 같은 꼴: 정렬만 되고 재배치 · 크기 · 숨김은 없다.
constexpr ImGuiTableFlags kSortOnly = ImGuiTableFlags_Sortable;
// 아이템 목록 · 인벤토리 표의 꼴: 정렬 + 너비 조절(Resizable).
constexpr ImGuiTableFlags kSortResize = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable;

struct Session {
    std::string order;                 // 표시 차례의 열 첨자, 예 "0 1 2"
    int sort_col = -1;
    int sort_dir = ImGuiSortDirection_None;
    float width = 0.0f;                // 가운데 열의 실제 너비
    std::string ini;                   // 세션을 닫을 때 저장된 ini
};

// 헤드리스 ImGui 한 세션. ini 를 넣고 3열 표를 네 프레임 그린다. set_sort_col >= 0 이면
// 둘째 프레임에 그 열을 내림차순으로 정렬하고, set_width_col >= 0 이면 그 열의 너비를
// set_width 로 바꾼다(사용자가 경계를 끄는 것과 같은 경로 - TableSetColumnWidth).
Session run(const std::string& ini_in, ImGuiTableFlags flags, bool keep, int set_sort_col,
            int set_width_col = -1, float set_width = 0.0f) {
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->AddFontDefault();
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    if (!ini_in.empty()) ImGui::LoadIniSettingsFromMemory(ini_in.c_str());

    // 표는 처음 두세 프레임 동안 자동 맞춤(AutoFitQueue)을 돌며 너비를 스스로 정한다.
    // 그동안 넣은 너비는 지워지므로, 너비는 frame 3 에 넣고 마지막 프레임에서 읽는다.
    constexpr int kFrames = 6;
    constexpr int kWidthFrame = 3;
    Session s;
    for (int frame = 0; frame < kFrames; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(600, 400));
        ImGui::Begin("W");
        if (ImGui::BeginTable("t", 3, flags)) {
            if (keep) cdtb::render::table_keep_natural_order();
            ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("b");
            ImGui::TableSetupColumn("c");
            // 너비는 배치가 잠기기 전에 바꾼다(TableSetColumnWidth 의 단언).
            if (frame == kWidthFrame && set_width_col >= 0) {
                ImGui::TableSetColumnWidth(set_width_col, set_width);
            }
            ImGui::TableHeadersRow();
            if (frame == 1 && set_sort_col >= 0) {
                ImGui::TableSetColumnSortDirection(set_sort_col, ImGuiSortDirection_Descending,
                                                   false);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("x");
            if (frame == kFrames - 1) {
                ImGuiTable* t = ImGui::GetCurrentTable();
                for (int d = 0; d < t->ColumnsCount; ++d) {
                    if (d > 0) s.order += " ";
                    s.order += std::to_string(t->DisplayOrderToIndex[d]);
                }
                s.width = t->Columns[1].WidthGiven;
                if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs()) {
                    if (ss->SpecsCount > 0) {
                        s.sort_col = ss->Specs[0].ColumnIndex;
                        s.sort_dir = ss->Specs[0].SortDirection;
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::End();
        ImGui::Render();
    }
    s.ini = ImGui::SaveIniSettingsToMemory();
    ImGui::DestroyContext(ctx);
    return s;
}

}  // namespace

TEST(table_sort_only_reload_puts_the_sorted_column_first_in_imgui_1_92_9b) {
    // ImGui 1.92.9b 의 결함을 못박는 특성 시험(상류 ocornut/imgui#9519). 상류가 고쳐 이
    // 시험이 깨지면 table_keep_natural_order 를 걷어낼지 볼 때가 된 것이다.
    const Session first = run("", kSortOnly, false, 2);
    const Session again = run(first.ini, kSortOnly, false, -1);
    CHECK_EQ(again.order, std::string("2 0 1"));
}

TEST(table_keep_natural_order_restores_the_order_and_keeps_the_saved_sort) {
    const Session first = run("", kSortOnly, true, 2);
    const Session again = run(first.ini, kSortOnly, true, -1);
    CHECK_EQ(again.order, std::string("0 1 2"));
    CHECK_EQ(again.sort_col, 2);
    CHECK_EQ(again.sort_dir, static_cast<int>(ImGuiSortDirection_Descending));
    // 한 번 더 켜도 같다.
    const Session third = run(again.ini, kSortOnly, true, -1);
    CHECK_EQ(third.order, std::string("0 1 2"));
    CHECK_EQ(third.sort_col, 2);
}

TEST(table_keep_natural_order_leaves_reorderable_tables_alone) {
    // 사용자가 옮길 수 있는 표는 옮긴 순서가 저장값이다 - 되돌림을 걸면 안 된다.
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    unsigned char* px = nullptr;
    int w = 0, h = 0;
    io.Fonts->AddFontDefault();
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
    ImGui::NewFrame();
    ImGui::Begin("W");
    bool requested = true;
    if (ImGui::BeginTable("r", 3, kSortOnly | ImGuiTableFlags_Reorderable)) {
        cdtb::render::table_keep_natural_order();
        requested = ImGui::GetCurrentTable()->IsResetDisplayOrderRequest;
        ImGui::TableSetupColumn("a");
        ImGui::TableSetupColumn("b");
        ImGui::TableSetupColumn("c");
        ImGui::TableHeadersRow();
        ImGui::EndTable();
    }
    ImGui::End();
    ImGui::Render();
    ImGui::DestroyContext(ctx);
    CHECK(!requested);
}

TEST(table_resizable_keeps_saved_column_widths_across_sessions) {
    // 열 너비 조절(Resizable)과 차례 되돌림(table_keep_natural_order)이 함께 있어도
    // 저장된 너비가 살아남아야 한다 - 되돌림이 너비까지 지우면 매번 기본값으로 돌아간다.
    const Session base = run("", kSortResize, true, -1);
    const Session wide = run("", kSortResize, true, -1, 1, 250.0f);
    CHECK(wide.width > base.width + 1.0f);
    const Session again = run(wide.ini, kSortResize, true, -1);
    CHECK(std::fabs(again.width - wide.width) < 1.0f);
    CHECK_EQ(again.order, std::string("0 1 2"));
}
