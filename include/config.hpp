#pragma once

namespace Config {
    struct Configuration {
        bool enableTabSwitcher;
        std::set<std::string> tabbedApps;
        std::map<std::string, std::string> tabbedAppsNames;

        bool enableVSCodeLauncher;
        unsigned int VSCodeLauncherKey;
        
        bool enableWSLTerminalLauncher;
        unsigned int WSLTerminalLauncherKey;
        
        bool enableTaskSwitcher;
        unsigned int allAppsSwitcherMod;
        unsigned int allAppsSwitcherKey;
        unsigned int sameAppsSwitcherMod;
        unsigned int sameAppsSwitcherKey;
    };
    extern const Configuration defaultConfiguration;
    extern Configuration currentConfiguration;

    void DefaultConfig();
    void OpenConfig();
    void LoadConfig();
    void SaveConfig();
    void SetUIConfig(Configuration& configuration);
    void SetConfgiFromUI(const Configuration& configuration);
}