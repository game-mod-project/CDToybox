#include "render/notice.h"

#include <imgui.h>

#include "render/colors.h"

namespace cdtb::render {

void notice_draw(const Notice& n) {
    switch (notice_age(n, ImGui::GetTime())) {
        case NoticeAge::Gone:
            return;
        case NoticeAge::Faded:
            ImGui::TextDisabled("%s", n.text);
            return;
        case NoticeAge::Fresh:
            break;
    }
    ImVec4 c = col::kBusy;
    switch (n.level) {
        case NoticeLevel::Ok: c = col::kOk; break;
        case NoticeLevel::Warn: c = col::kWarn; break;
        case NoticeLevel::Bad: c = col::kBad; break;
        case NoticeLevel::Info: c = col::kBusy; break;
    }
    ImGui::TextColored(c, "%s", n.text);
}

void notice_put_now(Notice* n, NoticeLevel lv, const char* text) {
    notice_put(n, lv, ImGui::GetTime(), text);
}

}  // namespace cdtb::render
