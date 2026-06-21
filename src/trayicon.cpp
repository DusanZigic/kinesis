#include "common.hpp"
#include "config.hpp"
#include "systemstate.hpp"
#include "configui.hpp"
#include "trayicon.hpp"

// #define ID_TRAY_APP_ICON 1001

namespace Tray {
    constexpr UINT ID_TRAY_APP_ICON = 1001;

    enum TrayMenuIDs {
        ID_EXIT = 100,
        ID_EDIT_CONFIG,
        ID_RELOAD_CONFIG,
        ID_DEFAULT_CONFIG,
        ID_OPEN_CONFIG_UI,
        ID_STARTUP_TOGGLE,
        ID_ADMIN_TOGGLE
    };

    void HandleTrayInit(HWND hGhostWnd) {
    #ifdef NDEBUG
        HINSTANCE hInst = GetModuleHandle(NULL);
        HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(1));
        if (!hIcon) hIcon = LoadIcon(NULL, IDI_APPLICATION);

        NOTIFYICONDATAA nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hGhostWnd;
        nid.uID = ID_TRAY_APP_ICON;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = hIcon;
        strcpy(nid.szTip, "kinesis");

        Shell_NotifyIconA(NIM_ADD, &nid);
    #else
        (void)hGhostWnd;
    #endif
    }

    void HandleTrayCleanup(HWND hGhostWnd) {
    #ifdef NDEBUG
        NOTIFYICONDATAA nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hGhostWnd;
        nid.uID = ID_TRAY_APP_ICON;
        Shell_NotifyIconA(NIM_DELETE, &nid);
    #else
        (void)hGhostWnd;
    #endif
    }

    void ShowTrayMenu(HWND hGhostWnd) {
        HMENU hMenu    = CreatePopupMenu();
        HMENU hSubMenu = CreatePopupMenu();
        if (hMenu && hSubMenu) {
            bool isOnStartup = SystemState::IsOnStartupEnabled();
            bool isAdminRun  = SystemState::IsRunAsAdminEnabled();

            AppendMenuW(hMenu, MF_STRING, ID_OPEN_CONFIG_UI, L"Open Configuration UI");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

            AppendMenuW(hSubMenu, MF_STRING | (isOnStartup ? MF_CHECKED : MF_UNCHECKED), ID_STARTUP_TOGGLE, L"On Startup");
            AppendMenuW(hSubMenu, MF_STRING | (isAdminRun  ? MF_CHECKED : MF_UNCHECKED), ID_ADMIN_TOGGLE,   L"As Admin");
            AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hSubMenu, MF_STRING, ID_EDIT_CONFIG,   L"Edit Configuration File");
            AppendMenuW(hSubMenu, MF_STRING, ID_RELOAD_CONFIG, L"Reload Configuration File");
            AppendMenuW(hSubMenu, MF_SEPARATOR, 0, NULL);        
            AppendMenuW(hSubMenu, MF_STRING, ID_DEFAULT_CONFIG, L"Restore Configuration Defaults");
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hSubMenu, L"Advanced");

            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING,    ID_EXIT, L"Quit Kinesis");
            
            POINT curPoint;
            GetCursorPos(&curPoint);
            SetForegroundWindow(hGhostWnd);

            int clicked = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, curPoint.x, curPoint.y, 0, hGhostWnd, NULL);
            DestroyMenu(hMenu);

            switch (clicked) {
                case ID_STARTUP_TOGGLE:
                    SystemState::SetOnStartup(!isOnStartup);
                    break;
                case ID_ADMIN_TOGGLE:
                    SystemState::SetRunAsAdmin(!isAdminRun);
                    break;
                case ID_OPEN_CONFIG_UI:
                    ConfigUI::OpenConfigUI();
                    break;
                case ID_EDIT_CONFIG:
                    Config::OpenConfig();
                    break;
                case ID_RELOAD_CONFIG:
                    Config::LoadConfig();
                    break;
                case ID_DEFAULT_CONFIG:
                    Config::DefaultConfig();
                    Config::SaveConfig();
                    break;
                case ID_EXIT:
                    PostQuitMessage(0);
                    break;
            }
        }
    }
} // namspace Tray