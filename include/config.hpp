#pragma once

#include <string>
#include <set>
#include <map>
#include <mutex>

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
    extern std::mutex configMutex;

    void DefaultConfig();
    void OpenConfig();
    void LoadConfig();
    void SaveConfig();
    void SetUIConfig(Configuration& configuration);
    void SetConfgiFromUI(const Configuration& configuration);
}