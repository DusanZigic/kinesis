#include "common.hpp"
#include "systemstate.hpp"

namespace SystemState {
    HANDLE hMutex = NULL;
    UINT uTaskbarRestartMsg = 0;

    const char* REG_PATH = "Software\\Kinesis";
    const char* RUN_KEY = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const char* APP_NAME = "Kinesis";

    bool GetRegBool(const char* valueName) {
        DWORD data = 0;
        DWORD size = sizeof(data);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, valueName, NULL, NULL, (BYTE*)&data, &size);
            RegCloseKey(hKey);
        }
        return data != 0;
    }

    void SetRegBool(const char* valueName, bool value) {
        DWORD data = value ? 1 : 0;
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, valueName, 0, REG_DWORD, (const BYTE*)&data, sizeof(data));
            RegCloseKey(hKey);
        }
    }

    std::string GetRegString(const char* valueName) {
        char buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExA(hKey, valueName, NULL, NULL, (BYTE*)buffer, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(buffer);
            }
            RegCloseKey(hKey);
        }
        return "";
    }

    void SetRegString(const char* valueName, const char* data) {
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_PATH, 0, NULL, 
            REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, valueName, 0, REG_SZ, (const BYTE*)data, (DWORD)strlen(data) + 1);
            RegCloseKey(hKey);
        }
    }

    std::string GetCurrentProcessPath() {
        char szPath[MAX_PATH];
        GetModuleFileNameA(NULL, szPath, MAX_PATH);
        return std::string(szPath);
    }

    bool IsAppRunningAsAdmin() {
        bool isAdmin = false;
        HANDLE hToken = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elevation;
            DWORD dwSize;
            if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
                isAdmin = (elevation.TokenIsElevated != 0);
            }
        }
        if (hToken) CloseHandle(hToken);
        return isAdmin;
    }

    void DisableAllStartup() {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, APP_NAME);
            RegCloseKey(hKey);
        }

        std::string delCmd = "/delete /tn \"" + std::string(APP_NAME) + "\" /f";
        ShellExecuteA(NULL, "open", "schtasks.exe", delCmd.c_str(), NULL, SW_HIDE);
    }

    void EnableAdminStartup(const std::string& fullPath) {
        DisableAllStartup();

        std::string psCommand = 
            "schtasks /create /tn '" + std::string(APP_NAME) + "' /tr \"\\\"" + fullPath + "\\\"\" /sc onlogon /rl highest /f; " +
            "$t = Get-ScheduledTask -TaskName '" + std::string(APP_NAME) + "'; " +
            "$t.Settings.DisallowStartIfOnBatteries = $false; " +
            "$t.Settings.StopIfGoingOnBatteries = $false; " +
            "Set-ScheduledTask -InputObject $t";
        
        std::string finalArgs = "-NoProfile -ExecutionPolicy Bypass -Command \"" + psCommand + "\"";

        ShellExecuteA(NULL, "open", "powershell.exe", finalArgs.c_str(), NULL, SW_HIDE);
    }

    void EnableStandardStartup(const std::string& fullPath) {
        DisableAllStartup();
        
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, APP_NAME, 0, REG_SZ, (const BYTE*)fullPath.c_str(), (DWORD)fullPath.length() + 1);
            RegCloseKey(hKey);
        }
    }

    void SyncSystemState() {
        bool onStartup = GetRegBool("OnStartup");
        bool asAdmin   = GetRegBool("RunAsAdmin");
        bool currentlyIsAdmin = IsAppRunningAsAdmin();

        char szPath[MAX_PATH];
        GetModuleFileNameA(NULL, szPath, MAX_PATH);
        std::string currentPath(szPath);

        if (GetRegString("LastKnownPath") != currentPath) {
            SetRegString("LastKnownPath", currentPath.c_str());
        }

        if (onStartup) {
            if (asAdmin && currentlyIsAdmin) {
                EnableAdminStartup(currentPath);
            } else {
                EnableStandardStartup(currentPath);
            }
        } else {
            DisableAllStartup();
        }
    }

    bool IsOnStartupEnabled() {
        return GetRegBool("OnStartup");
    }
    
    bool IsRunAsAdminEnabled() {
        return GetRegBool("RunAsAdmin");
    }

    void SetOnStartup(bool enable) {
        SetRegBool("OnStartup", enable);
        SyncSystemState();
    }

    bool CoreInitialize() {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        uTaskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated"); 

        RegisterApplicationRestart(NULL, RESTART_NO_REBOOT | RESTART_NO_CRASH);

        SECURITY_DESCRIPTOR sd;
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = &sd;
        sa.bInheritHandle = FALSE;

        hMutex = CreateMutexA(&sa, TRUE, "Global\\Kinesis_SingleInstance_Mutex");

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            CleanUp();
            return false;
        }

        return true;
    }

    void SetRunAsAdmin(bool enable) {
        SetRegBool("RunAsAdmin", enable);
        SyncSystemState();

        bool currentlyAdmin = IsUserAnAdmin();

        if (enable == currentlyAdmin) return;

        CleanUp();

        if (ReLaunchApp(enable)) {
            SetRegBool("RunAsAdmin", enable);
            SyncSystemState();
            PostQuitMessage(0);
        } else {
            CoreInitialize();
            SetRegBool("RunAsAdmin", currentlyAdmin);
        }
    }

    bool Initialize() {
        if (!CoreInitialize()) {
            return false;
        }
        if (GetRegBool("RunAsAdmin") && !IsUserAnAdmin()) {
            CleanUp();
            if (ReLaunchApp(true)) {
                return false;
            } else {
                SetRegBool("RunAsAdmin", false);
                SyncSystemState();
                if (!CoreInitialize()) {
                    return false;
                }
            }
        }
        SyncSystemState();
        return true;
    }

    bool ReLaunchApp(bool asAdmin) {
        char szPath[MAX_PATH];
        if (GetModuleFileNameA(NULL, szPath, MAX_PATH)) {
            if (asAdmin) {
                SHELLEXECUTEINFOA sei {};
                sei.cbSize = sizeof(sei);
                sei.lpVerb = "runas";
                sei.lpFile = szPath;
                sei.nShow = SW_NORMAL;
                return ShellExecuteExA(&sei);
            } else {
                HINSTANCE hInst = ShellExecuteA(NULL, "open", "explorer.exe", szPath, NULL, SW_NORMAL);
                return (INT_PTR)hInst > 32;
            }
        }
        return false;
    }

    void CleanUp() {
        if (hMutex) {
            ReleaseMutex(hMutex); 
            CloseHandle(hMutex);
            hMutex = NULL;
        }
        CoUninitialize();
    }
}