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

        file << "{\n"
             << "  // Enable or disable the Alt+Number tab switching\n"
             << "  \"enableTabSwitcher\": true,\n\n"
             << "  // Apps that should use Alt+Number as Ctrl+Number\n"
             << "  \"tabbedApps\": [";
        
             size_t i = 0;
        for (const auto& app : tabbedApps) {
            file << "\"" << app << "\"";
            if (++i < tabbedApps.size()) file << ", ";
        }
        
        file << "]\n"
             << "}";
        
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

            if (line.find("\"enableTabSwitcher\"") != std::string::npos) {
                enableTabSwitcher = (line.find("true") != std::string::npos);
            }

            if (line.find("\"tabbedApps\"") != std::string::npos) {
                tabbedApps.clear();
                size_t start = line.find("[");
                size_t end = line.find("]");
                if (start != std::string::npos && end != std::string::npos) {
                    std::string content = line.substr(start + 1, end - start - 1);
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
        }
        return;
    }
}