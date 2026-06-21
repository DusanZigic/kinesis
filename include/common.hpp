#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <psapi.h>
#include <commoncontrols.h>
#include <shlobj.h>
#include <objbase.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <string>
#include <sstream>
#include <cmath>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

namespace Common {

    std::string ToUpper(std::string s);
    std::string ToLower(std::string s);
    std::string GetProcessName(DWORD pid);
    std::string WToUTF8(const std::wstring& wstr);
    std::wstring UTF8ToW(const std::string& str);
    std::string GetKnownFolderPath(REFKNOWNFOLDERID rfid);
    std::wstring GetKnownFolderPathW(REFKNOWNFOLDERID rfid);
    void SmoothShowWindow(HWND hwnd);
} // namespace Common 