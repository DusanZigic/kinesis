#pragma once

enum class LauncherMode {
    VSCode,
    WSL
};

struct LauncherContext {
    LauncherMode type;
    std::string windowTitle;
    int logoResourceID;
    std::string historyFileName;
    bool isEngineFound = false;
    std::string executablePath;
    std::string cliPath;
    Gdiplus::Image* logoImage = nullptr;
    std::string placeholder;
    std::vector<std::string> history;
};

void InitializeLauncher();
void ShowLauncher(LauncherMode mode);
void ReleaseLauncherResources();