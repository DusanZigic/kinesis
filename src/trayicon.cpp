#include "onstartup.hpp"
#include "trayicon.h"

void InitTrayIcon(HWND hGhostWnd, HICON hIcon) {
    NOTIFYICONDATAA nid {};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hGhostWnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON; 
    nid.hIcon = hIcon;

    if (!hIcon) {
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
    }
    
    strcpy_s(nid.szTip, "Kinesis");

    Shell_NotifyIconA(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hGhostWnd) {
    NOTIFYICONDATAA nid {};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hGhostWnd;
    nid.uID = ID_TRAY_APP_ICON;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

void ShowTrayMenu(HWND hGhostWnd) {
    POINT curPoint;
    GetCursorPos(&curPoint);

    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        bool startup = IsStartupEnabled();

        UINT startupFlags = MF_STRING | (startup ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuA(hMenu, startupFlags, 1, "On Startup");
        AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuA(hMenu, MF_STRING, 2, "Quit Kinesis");

        SetForegroundWindow(hGhostWnd);

        int clicked = TrackPopupMenu(
            hMenu, 
            TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY, 
            curPoint.x, curPoint.y,
            0,
            hGhostWnd,
            NULL
        );

        if (clicked == 1) {
            SetStartup(!startup);
        } else if (clicked == 2) {
            PostQuitMessage(0);
        }

        DestroyMenu(hMenu);
    }
}