#pragma once

namespace Config {
    extern bool EnableTabSwitcher;

    std::string GetConfigPath();
    void SaveDefaultConfig(const std::string& fullPath);
    void LoadConfig();
}