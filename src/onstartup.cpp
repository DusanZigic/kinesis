#include "common.hpp"
#include "onstartup.hpp"

bool IsStartupEnabled() {
    HKEY hKey;
    LONG result = RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS) {
        result = RegQueryValueExA(hKey, "KinesisLauncher", NULL, NULL, NULL, NULL);
        RegCloseKey(hKey);
        return (result == ERROR_SUCCESS);
    }
    return false;
}

void SetStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            char path[MAX_PATH];
            GetModuleFileNameA(NULL, path, MAX_PATH);
            RegSetValueExA(hKey, "KinesisLauncher", 0, REG_SZ, (const BYTE*)path, (DWORD)strlen(path) + 1);
        } else {
            RegDeleteValueA(hKey, "KinesisLauncher");
        }
        RegCloseKey(hKey);
    }
}