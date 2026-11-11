#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <string>

#include "hotkeys.hpp"
#include "trayicon.hpp"
#include "taskswitcher.hpp"
#include "launchers.hpp"
#include "quitsequence.hpp"
#include "config.hpp"
#include "configui.hpp"
#include "systemstate.hpp"

LRESULT CALLBACK GhostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (SystemState::uTaskbarRestartMsg != 0 && msg == SystemState::uTaskbarRestartMsg) {
        Tray::HandleTrayInit(hwnd);
        return 0;
    }

    switch (msg) {
        case WM_CREATE: {
            ChangeWindowMessageFilterEx(hwnd, SystemState::uTaskbarRestartMsg, 1, NULL);
            ChangeWindowMessageFilterEx(hwnd, Tray::WM_TRAYICON, 1, NULL);
            Tray::HandleTrayInit(hwnd);
            return 0;
        }
        case Tray::WM_TRAYICON: {
            if (lp == WM_RBUTTONUP) {
                Tray::ShowTrayMenu(hwnd);
            }
            break;
        }
        case Hotkeys::WM_KINESIS_SHOW_LAUNCHER: {
            Launcher::Show(static_cast<Launcher::Mode>(wp));
            return 0;
        }
        case Hotkeys::WM_KINESIS_OPEN_CONFIG: {
            ConfigUI::OpenConfigUI();
            return 0;
        }
        case Hotkeys::WM_KINESIS_INITIATE_QUIT_SEQUENCE: {
            QuitSequence::InitiateQuitSequence();
            return 0;
        }
        case Hotkeys::WM_KINESIS_SWITCHER_CYCLE: {
            TaskSwitcher::AppCycleSwitcher(static_cast<DWORD>(wp), static_cast<TaskSwitcher::SwitcherMode>(lp));
            return 0;
        }
        case Hotkeys::WM_KINESIS_SWITCHER_END: {
            TaskSwitcher::ResetSwitcherSession(static_cast<DWORD>(wp));
            return 0;
        }
        case WM_DESTROY: {
            Tray::HandleTrayCleanup(hwnd);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int main() {
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    if (!SystemState::Initialize()) {
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    Config::LoadConfig();    
    
    Launcher::Initialize();

    WNDCLASSA wc {};
    wc.lpfnWndProc = GhostWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "KinesisGhostClass";
    RegisterClassA(&wc);

    HWND hGhostWnd = CreateWindowA(wc.lpszClassName, "KinesisGhost", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hGhostWnd == NULL) {
        return 0;
    }

    DWORD errorCode;
    if (!Hotkeys::Start(hGhostWnd, errorCode)) {
        if (errorCode == ERROR_ACCESS_DENIED) {
            std::wstring errorMsg = L"Access Denied. Please try running as Administrator.";
            MessageBoxW(NULL, errorMsg.c_str(), L"Kinesis - Permission Error", MB_OK | MB_ICONSTOP | MB_TOPMOST);
        } else {
            std::wstring errorMsg = L"Critical Error: Failed to install keyboard hook.\n\n"
                                    L"Error Code: " + std::to_wstring(errorCode) + L"\n"
                                    L"The application will now exit.";
            MessageBoxW(NULL, errorMsg.c_str(), L"Kinesis - System Error", MB_OK | MB_ICONERROR | MB_TOPMOST);
        }
        return 1;
    }
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Hotkeys::Stop();

    Launcher::ReleaseResources();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    SystemState::CleanUp();

    return (int)msg.wParam;
}