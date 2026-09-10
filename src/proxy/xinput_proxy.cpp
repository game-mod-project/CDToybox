#include "proxy/xinput_proxy.h"

#include <windows.h>
#include <xinput.h>

#include <string>

namespace {

HMODULE g_original = nullptr;

using PFN_GetState   = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using PFN_SetState   = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using PFN_GetCaps    = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);
using PFN_Enable     = void(WINAPI*)(BOOL);
using PFN_GetBattery = DWORD(WINAPI*)(DWORD, BYTE,
                                      XINPUT_BATTERY_INFORMATION*);
using PFN_GetKey     = DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE);
using PFN_GetAudio   = DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*);

PFN_GetState   o_GetState   = nullptr;
PFN_SetState   o_SetState   = nullptr;
PFN_GetCaps    o_GetCaps    = nullptr;
PFN_Enable     o_Enable     = nullptr;
PFN_GetBattery o_GetBattery = nullptr;
PFN_GetKey     o_GetKey     = nullptr;
PFN_GetAudio   o_GetAudio   = nullptr;

}  // namespace

namespace cdtb::proxy {

bool load_original() {
    wchar_t dir[MAX_PATH]{};
    const UINT n = ::GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;

    std::wstring path(dir, n);
    path += L"\\XINPUT1_4.dll";

    g_original = ::LoadLibraryW(path.c_str());
    if (g_original == nullptr) return false;

    o_GetState = reinterpret_cast<PFN_GetState>(
        ::GetProcAddress(g_original, "XInputGetState"));
    o_SetState = reinterpret_cast<PFN_SetState>(
        ::GetProcAddress(g_original, "XInputSetState"));
    o_GetCaps = reinterpret_cast<PFN_GetCaps>(
        ::GetProcAddress(g_original, "XInputGetCapabilities"));
    o_Enable = reinterpret_cast<PFN_Enable>(
        ::GetProcAddress(g_original, "XInputEnable"));
    o_GetBattery = reinterpret_cast<PFN_GetBattery>(
        ::GetProcAddress(g_original, "XInputGetBatteryInformation"));
    o_GetKey = reinterpret_cast<PFN_GetKey>(
        ::GetProcAddress(g_original, "XInputGetKeystroke"));
    o_GetAudio = reinterpret_cast<PFN_GetAudio>(
        ::GetProcAddress(g_original, "XInputGetAudioDeviceIds"));

    return o_GetState != nullptr && o_SetState != nullptr;
}

}  // namespace cdtb::proxy

// ---------------------------------------------------------------- exports
//
// 원본 획득에 실패했으면 ERROR_DEVICE_NOT_CONNECTED(1167)를 반환한다.
// 게임은 컨트롤러 없음으로 인식하고 정상 진행한다.

extern "C" {

DWORD WINAPI XInputGetState(DWORD i, XINPUT_STATE* s) noexcept {
    if (o_GetState == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetState(i, s);
}

DWORD WINAPI XInputSetState(DWORD i, XINPUT_VIBRATION* v) noexcept {
    if (o_SetState == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_SetState(i, v);
}

DWORD WINAPI XInputGetCapabilities(DWORD i, DWORD f,
                                   XINPUT_CAPABILITIES* c) noexcept {
    if (o_GetCaps == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetCaps(i, f, c);
}

void WINAPI XInputEnable(BOOL enable) noexcept {
    if (o_Enable != nullptr) o_Enable(enable);
}

DWORD WINAPI XInputGetBatteryInformation(DWORD i, BYTE t,
                                         XINPUT_BATTERY_INFORMATION* b) noexcept {
    if (o_GetBattery == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetBattery(i, t, b);
}

DWORD WINAPI XInputGetKeystroke(DWORD i, DWORD r, PXINPUT_KEYSTROKE k) noexcept {
    if (o_GetKey == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetKey(i, r, k);
}

DWORD WINAPI XInputGetAudioDeviceIds(DWORD i, LPWSTR rid, UINT* rc,
                                     LPWSTR cid, UINT* cc) noexcept {
    if (o_GetAudio == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    return o_GetAudio(i, rid, rc, cid, cc);
}

}  // extern "C"
