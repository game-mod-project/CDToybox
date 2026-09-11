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
    // 해체 경로(보관함 큐 접기·flush 실패)가 컨텍스트가 파괴된 뒤에 올 수 있다 -
    // GetTime 은 널 검사가 없다. 그때의 알림은 어차피 다음 컨텍스트에서 Gone 이다.
    if (ImGui::GetCurrentContext() == nullptr) return;
    notice_put(n, lv, ImGui::GetTime(), text);
}

}  // namespace cdtb::render
