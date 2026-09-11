#include "render/layout.h"

#include <imgui.h>

#include <cfloat>

namespace cdtb::render {

bool begin_window(Win w, bool* open) {
    const WindowSpec& s = window_spec(w);
    ImGui::SetNextWindowPos(ImVec2(s.x, s.y), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(s.w, s.h), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(s.min_w, s.min_h),
                                        ImVec2(FLT_MAX, FLT_MAX));
    return ImGui::Begin(s.title, open);
}

}  // namespace cdtb::render
