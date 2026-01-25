#pragma once

#define WM_TRAYICON (WM_USER + 1)

enum TrayMenuIDs {
    ID_EXIT = 100,
    ID_EDIT_CONFIG,
    ID_RELOAD_CONFIG,
    ID_DEFAULT_CONFIG,
    ID_STARTUP_TOGGLE,
    ID_ADMIN_TOGGLE
};

void HandleTrayInit(HWND hGhostWnd);
void HandleTrayCleanup(HWND hGhostWnd);
void ShowTrayMenu(HWND hGhostWnd);