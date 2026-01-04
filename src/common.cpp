#include "common.hpp"

std::string GetProcessName(DWORD pid) {
    char name[MAX_PATH] = "<unknown>";
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (hProcess) {
        GetModuleBaseNameA(hProcess, NULL, name, MAX_PATH);
        CloseHandle(hProcess);
    }
    return std::string(name);
}