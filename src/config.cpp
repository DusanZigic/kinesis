#include "common.hpp"
#include "config.hpp"

namespace fs = std::filesystem;

namespace Config {
    bool enableTabSwitcher = true;
    std::set<std::string> tabbedApps = {
        "chrome.exe", 
        "msedge.exe", 
        "firefox.exe", 
        "explorer.exe",
        "WindowsTerminal.exe"
    };

    bool enableVSCodeLauncher = true;
    unsigned int VSCodeLauncherKey = 'V';
    bool enableWSLTerminalLauncher = true;
    unsigned int WSLTerminalLauncherKey = 'L';

    std::string GetConfigPath() {
        std::string baseAppPath = GetKnownFolderPath(FOLDERID_LocalAppData);
        if (!baseAppPath.empty()) {
            std::string kinesisPath = baseAppPath + "\\Kinesis";
            std::string configPath = kinesisPath + "\\Config";
            CreateDirectoryA(kinesisPath.c_str(), NULL);
            CreateDirectoryA(configPath.c_str(), NULL);
            return configPath + "\\config.jsonc";
        }
        return "config.jsonc";
    }

    void SaveDefaultConfig(const std::string& fullPath) {
        std::ofstream file(fullPath);
        if (!file.is_open()) return;

        file << "{\n";
        
        file << "  // Enable or disable the Alt+Number tab switching and list of apps that should use it\n"
             << "  \"enableTabSwitcher\": true,\n"
             << "  \"tabbedApps\": [";        
        size_t i = 0;
        for (const auto& app : tabbedApps) {
            file << "\"" << app << "\"";
            if (++i < tabbedApps.size()) file << ", ";
        }        
        file << "],\n\n";
        
        file << "  // Enable or disable VS Code launcher and shortcuts (Mandatory: Ctrl + Alt + Key)\n"
             << "  \"enableVSCodeLauncher\": true,\n"
             << "  \"VSCodeLauncherKey\": \"V\",\n\n";
        
        file << "  // Enable or disable WSL terminal launcher and shortcuts (Mandatory: Ctrl + Alt + Key)\n"
             << "  \"enableWSLTerminalLauncher\": true,\n"
             << "  \"WSLTerminalLauncherKey\": \"L\"\n";

        file << "}";
        
        file.close();
    }

    std::string CleanValue(std::string s) {
        s.erase(std::remove(s.begin(), s.end(), '\"'), s.end());
        s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
        s.erase(std::remove(s.begin(), s.end(), ','), s.end());
        s.erase(std::remove(s.begin(), s.end(), '['), s.end());
        s.erase(std::remove(s.begin(), s.end(), ']'), s.end());
        return s;
    }

    void AssignSetting(const std::string& key, const std::string& value) {
        std::string cleanValue = CleanValue(value);
        
        if      (key == "enableTabSwitcher")         enableTabSwitcher         = (cleanValue == "true");
        else if (key == "enableVSCodeLauncher")      enableVSCodeLauncher      = (cleanValue == "true");
        else if (key == "enableWSLTerminalLauncher") enableWSLTerminalLauncher = (cleanValue == "true");
        
        else if (key == "VSCodeLauncherKey")      VSCodeLauncherKey      = VkKeyScanA(cleanValue[0]) & 0xFF;
        else if (key == "WSLTerminalLauncherKey") WSLTerminalLauncherKey = VkKeyScanA(cleanValue[0]) & 0xFF;
    }

    void ParseTabbedApps(const std::string& val) {
        tabbedApps.clear();
        size_t start = val.find("[");
        size_t end = val.find("]");
        if (start != std::string::npos && end != std::string::npos) {
            std::string content = val.substr(start + 1, end - start - 1);
            std::stringstream ss(content);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string cleaned = CleanValue(item);
                if (!cleaned.empty()) {
                    tabbedApps.insert(cleaned);
                }
            }
        }
    }

    void LoadConfig() {
        std::string configPath = GetConfigPath();
        std::ifstream file(configPath);

        if (!file.is_open()) {
            SaveDefaultConfig(configPath);
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
        return;
    }
}