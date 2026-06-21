#pragma once

namespace Tray {
    inline constexpr UINT WM_TRAYICON = WM_USER + 1;

    void HandleTrayInit(HWND hGhostWnd);
    void HandleTrayCleanup(HWND hGhostWnd);
    void ShowTrayMenu(HWND hGhostWnd);
} // namespace Tray