#pragma once

#include <windef.h>
#include <set>
#include <string>

namespace TabSwitcher {
    bool SwitchTabs(DWORD vkCode, const std::set<std::string>& tabbedApps);
} // namespace TabSwitcher