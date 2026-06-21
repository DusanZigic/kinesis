#include "common.hpp"

namespace Common {
    std::string ToUpper(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), 
                    [](unsigned char c){ return std::toupper(c); });
        return s;
    }

    std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), 
                    [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    std::string GetProcessName(DWORD pid) {
        char path[MAX_PATH] = {0};
        DWORD size = MAX_PATH;
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            if (QueryFullProcessImageNameA(hProcess, 0, path, &size)) {
                std::string fileName = PathFindFileNameA(path);
                CloseHandle(hProcess);
                return fileName;
            }
            CloseHandle(hProcess);
        }
        return "<unknown>";
    }

    std::string WToUTF8(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, NULL, NULL);
        return result;
    }

    std::wstring UTF8ToW(const std::string& str) {
        if (str.empty()) return L"";
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
        std::wstring result(size - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
        return result;
    }

    std::string GetKnownFolderPath(REFKNOWNFOLDERID rfid) {
        PWSTR pszPath = NULL;
        std::string path = "";
        if (SUCCEEDED(SHGetKnownFolderPath(rfid, 0, NULL, &pszPath))) {
            int size = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, NULL, 0, NULL, NULL);
            if (size > 0) {
                std::vector<char> buf(size);
                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, buf.data(), size, NULL, NULL);
                path = buf.data();
            }
            CoTaskMemFree(pszPath);
        }
        return path;
    }

    std::wstring GetKnownFolderPathW(REFKNOWNFOLDERID rfid) {
        PWSTR pszPath = NULL;
        HRESULT hr = SHGetKnownFolderPath(rfid, 0, NULL, &pszPath);
        if (SUCCEEDED(hr)) {
            std::wstring path(pszPath);
            CoTaskMemFree(pszPath);
            return path;
        }
        return L"";
    }

    void SmoothShowWindow(HWND hwnd) {
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
        ShowWindow(hwnd, SW_SHOW);
        RedrawWindow(hwnd, NULL, NULL, RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
        for (int i=0; i<=255; i+=51) {
            SetLayeredWindowAttributes(hwnd, 0, (BYTE)i, LWA_ALPHA);
            UpdateWindow(hwnd);
            Sleep(10);
        }
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }
} // namespace Common