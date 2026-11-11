#pragma once

#include <windows.h>

namespace Hotkeys {
    inline constexpr UINT WM_KINESIS_SHOW_LAUNCHER          = WM_APP + 1;
    inline constexpr UINT WM_KINESIS_OPEN_CONFIG            = WM_APP + 2;
    inline constexpr UINT WM_KINESIS_INITIATE_QUIT_SEQUENCE = WM_APP + 3;
    inline constexpr UINT WM_KINESIS_SWITCHER_CYCLE         = WM_APP + 4;
    inline constexpr UINT WM_KINESIS_SWITCHER_END           = WM_APP + 5;

    bool Start(HWND target, DWORD& error);
    void Stop();
}