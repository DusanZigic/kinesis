#include "common.hpp"
#include "tabswitcher.hpp"

static const std::set<std::string> tabbedApps = {
    "chrome.exe", 
    "msedge.exe", 
    "firefox.exe", 
    "explorer.exe",
    "WindowsTerminal.exe"
};

bool SwitchTabs(DWORD vkCode) {
    HWND activeWindow = GetForegroundWindow();
    DWORD processId;
    GetWindowThreadProcessId(activeWindow, &processId);
    std::string windowProcessName = GetProcessName(processId);
    if (tabbedApps.count(windowProcessName)) {
        INPUT inputs[8] {};
        // logically release ALT
        inputs[0].type       = INPUT_KEYBOARD;
        inputs[0].ki.wVk     = VK_MENU;
        inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
        // clear system menu state
        inputs[1].type       = INPUT_KEYBOARD;
        inputs[1].ki.wVk     = VK_ESCAPE;
        inputs[2].type       = INPUT_KEYBOARD;
        inputs[2].ki.wVk     = VK_ESCAPE;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        // send CTRL + number
        inputs[3].type       = INPUT_KEYBOARD;
        inputs[3].ki.wVk     = VK_CONTROL;
        inputs[4].type       = INPUT_KEYBOARD;
        inputs[4].ki.wVk     = vkCode;
        inputs[5].type       = INPUT_KEYBOARD;
        inputs[5].ki.wVk     = vkCode;
        inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
        // release CTRL
        inputs[6].type       = INPUT_KEYBOARD;
        inputs[6].ki.wVk     = VK_CONTROL;
        inputs[6].ki.dwFlags = KEYEVENTF_KEYUP;
        // restore ALT state
        inputs[7].type   = INPUT_KEYBOARD;
        inputs[7].ki.wVk = VK_MENU;

        SendInput(8, inputs, sizeof(INPUT));

        keybd_event(0xFC, 0, 0, 0);
        keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);

        return true;
    }

    return false;
}