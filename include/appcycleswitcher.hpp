#pragma once
#define WIN32_LEAN_AND_MEAN

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
};

bool IsSwitcherActive();
void ResetAltTildeSession(DWORD vkCode);
void AppCycleSwitcher(DWORD vkCode);