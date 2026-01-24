#pragma once

#define WM_TRAYICON (WM_USER + 1)

void HandleTrayInit(HWND hGhostWnd);
void HandleTrayCleanup(HWND hGhostWnd);
void ShowTrayMenu(HWND hGhostWnd);