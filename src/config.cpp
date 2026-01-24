#include "common.hpp"
#include "config.hpp"

namespace fs = std::filesystem;

namespace Config {
    bool EnableTabSwitcher = true;

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
             << "  \"enableTabSwitcher\": true\n"
             << "}";
        file.close();
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

            if (line.find("enableTabSwitcher") != std::string::npos) {
                EnableTabSwitcher = (line.find("true") != std::string::npos);
            }
        }
        return;
    }
}