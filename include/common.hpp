#pragma once

#include <string>
#include <shlobj.h>

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