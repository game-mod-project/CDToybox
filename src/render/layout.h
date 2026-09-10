#pragma once

#include "render/layout_table.h"

namespace cdtb::render {

// SetNextWindowPos/Size(FirstUseEver) + SetNextWindowSizeConstraints + Begin.
// 반환값과 open 은 ImGui::Begin 과 같다 - false 여도 End 는 불러야 한다.
bool begin_window(Win w, bool* open);

}  // namespace cdtb::render
