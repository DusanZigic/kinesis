#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001

void InitTrayIcon(HWND hGhostWnd, HICON hIcon);
void RemoveTrayIcon(HWND hGhostWnd);
void ShowTrayMenu(HWND hGhostWnd);