#pragma once

namespace TaskSwitcher {
    enum class SwitcherMode {
        None,
        SameApp,
        AllApps
    };

    bool IsSwitcherActive();
    void ResetSwitcherSession(DWORD vkCode);
    void AppCycleSwitcher(DWORD vkCode, SwitcherMode mode = SwitcherMode::None);

} // namespace Switcher