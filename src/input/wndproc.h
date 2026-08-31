#pragma once

#include <windows.h>

namespace cdtb::input {

void install(HWND hwnd);
void remove();
bool is_installed();

}  // namespace cdtb::input
