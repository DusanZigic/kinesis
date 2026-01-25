#include "common.hpp"
#include "trayicon.hpp"
#include "tabswitcher.hpp"
#include "taskswitcher.hpp"
#include "launchers.hpp"
#include "quitsequence.hpp"
#include "config.hpp"

LRESULT CALLBACK GhostWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TRAYICON:
            if (lp == WM_RBUTTONUP) {
                ShowTrayMenu(hwnd);
            }
            break;
        case WM_DESTROY:
            HandleTrayCleanup(hwnd);
            PostQuitMessage(0);
            return 0;
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

        static unsigned int activeSwitcherMod = 0;

        if (isDown) {
            if (ctrlHeld && altHeld) {
                if (Config::enableVSCodeLauncher && pKeyBoard->vkCode == Config::VSCodeLauncherKey) {
                    ShowLauncher(LauncherMode::VSCode);
                    return 1;
                }
                if (Config::enableWSLTerminalLauncher && pKeyBoard->vkCode == Config::WSLTerminalLauncherKey) {
                    ShowLauncher(LauncherMode::WSL);
                    return 1;
                }
                if (pKeyBoard->vkCode == 'Q') {
                    InitiateQuitSequence();
                    return 1;
                }
            }
            if (IsSwitcherActive()) {
                if (pKeyBoard->vkCode == VK_LEFT || pKeyBoard->vkCode == VK_RIGHT || 
                    pKeyBoard->vkCode == VK_UP   || pKeyBoard->vkCode == VK_DOWN) {
                    AppCycleSwitcher(pKeyBoard->vkCode, SwitcherMode::None);
                    return 1;
                }
                if (pKeyBoard->vkCode == Config::allAppsSwitcherKey) {
                    AppCycleSwitcher(Config::allAppsSwitcherKey, SwitcherMode::AllApps);
                    return 1;
                }
                if (pKeyBoard->vkCode == Config::sameAppsSwitcherKey) {
                    AppCycleSwitcher(Config::sameAppsSwitcherKey, SwitcherMode::SameApp);
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_RETURN || pKeyBoard->vkCode == VK_ESCAPE) {
                    ResetSwitcherSession(pKeyBoard->vkCode);
                    activeSwitcherMod = 0;
                    return 1;
                }
            }
            if (!IsSwitcherActive() && Config::enableTaskSwitcher) {
                bool allAppsSwitcherModHeld = (GetAsyncKeyState(Config::allAppsSwitcherMod) & 0x8000);
                if (allAppsSwitcherModHeld && pKeyBoard->vkCode == Config::allAppsSwitcherKey) {
                    activeSwitcherMod = Config::allAppsSwitcherMod;
                    AppCycleSwitcher(Config::allAppsSwitcherMod, SwitcherMode::AllApps);
                    return 1;
                }

                bool sameAppsSwitcherModHeld = (GetAsyncKeyState(Config::sameAppsSwitcherMod) & 0x8000);
                if (sameAppsSwitcherModHeld && pKeyBoard->vkCode == Config::sameAppsSwitcherKey) {
                    activeSwitcherMod = Config::sameAppsSwitcherMod;
                    AppCycleSwitcher(Config::sameAppsSwitcherMod, SwitcherMode::SameApp);
                    return 1;
                }
            }
            if (Config::enableTabSwitcher) {
                if (wParam == WM_SYSKEYDOWN && pKeyBoard->vkCode >= '1' && pKeyBoard->vkCode <= '9') {
                    if (SwitchTabs(pKeyBoard->vkCode)) return 1;
                }
            }

        }
        if (isUp && IsSwitcherActive()) {
            unsigned int releasedKey = pKeyBoard->vkCode;

            if (releasedKey == VK_LMENU    || releasedKey == VK_RMENU)    releasedKey = VK_MENU;
            if (releasedKey == VK_LCONTROL || releasedKey == VK_RCONTROL) releasedKey = VK_CONTROL;
            if (releasedKey == VK_LSHIFT   || releasedKey == VK_RSHIFT)   releasedKey = VK_SHIFT;

            if (releasedKey == activeSwitcherMod) {
                ResetSwitcherSession(activeSwitcherMod);
                activeSwitcherMod = 0;
                return 0;
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    SetProcessDPIAware();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    HANDLE hMutex = CreateMutexA(NULL, TRUE, "Global\\Kinesis_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        CoUninitialize();
        return 0;
    }

    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSA wc {};
    wc.lpfnWndProc = GhostWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "KinesisGhostClass";
    RegisterClassA(&wc);

    HWND hGhostWnd = CreateWindowA(wc.lpszClassName, "KinesisGhost", 0, 0, 0, 0, 0, NULL, NULL, GetModuleHandle(NULL), NULL);
    HandleTrayInit(hGhostWnd);

    HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (hhkLowLevelKybd == NULL) {
        std::cerr << "failed to install hook" << std::endl;
        return 1;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    Config::LoadConfig();
    
    InitializeLauncher();
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ReleaseLauncherResources();

    Gdiplus::GdiplusShutdown(gdiplusToken);

    UnhookWindowsHookEx(hhkLowLevelKybd);

    CoUninitialize();

    return (int)msg.wParam;
}