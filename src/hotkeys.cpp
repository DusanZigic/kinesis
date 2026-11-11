#include "hotkeys.hpp"

#include <windows.h>
#include <thread>
#include <future>

#include "config.hpp"
#include "launchers.hpp"
#include "taskswitcher.hpp"
#include "tabswitcher.hpp"

namespace Hotkeys {
    static HWND        hTarget      = NULL;
    static DWORD       hookThreadId = 0;
    static std::thread hookThread;

    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
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
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SHOW_LAUNCHER, static_cast<WPARAM>(Launcher::Mode::VSCode), 0);
                        return 1;
                    }
                    if (Config::currentConfiguration.enableWSLTerminalLauncher && pKeyBoard->vkCode == Config::currentConfiguration.WSLTerminalLauncherKey) {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SHOW_LAUNCHER, static_cast<WPARAM>(Launcher::Mode::WSL), 0);
                        return 1;
                    }
                    if (pKeyBoard->vkCode == 'C') {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_OPEN_CONFIG, 0, 0);
                        return 1;
                    }
                    if (pKeyBoard->vkCode == 'Q') {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_INITIATE_QUIT_SEQUENCE, 0, 0);
                        return 1;
                    }
                }
                if (activeSwitcherMod != 0) {
                    if (pKeyBoard->vkCode == VK_LEFT || pKeyBoard->vkCode == VK_RIGHT || 
                        pKeyBoard->vkCode == VK_UP   || pKeyBoard->vkCode == VK_DOWN) {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::None));
                        return 1;
                    }
                    if (pKeyBoard->vkCode == Config::currentConfiguration.allAppsSwitcherKey) {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::AllApps));
                        return 1;
                    }
                    if (pKeyBoard->vkCode == Config::currentConfiguration.sameAppsSwitcherKey) {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::SameApp));
                        return 1;
                    }
                    if (pKeyBoard->vkCode == VK_RETURN || pKeyBoard->vkCode == VK_ESCAPE) {
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_END, pKeyBoard->vkCode, 0);
                        activeSwitcherMod = 0;
                        return 1;
                    }
                }
                if (activeSwitcherMod == 0 && Config::currentConfiguration.enableTaskSwitcher) {
                    bool allAppsSwitcherModHeld = (GetAsyncKeyState(Config::currentConfiguration.allAppsSwitcherMod) & 0x8000);
                    if (allAppsSwitcherModHeld && pKeyBoard->vkCode == Config::currentConfiguration.allAppsSwitcherKey) {
                        activeSwitcherMod = Config::currentConfiguration.allAppsSwitcherMod;
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::AllApps));
                        return 1;
                    }

                    bool sameAppsSwitcherModHeld = (GetAsyncKeyState(Config::currentConfiguration.sameAppsSwitcherMod) & 0x8000);
                    if (sameAppsSwitcherModHeld && pKeyBoard->vkCode == Config::currentConfiguration.sameAppsSwitcherKey) {
                        activeSwitcherMod = Config::currentConfiguration.sameAppsSwitcherMod;
                        PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_CYCLE, pKeyBoard->vkCode, static_cast<LPARAM>(TaskSwitcher::SwitcherMode::SameApp));
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
                    PostMessage(hTarget, Hotkeys::WM_KINESIS_SWITCHER_END, activeSwitcherMod, 0);
                    activeSwitcherMod = 0;
                }
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    static void HookThreadMain(std::promise<DWORD> started) {
        HHOOK hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
        DWORD error = (hook == NULL) ? GetLastError() : 0;

        hookThreadId = GetCurrentThreadId();
        started.set_value(error);
        if (hook == NULL) return;

        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) > 0) {}

        UnhookWindowsHookEx(hook);
    }

    bool Start(HWND target, DWORD& error) {
        hTarget = target;

        std::promise<DWORD> started;
        std::future<DWORD> result = started.get_future();

        hookThread = std::thread(HookThreadMain, std::move(started));

        error = result.get();
        if (error != 0) {
            hookThread.join();
            return false;
        }

        return true;
    }

    void Stop() {
        if (!hookThread.joinable()) {
            return;
        }
        
        PostThreadMessage(hookThreadId, WM_QUIT, 0, 0);
        
        hookThread.join();
    }
}