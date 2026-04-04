#include "common.hpp"

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

std::wstring ConvertToWide(const std::string& str) {
    if (str.empty()) return L"";
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
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