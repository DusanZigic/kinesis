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
        
        bool ctrlHeld = (GetAsyncKeyState(VK_CONTROL) & 0x8000);
        bool altHeld  = (GetAsyncKeyState(VK_MENU) & 0x8000);

        if (isDown) {
            if (ctrlHeld && altHeld) {
                if (pKeyBoard->vkCode == 'V') {
                    ShowLauncher(LauncherMode::VSCode);
                    return 1;
                }
                if (pKeyBoard->vkCode == 'L') {
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
                if (pKeyBoard->vkCode == VK_TAB) {
                    AppCycleSwitcher(VK_TAB, SwitcherMode::AllApps);
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_OEM_3) {
                    AppCycleSwitcher(VK_OEM_3, SwitcherMode::SameApp);
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_RETURN) {
                    ResetSwitcherSession(VK_RETURN);
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_ESCAPE) {
                    ResetSwitcherSession(VK_ESCAPE);
                    return 1;
                }
            }
            if (pKeyBoard->vkCode == VK_TAB && altHeld) {
                AppCycleSwitcher(VK_TAB, SwitcherMode::AllApps);
                return 1;
            }
            if (pKeyBoard->vkCode == VK_OEM_3 && altHeld) {
                AppCycleSwitcher(VK_OEM_3, SwitcherMode::SameApp);
                return 1;
            }
            if (Config::enableTabSwitcher) {
                if (wParam == WM_SYSKEYDOWN && pKeyBoard->vkCode >= '1' && pKeyBoard->vkCode <= '9') {
                    if (SwitchTabs(pKeyBoard->vkCode)) return 1;
                }
            }

        }
        if (isUp) {
            if (pKeyBoard->vkCode == VK_MENU || pKeyBoard->vkCode == VK_LMENU || pKeyBoard->vkCode == VK_RMENU) {
                if (IsSwitcherActive()) {
                    ResetSwitcherSession(VK_MENU);
                    return 0;
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
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

    SetProcessDPIAware();
    
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