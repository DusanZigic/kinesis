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