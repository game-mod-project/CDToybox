#include <string>

#include <imgui.h>
#include <imgui_internal.h>

#include "harness.h"
#include "render/table_order.h"

namespace {

// 오버레이의 정렬 표와 같은 꼴: 정렬만 되고 재배치 · 크기 · 숨김은 없다.
constexpr ImGuiTableFlags kSortOnly = ImGuiTableFlags_Sortable;

struct Session {
    std::string order;                 // 표시 차례의 열 첨자, 예 "0 1 2"
    int sort_col = -1;
    int sort_dir = ImGuiSortDirection_None;
    std::string ini;                   // 세션을 닫을 때 저장된 ini
};

// 헤드리스 ImGui 한 세션. ini 를 넣고 3열 표를 네 프레임 그린다. set_sort_col >= 0 이면
// 둘째 프레임에 그 열을 내림차순으로 정렬한다.
Session run(const std::string& ini_in, ImGuiTableFlags flags, bool keep, int set_sort_col) {
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

    Session s;
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(600, 400));
        ImGui::Begin("W");
        if (ImGui::BeginTable("t", 3, flags)) {
            if (keep) cdtb::render::table_keep_natural_order();
            ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_DefaultSort);
            ImGui::TableSetupColumn("b");
            ImGui::TableSetupColumn("c");
            ImGui::TableHeadersRow();
            if (frame == 1 && set_sort_col >= 0) {
                ImGui::TableSetColumnSortDirection(set_sort_col, ImGuiSortDirection_Descending,
                                                   false);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("x");
            if (frame == 3) {
                ImGuiTable* t = ImGui::GetCurrentTable();
                for (int d = 0; d < t->ColumnsCount; ++d) {
                    if (d > 0) s.order += " ";
                    s.order += std::to_string(t->DisplayOrderToIndex[d]);
                }
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
