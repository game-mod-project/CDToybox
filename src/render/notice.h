#pragma once

#include <format>
#include <string>
#include <utility>

#include "render/notice_state.h"

namespace cdtb::render {

// 등급 색으로 그린다. Faded 면 회색, Gone 이면 아무것도 안 그린다.
void notice_draw(const Notice& n);

// now = ImGui::GetTime()
void notice_put_now(Notice* n, NoticeLevel lv, const char* text);

template <class... A>
void notice_set(Notice* n, NoticeLevel lv, std::format_string<A...> f,
                A&&... a) {
    notice_put_now(n, lv, std::format(f, std::forward<A>(a)...).c_str());
}

}  // namespace cdtb::render
