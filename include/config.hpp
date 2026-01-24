#pragma once

namespace Config {
    extern bool enableTabSwitcher;
    extern std::set<std::string> tabbedApps;

    extern bool enableVSCodeLauncher;
    extern unsigned int VSCodeLauncherKey;
    extern bool enableWSLTerminalLauncher;
    extern unsigned int WSLTerminalLauncherKey;

    std::string GetConfigPath();
    void SaveDefaultConfig(const std::string& fullPath);
    void LoadConfig();
}