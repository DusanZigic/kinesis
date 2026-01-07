#include "common.hpp"
#include "tabswitcher.hpp"
#include "appcycleswitcher.hpp"
#include "vscodelauncher.hpp"
#include "quitsequence.hpp"

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
                    ShowLauncher();
                    return 1;
                }
                if (pKeyBoard->vkCode == 'Q') {
                    InitiateQuitSequence();
                    return 1;
                }
            }
            if (IsSwitcherActive()) {
                if (pKeyBoard->vkCode == VK_LEFT || pKeyBoard->vkCode == VK_RIGHT || 
                    pKeyBoard->vkCode == VK_UP   || pKeyBoard->vkCode == VK_DOWN || 
                    pKeyBoard->vkCode == VK_OEM_3) {
                    AppCycleSwitcher(pKeyBoard->vkCode);
                    return 1;
                }
                if (pKeyBoard->vkCode == VK_RETURN) {
                    ResetAltTildeSession(VK_RETURN);
                    return 1;
                }
            }
            if (wParam == WM_SYSKEYDOWN && pKeyBoard->vkCode == VK_OEM_3 && !ctrlHeld) {
                AppCycleSwitcher(0); 
                return 1;
            }
            if (wParam == WM_SYSKEYDOWN && pKeyBoard->vkCode >= '1' && pKeyBoard->vkCode <= '9') {
                if (SwitchTabs(pKeyBoard->vkCode)) return 1;
            }

        }
        if (isUp) {
            if (pKeyBoard->vkCode == VK_MENU || pKeyBoard->vkCode == VK_LMENU || pKeyBoard->vkCode == VK_RMENU) {
                if (IsSwitcherActive()) {
                    ResetAltTildeSession(VK_MENU);
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

    HHOOK hhkLowLevelKybd = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (hhkLowLevelKybd == NULL) {
        std::cerr << "failed to install hook" << std::endl;
        return 1;
    }

    SetProcessDPIAware();
    InitializePaths();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hhkLowLevelKybd);

    CoUninitialize();

    return (int)msg.wParam;
}