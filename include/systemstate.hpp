#pragma once

namespace SystemState {
    extern HANDLE hMutex;
    extern UINT uTaskbarRestartMsg;

    bool IsOnStartupEnabled();
    bool IsRunAsAdminEnabled();

    void SetOnStartup(bool enable);
    void SetRunAsAdmin(bool enable);

    bool Initialize();
    bool ReLaunchApp(bool asAdmin);
    void CleanUp();
}