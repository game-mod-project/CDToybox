#include "core/vk_name.h"

#include <cstdio>

namespace cdtb {

const char* vk_name(int vk, char* buf, std::size_t n) {
    switch (vk) {
        case 0x08: return "Backspace";
        case 0x09: return "Tab";
        case 0x0D: return "Enter";
        case 0x10: return "Shift";
        case 0x11: return "Ctrl";
        case 0x12: return "Alt";
        case 0x13: return "Pause";
        case 0x14: return "CapsLock";
        case 0x1B: return "Esc";
        case 0x20: return "Space";
        case 0x21: return "PgUp";
        case 0x22: return "PgDn";
        case 0x23: return "End";
        case 0x24: return "Home";
        case 0x25: return "Left";
        case 0x26: return "Up";
        case 0x27: return "Right";
        case 0x28: return "Down";
        case 0x2C: return "PrintScreen";
        case 0x2D: return "Insert";
        case 0x2E: return "Delete";
        case 0x90: return "NumLock";
        case 0x91: return "ScrollLock";
        default: break;
    }
    static const char* const kDigits[10] = {"0", "1", "2", "3", "4",
                                            "5", "6", "7", "8", "9"};
    static const char* const kLetters[26] = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
    static const char* const kNum[10] = {"Num0", "Num1", "Num2", "Num3", "Num4",
                                         "Num5", "Num6", "Num7", "Num8", "Num9"};
    static const char* const kFn[24] = {
        "F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",  "F8",
        "F9",  "F10", "F11", "F12", "F13", "F14", "F15", "F16",
        "F17", "F18", "F19", "F20", "F21", "F22", "F23", "F24"};
    if (vk >= 0x30 && vk <= 0x39) return kDigits[vk - 0x30];
    if (vk >= 0x41 && vk <= 0x5A) return kLetters[vk - 0x41];
    if (vk >= 0x60 && vk <= 0x69) return kNum[vk - 0x60];
    if (vk >= 0x70 && vk <= 0x87) return kFn[vk - 0x70];
    std::snprintf(buf, n, "0x%02X", vk);
    return buf;
}

}  // namespace cdtb
