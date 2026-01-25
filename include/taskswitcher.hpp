#pragma once

enum class SwitcherMode {
    None,
    SameApp,
    AllApps
};

struct WindowEntry {
    HWND hwnd;
    HICON hIcon;
    std::string title;
};

struct WindowData {
    std::string targetProcessName;
    std::vector<WindowEntry> windows;
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