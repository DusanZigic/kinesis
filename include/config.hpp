#pragma once

namespace Config {
    extern bool enableTabSwitcher;
    extern std::set<std::string> tabbedApps;

    std::string GetConfigPath();
    void SaveDefaultConfig(const std::string& fullPath);
    void LoadConfig();
}