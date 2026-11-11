#include <windows.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <string>

#include "trayicon.hpp"
#include "tabswitcher.hpp"
#include "taskswitcher.hpp"
#include "launchers.hpp"
#include "quitsequence.hpp"
#include "config.hpp"
#include "configui.hpp"
#include "systemstate.hpp"

constexpr UINT WM_KINESIS_SHOW_LAUNCHER          = WM_APP + 1;
constexpr UINT WM_KINESIS_OPEN_CONFIG            = WM_APP + 2;
constexpr UINT WM_KINESIS_INITIATE_QUIT_SEQUENCE = WM_APP + 3;
constexpr UINT WM_KINESIS_SWITCHER_CYCLE         = WM_APP + 4;
constexpr UINT WM_KINESIS_SWITCHER_END           = WM_APP + 5;

static HWND hGhostWnd = NULL;

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
        case WM_KINESIS_SHOW_LAUNCHER: {
            Launcher::Show(static_cast<Launcher::Mode>(wp));
            return 0;
        }
        case WM_KINESIS_OPEN_CONFIG: {
            ConfigUI::OpenConfigUI();
            return 0;
        }
        case WM_KINESIS_INITIATE_QUIT_SEQUENCE: {
            QuitSequence::InitiateQuitSequence();
            return 0;
        }
        case WM_KINESIS_SWITCHER_CYCLE: {
            TaskSwitcher::AppCycleSwitcher(static_cast<DWORD>(wp), static_cast<TaskSwitcher::SwitcherMode>(lp));
            return 0;
        }
        case WM_KINESIS_SWITCHER_END: {
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

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT*)lParam;

        bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
        
        bool ctrlHeld  = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
        bool altHeld   = (GetAsyncKeyState(VK_MENU) & 0x8000);

        static unsigned int activeSwitcherMod = 0; // NOTE (dusan): 0 = no switcher session, otherwise the modifier that started it

        if (isDown) {
            if (ctrlHeld && altHeld) {
                if (Config::currentConfiguration.enableVSCodeLauncher && pKeyBoard->vkCode == Config::currentConfiguration.VSCodeLauncherKey) {
                    PostMessage(hGhostWnd, WM_KINESIS_SHOW_LAUNCHER, static_cast<WPARAM>(Launcher::Mode::VSCode), 0);
                    return 1;
                }
                if (Config::currentConfiguration.enableWSLTerminalLauncher && pKeyBoard->vkCode == Config::currentConfiguration.WSLTerminalLauncherKey) {
                    PostMessage(hGhostWnd, WM_KINESIS_SHOW_LAUNCHER, static_cast<WPARAM>(Launcher::Mode::WSL), 0);
                    return 1;
                }
                if (pKeyBoard->vkCode == 'C') {
                    PostMessage(hGhostWnd, WM_KINESIS_OPEN_CONFIG, 0, 0);
                    return 1;
                }
                if (pKeyBoard->vkCode == 'Q') {
                    PostMessage(hGhostWnd, WM_KINESIS_INITIATE_QUIT_SEQUENCE, 0, 0);
                    return 1;
                }
            }
            if (activeSwitcherMod != 0) {
                if (pKeyBoard->vkCode == VK_LEFT || pKeyBoard->vkCode == VK_RIGHT || 
                    pKeyBoard->vkCode == VK_UP   || pKeyBoard->vkCode == VK_DOWN) {
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::None));
                    return 1;
                }
                if (pKeyBoard->vkCode == Config::currentConfiguration.allAppsSwitcherKey) {
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::AllApps));
                    return 1;
                }
                if (pKeyBoard->vkCode == Config::currentConfiguration.sameAppsSwitcherKey) {
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::SameApp));
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_RETURN || pKeyBoard->vkCode == VK_ESCAPE) {
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_END, pKeyBoard->vkCode, 0);
                    activeSwitcherMod = 0;
                    return 1;
                }
            }
            if (activeSwitcherMod == 0 && Config::currentConfiguration.enableTaskSwitcher) {
                bool allAppsSwitcherModHeld = (GetAsyncKeyState(Config::currentConfiguration.allAppsSwitcherMod) & 0x8000);
                if (allAppsSwitcherModHeld && pKeyBoard->vkCode == Config::currentConfiguration.allAppsSwitcherKey) {
                    activeSwitcherMod = Config::currentConfiguration.allAppsSwitcherMod;
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::AllApps));
                    return 1;
                }

                bool sameAppsSwitcherModHeld = (GetAsyncKeyState(Config::currentConfiguration.sameAppsSwitcherMod) & 0x8000);
                if (sameAppsSwitcherModHeld && pKeyBoard->vkCode == Config::currentConfiguration.sameAppsSwitcherKey) {
                    activeSwitcherMod = Config::currentConfiguration.sameAppsSwitcherMod;
                    PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::SameApp));
                    return 1;
                }
            }
            if (Config::currentConfiguration.enableTabSwitcher) {
                if (wParam == WM_SYSKEYDOWN && pKeyBoard->vkCode >= '1' && pKeyBoard->vkCode <= '9') {
                    if (TabSwitcher::SwitchTabs(pKeyBoard->vkCode)) return 1;
                }
            }

        }
        if (isUp && activeSwitcherMod != 0) {
            unsigned int releasedKey = pKeyBoard->vkCode;

            if (releasedKey == VK_LMENU    || releasedKey == VK_RMENU)    releasedKey = VK_MENU;
            if (releasedKey == VK_LCONTROL || releasedKey == VK_RCONTROL) releasedKey = VK_CONTROL;
            if (releasedKey == VK_LSHIFT   || releasedKey == VK_RSHIFT)   releasedKey = VK_SHIFT;

            if (releasedKey == activeSwitcherMod) {
                PostMessage(hGhostWnd, WM_KINESIS_SWITCHER_END, activeSwitcherMod, 0);
                activeSwitcherMod = 0;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
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

    WNDCLASSA wc {};
    wc.lpfnWndProc = GhostWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "KinesisGhostClass";
    RegisterClassA(&wc);

    hGhostWnd = CreateWindowA(wc.lpszClassName, "KinesisGhost", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hGhostWnd == NULL) return 0;

    HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (hhkLowLevelKybd == NULL) {
        DWORD errorCode = GetLastError();
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

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    Config::LoadConfig();    
    Launcher::Initialize();
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hhkLowLevelKybd);
    Launcher::ReleaseResources();
    Gdiplus::GdiplusShutdown(gdiplusToken);
    SystemState::CleanUp();

    return (int)msg.wParam;
}