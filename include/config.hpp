#pragma once

#include <string>
#include <set>
#include <map>
#include <memory>

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
    
    void DefaultConfig();
    void OpenConfig();
    void LoadConfig();
    void SaveConfig();
    void SetUIConfig(Configuration& configuration);
    void SetConfgiFromUI(const Configuration& configuration);
    std::shared_ptr<const Configuration> GetConfig();
}