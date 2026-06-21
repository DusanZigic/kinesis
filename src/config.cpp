#include "common.hpp"
#include "config.hpp"

namespace fs = std::filesystem;

namespace Config {
    const Configuration defaultConfiguration {
        /*.enableTabSwitcher =*/ true,
        /*.tabbedApps =*/ {"chrome.exe", "msedge.exe", "firefox.exe", "WindowsTerminal.exe", "explorer.exe"},
        /*.tabbedAppsName =*/ {{"chrome.exe", "Google Chrome"}, {"msedge.exe", "Microsoft Edge"}, {"firefox.exe", "Firefox"}, {"WindowsTerminal.exe", "Terminal"}, {"explorer.exe", "File Explorer"}},
        /*.enableVSCodeLauncher =*/ true,
        /*.VSCodeLauncherKey =*/ 'V',
        /*.enableWSLTerminalLauncher =*/ true,
        /*.WSLTerminalLauncherKey =*/ 'L',
        /*.enableTaskSwitcher =*/ true,
        /*.allAppsSwitcherMod =*/ VK_MENU,
        /*.allAppsSwitcherKey =*/ VK_TAB,
        /*.sameAppsSwitcherMod =*/ VK_MENU,
        /*.sameAppsSwitcherKey =*/ VK_OEM_3
    };
    Configuration currentConfiguration;

    static unsigned int StringToVK(const std::string& s) {
        std::string upper = Common::ToUpper(s);
        if (upper == "ALT")   return VK_MENU;
        if (upper == "CTRL")  return VK_CONTROL;
        if (upper == "SHIFT") return VK_SHIFT;
        if (upper == "TAB")   return VK_TAB;
        if (upper == "SPACE") return VK_SPACE;
        if (upper == "TILDE") return VK_OEM_3;
        
        return VkKeyScanA(s[0]) & 0xFF;
    }

    static std::string VkToString(unsigned int vk) {
        switch (vk) {
            case VK_MENU:    return "ALT";
            case VK_CONTROL: return "CTRL";
            case VK_SHIFT:   return "SHIFT";
            case VK_TAB:     return "TAB";
            case VK_SPACE:   return "SPACE";
            case VK_OEM_3:   return "TILDE";
        }

        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
            return std::string(1, (char)vk);
        }
        
