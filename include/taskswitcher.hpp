#pragma once

enum class SwitcherMode {
    None,
    SameApp,
    AllApps
};

struct WindowData {
    std::string targetProcessName;
    std::vector<HWND> windows;
};

struct SwitcherLayout {
    int winW;
    int winH;
    int thumbW;
    int thumbH;

    int margin;
    int spacing;
    int titleHeight;
    int fontSize;
    int cols;
    int rows;

    int maxCols;
};

bool IsSwitcherActive();
void ResetSwitcherSession(DWORD vkCode);
void AppCycleSwitcher(DWORD vkCode, SwitcherMode mode = SwitcherMode::None);