#include "common.hpp"

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