        char name[64];
        LONG scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    
        if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR  || vk == VK_NEXT   || vk == VK_LEFT || vk == VK_RIGHT ||
            vk == VK_UP     || vk == VK_DOWN) 
        {
            scanCode |= 0x100;
        }

        if (GetKeyNameTextA(scanCode << 16, name, sizeof(name))) {
            return std::string(name);
        }

        return "UNKNOWN";
    }

    static std::string GetConfigPath() {
        std::string baseAppPath = Common::GetKnownFolderPath(FOLDERID_LocalAppData);
        if (!baseAppPath.empty()) {
            std::string kinesisPath = baseAppPath + "\\Kinesis";
            std::string configPath = kinesisPath + "\\Config";
            CreateDirectoryA(kinesisPath.c_str(), NULL);
            CreateDirectoryA(configPath.c_str(), NULL);
            return configPath + "\\config.jsonc";
        }
        return "config.jsonc";
    }

    static void SaveConfig(const std::string& fullPath, const Configuration& configuration) {
        std::ofstream file(fullPath);
        if (!file.is_open()) return;

        file << "{\n";
        
        file << "  // Enable or disable Task Switcher\n";
        file << "  \"enableTaskSwitcher\": "<< std::boolalpha << configuration.enableTaskSwitcher << ",\n\n";
        
        file << "  // All apps task switcher\n"
             << "  \"allAppsSwitcherMod\": \"" << VkToString(configuration.allAppsSwitcherMod) << "\",\n"
             << "  \"allAppsSwitcherKey\": \"" << VkToString(configuration.allAppsSwitcherKey) << "\",\n\n";

        file << "  // Same app / cluster task switcher\n"
             << "  \"sameAppsSwitcherMod\": \"" << VkToString(configuration.sameAppsSwitcherMod) << "\",\n"
             << "  \"sameAppsSwitcherKey\": \"" << VkToString(configuration.sameAppsSwitcherKey) << "\",\n\n";
             
        file << "  // Enable or disable VS Code launcher and shortcuts (Mandatory: Ctrl + Alt + Key)\n";
        file << "  \"enableVSCodeLauncher\": "<< std::boolalpha << configuration.enableVSCodeLauncher << ",\n";
        file << "  \"VSCodeLauncherKey\": \"" << static_cast<char>(configuration.VSCodeLauncherKey) << "\",\n\n";
        
        file << "  // Enable or disable WSL terminal launcher and shortcuts (Mandatory: Ctrl + Alt + Key)\n";
        file << "  \"enableWSLTerminalLauncher\": " << std::boolalpha << configuration.enableWSLTerminalLauncher << ",\n";
        file << "  \"WSLTerminalLauncherKey\": \"" << static_cast<char>(configuration.WSLTerminalLauncherKey) << "\",\n\n";
             
        file << "  // Enable or disable the Alt+Number tab switching and list of apps that should use it\n";
        file << "  \"enableTabSwitcher\": "<< std::boolalpha << configuration.enableTabSwitcher << ",\n";
        file << "  \"tabbedApps\": [";        
        size_t i = 0;
        for (const auto& app : configuration.tabbedApps) {
            file << "\"" << app << "\"";
            if (++i < configuration.tabbedApps.size()) file << ", ";
        }        
        file << "]\n";

        file << "}";
        
        file.close();
    }

    void SaveConfig() {
        std::string configPath = GetConfigPath();
        SaveConfig(configPath, currentConfiguration);
    }

    void OpenConfig() {
        std::string configPath = GetConfigPath();
        ShellExecuteA(NULL, "open", configPath.c_str(), NULL, NULL, SW_SHOW);
    }

    void DefaultConfig() {
        currentConfiguration = defaultConfiguration;
    }

    static std::string CleanValue(std::string s) {
        s.erase(std::remove(s.begin(), s.end(), '\"'), s.end());
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        s.erase(std::remove(s.begin(), s.end(), ','), s.end());
        s.erase(std::remove(s.begin(), s.end(), '['), s.end());
        s.erase(std::remove(s.begin(), s.end(), ']'), s.end());
        return s;
    }

    static void AssignSetting(const std::string& key, const std::string& value) {
        std::string cleanValue = CleanValue(value);
        
        if      (key == "enableTabSwitcher")         currentConfiguration.enableTabSwitcher         = (cleanValue == "true");
        else if (key == "enableVSCodeLauncher")      currentConfiguration.enableVSCodeLauncher      = (cleanValue == "true");
        else if (key == "enableWSLTerminalLauncher") currentConfiguration.enableWSLTerminalLauncher = (cleanValue == "true");
        else if (key == "enableTaskSwitcher")        currentConfiguration.enableTaskSwitcher        = (cleanValue == "true");
        
        else if (key == "VSCodeLauncherKey")      currentConfiguration.VSCodeLauncherKey      = StringToVK(cleanValue);
        else if (key == "WSLTerminalLauncherKey") currentConfiguration.WSLTerminalLauncherKey = StringToVK(cleanValue);
        else if (key == "allAppsSwitcherMod")     currentConfiguration.allAppsSwitcherMod     = StringToVK(cleanValue);
        else if (key == "allAppsSwitcherKey")     currentConfiguration.allAppsSwitcherKey     = StringToVK(cleanValue);
        else if (key == "sameAppsSwitcherMod")    currentConfiguration.sameAppsSwitcherMod    = StringToVK(cleanValue);
        else if (key == "sameAppsSwitcherKey")    currentConfiguration.sameAppsSwitcherKey    = StringToVK(cleanValue);
    }

    static void ParseTabbedApps(const std::string& val) {
        currentConfiguration.tabbedApps.clear();
        size_t start = val.find("[");
        size_t end = val.find("]");
        if (start != std::string::npos && end != std::string::npos) {
            std::string content = val.substr(start + 1, end - start - 1);
            std::stringstream ss(content);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string cleaned = CleanValue(item);
                if (!cleaned.empty()) {
                    currentConfiguration.tabbedApps.insert(cleaned);
                }
            }
        }
    }

    void LoadConfig() {
        std::string configPath = GetConfigPath();
        std::ifstream file(configPath);

        if (!file.is_open()) {
            SaveConfig(configPath, defaultConfiguration);
            currentConfiguration = defaultConfiguration;
            return;
        }

        std::string line;
        while (std::getline(file, line)) {
            line.erase(0, line.find_first_not_of(" \t"));            
            if (line.empty() || line.find("//") == 0 || line.find("#") == 0) continue;

            size_t delim = line.find(":");
            if (delim == std::string::npos) continue;

            std::string key = CleanValue(line.substr(0, delim));
            std::string val = line.substr(delim + 1);

            if (key == "tabbedApps") {
                ParseTabbedApps(val);
            } else {
                AssignSetting(key, val);
            }

        }
        
        currentConfiguration.tabbedAppsNames = defaultConfiguration.tabbedAppsNames;

        return;
    }

    void SetUIConfig(Configuration& configuration) {
        configuration = currentConfiguration;
    }

    void SetConfgiFromUI(const Configuration& configuration) {
        currentConfiguration = configuration;
    }
}