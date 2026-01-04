#include "common.hpp"
#include "vscodelauncher.hpp"

static HWND hLauncherWindow = NULL;
static HWND hEdit = NULL;
static HWND hListBox = NULL;
static HWND hPathLabel = NULL;
static HFONT hGlobalFont = NULL;
static HFONT hSmallFont = NULL;

static const size_t maxPathsN = 5;
static const int maxSubFolderDepth = 5;
static std::vector<std::string> currentMatches;
static std::vector<std::string> history;
static std::vector<std::string> allCrawledFolders;
static int pendingIndex = -1;

static std::atomic<bool> isScanning(false);
static std::mutex crawlMutex;

static std::string GetEnvVar(const std::string& key) {
    char buffer[MAX_PATH];
    DWORD result = GetEnvironmentVariableA(key.c_str(), buffer, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
        return std::string(buffer);
    }
    return "";
}

static std::string FindVSCodeExecutable() {
    std::string localAppData = GetEnvVar("LOCALAPPDATA");
    std::string programFiles = GetEnvVar("ProgramFiles");

    std::vector<std::string> searchPaths = {
        std::string(localAppData) + "\\Programs\\Microsoft VS Code\\bin\\code.exe",
        "C:\\Program Files\\Microsoft VS Code\\bin\\code.exe",
        "C:\\Program Files (x86)\\Microsoft VS Code\\bin\\code.exe",
        std::string(localAppData) + "\\Programs\\Microsoft VS Code\\bin\\code.cmd",
        "C:\\Program Files\\Microsoft VS Code\\bin\\code.cmd",
        "C:\\Program Files (x86)\\Microsoft VS Code\\bin\\code.cmd"
    };

    for (const auto& path : searchPaths) {
        if (fs::exists(path)) return path;
    }

    return "";
}

static void LoadHistory() {
    char exeFilePath[MAX_PATH];
    GetModuleFileNameA(NULL, exeFilePath, MAX_PATH);
    std::string path(exeFilePath);
    std::string historyFilePath = path.substr(0, path.find_last_of("\\/")) + "\\history.txt";
    std::ifstream historyFile(historyFilePath);
    if (historyFile.is_open()) {
        history.clear();
        std::string line;
        while (std::getline(historyFile, line)) {
            if (!line.empty()) {
                history.push_back(line);
            }
        }
        historyFile.close();
    }
}

void SaveHistory() {
    char exeFilePath[MAX_PATH];
    GetModuleFileNameA(NULL, exeFilePath, MAX_PATH);
    std::string path(exeFilePath);
    std::string historyFilePath = path.substr(0, path.find_last_of("\\/")) + "\\history.txt";

    std::ofstream file(historyFilePath, std::ios::trunc);
    if (file.is_open()) {
        for (const auto& entry : history) {
            file << entry << "\n";
        }
        file.close();
    }
}

void AddToHistory(const std::string& newPath) {
    auto it = std::find(history.begin(), history.end(), newPath);
    if (it != history.end()) {
        history.erase(it);
    }
    history.insert(history.begin(), newPath);
    if (history.size() > 50) {
        history.pop_back();
    }
    SaveHistory();
}

static void ScanDirectory(const std::string& path, std::vector<std::string>& results, int depth) {
    if (depth > maxSubFolderDepth) {
        return;
    }

    std::string searchPath = path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&  !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                    if (strcmp(fd.cFileName, "node_modules") == 0 || 
                        strcmp(fd.cFileName, ".git") == 0 || 
                        strcmp(fd.cFileName, "bin") == 0 || 
                        strcmp(fd.cFileName, "obj") == 0) continue;
                    std::string fullPath = path + "\\" + fd.cFileName;
                    results.push_back(fullPath);
                    ScanDirectory(fullPath, results, depth + 1);
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void BackgroundCrawl() {
    if (isScanning.exchange(true)) {
        return;
    }

    std::thread worker([]() {
        std::vector<std::string> tempFolders;
        KNOWNFOLDERID roots[] = { FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Downloads };
        for (int i = 0; i < 3; i++) {
            PWSTR widePath = NULL;
            if (SUCCEEDED(SHGetKnownFolderPath(roots[i], 0, NULL, &widePath))) {
                char rootPath[MAX_PATH];
                size_t convertedChars = 0;
                wcstombs_s(&convertedChars, rootPath, MAX_PATH, widePath, _TRUNCATE);
                ScanDirectory(rootPath, tempFolders, 0);
                CoTaskMemFree(widePath);
            }
        }
        {
            std::lock_guard<std::mutex> lock(crawlMutex);
            allCrawledFolders.swap(tempFolders); 
        }

        isScanning = false;
    });
    worker.detach();
}

static void RefreshMatches(std::string input) {    
    currentMatches.clear();
    SendMessage(hListBox, LB_RESETCONTENT, 0, 0);

    std::string lowerInput = input;
    std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);

    auto addMatch = [&](const std::string& path) {
        currentMatches.push_back(path);
        const char* displayName = PathFindFileNameA(path.c_str());
        SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)displayName);
    };

    if (input.empty()) {
        for (size_t i = 0; i < history.size() && i < maxPathsN; ++i) {
            addMatch(history[i]);
        }
    } else {
        for (const auto& path : history) {
            if (currentMatches.size() >= maxPathsN) break;

            std::string lowerPath = path;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
            if (lowerPath.find(lowerInput) != std::string::npos) {
                addMatch(path);
            }
        }
        std::lock_guard<std::mutex> lock(crawlMutex);
        for (const auto& path : allCrawledFolders) {
            if (currentMatches.size() >= maxPathsN) break;
            
            if (std::find(history.begin(), history.end(), path) != history.end()) continue;
            std::string lowerPath = path;
            std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
            if (lowerPath.find(lowerInput) != std::string::npos) {
                addMatch(path);
            }
        }
    }

    if (!currentMatches.empty()) {
        SendMessage(hListBox, LB_SETCURSEL, 0, 0);
        SetWindowTextA(hPathLabel, currentMatches[0].c_str());
    } else {
        if (isScanning) {
            SetWindowTextA(hPathLabel, "Scanning directories... please wait.");
        } else {
            SetWindowTextA(hPathLabel, input.empty() ? "" : "No matches found.");
        }
    }

    InvalidateRect(hListBox, NULL, TRUE);
    UpdateWindow(hListBox);
}

void InitializePaths() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    LoadHistory();
    BackgroundCrawl();
    RefreshMatches("");
}

static void ApplyScaledFonts(int winHeight) {
    if (hGlobalFont) {
        DeleteObject(hGlobalFont);
        hGlobalFont = NULL;
    }
    if (hSmallFont) {
        DeleteObject(hSmallFont);
        hSmallFont = NULL;
    }

    int fontSize = winHeight * 0.06;
    
    hGlobalFont = CreateFontA(
        -fontSize,
        0, 0, 0,
        FW_MEDIUM,
        FALSE, FALSE, FALSE, 
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Consolas"
    );
    
    hSmallFont = CreateFontA(
        -fontSize * 0.8,
        0, 0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        "Consolas"
    );

    SendMessage(hEdit,      WM_SETFONT, (WPARAM)hGlobalFont, TRUE);
    SendMessage(hListBox,   WM_SETFONT, (WPARAM)hGlobalFont, TRUE);    
    SendMessage(hPathLabel, WM_SETFONT, (WPARAM)hSmallFont,  TRUE);
}

static void LaunchSelectedPath(int selected) {
    std::string vsCodeExe = FindVSCodeExecutable();
    if (vsCodeExe.empty()) {
        MessageBoxA(hLauncherWindow, "VS Code not found in standard paths!", "Error", MB_ICONERROR);
    } else {
        AddToHistory(currentMatches[selected]);
        std::string  commandLine = "\"" + vsCodeExe + "\" \"" + currentMatches[selected] + "\"";
        STARTUPINFOA si {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        if (CreateProcessA(NULL, (LPSTR)commandLine.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

static LRESULT CALLBACK LauncherWindProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(30, 30, 30));
            static HBRUSH hbrEdit = CreateSolidBrush(RGB(30, 30, 30));
            return (INT_PTR)hbrEdit;
        }
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(220, 220, 220));
            SetBkColor(hdc, RGB(45, 45, 45));
            static HBRUSH hbrList = CreateSolidBrush(RGB(45, 45, 45));
            return (INT_PTR)hbrList;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(150, 150, 150));
            SetBkColor(hdc, RGB(30, 30, 30));
            static HBRUSH hbrStatic = CreateSolidBrush(RGB(30, 30, 30));
            return (INT_PTR)hbrStatic;
        }
        case WM_DRAWITEM: {
            PDRAWITEMSTRUCT pdis = (PDRAWITEMSTRUCT)lParam;
            if (pdis->itemID == (UINT)-1) break;
            COLORREF bgColor;
            COLORREF textColor = RGB(220, 220, 220);

            if (pdis->itemState && ODS_SELECTED) {
                bgColor = RGB(80, 80, 80); 
                textColor = RGB(255, 255, 255);
            } else {
                bgColor = RGB(45, 45, 45);
            }

            HBRUSH hbrush = CreateSolidBrush(bgColor);
            FillRect(pdis->hDC, &pdis->rcItem, hbrush);
            DeleteObject(hbrush);

            char buffer[256];
            SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)buffer);
            SetBkMode(pdis->hDC, textColor);
            RECT textRect = pdis->rcItem;
            textRect.left += 5;
            DrawTextA(pdis->hDC, buffer, -1, &textRect, DT_SINGLELINE | DT_VCENTER);

            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE) {
                char buffer[256];
                GetWindowTextA(hEdit, buffer, 256);
                RefreshMatches(std::string(buffer));
                
                int count = (int)SendMessage(hListBox, LB_GETCOUNT, 0, 0);
                if (buffer[0] == '\0' || count <= 0) {
                    SetWindowTextA(hPathLabel, "");
                } else {
                    int sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                        SetWindowTextA(hPathLabel, currentMatches[sel].c_str());
                    }
                }
            }
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY: {
            if (hGlobalFont) {
                DeleteObject(hGlobalFont);
                hGlobalFont = NULL;
            }
            if (hSmallFont) {
                DeleteObject(hSmallFont);
                hSmallFont = NULL;
            }
            hLauncherWindow = NULL;
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubClass*/, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_KEYDOWN) {
        switch (wParam) {
            case VK_RETURN: {
                if (!currentMatches.empty()) {
                    int sel = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                        LaunchSelectedPath(sel);
                    }
                    DestroyWindow(hLauncherWindow);
                    return 0;
                }
                break;
            }
            case VK_DOWN:
            case VK_UP: {
                int count  = (int)SendMessage(hListBox, LB_GETCOUNT, 0, 0);
                int curSel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                if (count > 0) {
                    int nextSel = (wParam == VK_DOWN)
                        ? (curSel < count -1 ? curSel + 1 : 0)
                        : (curSel > 0        ? curSel - 1: count - 1);
                    SendMessage(hListBox, LB_SETCURSEL, nextSel, 0);
                    if (nextSel >= 0 && nextSel < (int)currentMatches.size()) {
                        SetWindowTextA(hPathLabel, currentMatches[nextSel].c_str());
                    }
                }
                break;
            }
            case VK_ESCAPE: {
                DestroyWindow(hLauncherWindow);
                break;
            }
        }
        return 0;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK ListBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubClass*/, DWORD_PTR /*dwRefData*/) {
    switch (uMsg) {
        case WM_MOUSEMOVE: {
            LRESULT result = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            if (HIWORD(result) == 0) {
                int hoveredIndex = LOWORD(result);
                int currentSel = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                if (hoveredIndex != currentSel && hoveredIndex != pendingIndex && hoveredIndex < (int)currentMatches.size()) {
                    pendingIndex = hoveredIndex;
                    KillTimer(hwnd, 1);
                    SetTimer(hwnd, 1, 50, NULL);
                }
            }else {
                KillTimer(hwnd, 1);
                pendingIndex = -1;
            }
            break;
        }
        case WM_TIMER: {
            if (wParam == 1) {
                KillTimer(hwnd, 1);
                if (pendingIndex != -1) {
                    SendMessage(hwnd, LB_SETCURSEL, pendingIndex, 0);
                    SetWindowTextA(hPathLabel, currentMatches[pendingIndex].c_str());
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int sel = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                LaunchSelectedPath(sel);
            }
            DestroyWindow(hLauncherWindow);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

bool ShowLauncher() {
    bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
    bool alt  = GetAsyncKeyState(VK_MENU)    & 0x8000;
    if (!(ctrl && alt)) {
        return false;
    }

    if (hLauncherWindow != NULL) {
        SetForegroundWindow(hLauncherWindow);
        return true;
    }

    WNDCLASSA wndClass {};
    wndClass.lpfnWndProc = LauncherWindProc;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.lpszClassName = "VSCodeLauncher";
    wndClass.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
    RegisterClassA(&wndClass);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int winW = screenW * 0.50;
    int winH = screenH * 0.40;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 3;

    DWORD dwStyle = WS_POPUP | WS_VISIBLE;
    hLauncherWindow = CreateWindowExA(
        WS_EX_TOPMOST,
        "VSCodeLauncher",
        NULL,
        dwStyle,
        winX, winY, winW , winH,
        NULL, NULL,
        wndClass.hInstance,
        NULL
    );

    int margin     = winW * 0.02;
    int currentY   = margin;
    int innerWidth = winW - (margin * 2);

    dwStyle = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL;
    int editH = winH * 0.12;
    hEdit = CreateWindowExA(0, "EDIT", "", dwStyle, margin, currentY, innerWidth, editH, hLauncherWindow, NULL, wndClass.hInstance, NULL);
    currentY += editH + (margin / 2);

    dwStyle = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED;
    int pathH = winH * 0.10;
    int listH = winH - currentY - pathH - (margin * 2);
    hListBox = CreateWindowExA(0, "LISTBOX", NULL, dwStyle, margin, currentY, innerWidth, listH, hLauncherWindow, NULL, wndClass.hInstance, NULL);
    currentY += listH + (margin / 2);

    dwStyle = WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP;
    hPathLabel = CreateWindowExA(0, "STATIC", "",  dwStyle, margin, currentY, innerWidth, pathH, hLauncherWindow, NULL, wndClass.hInstance, NULL);
 
    ApplyScaledFonts(winH);

    SendMessage(hListBox, LB_SETITEMHEIGHT, 0, (LPARAM)(winH * 0.12));

    int cornerRadius = winH * 0.06;
    HRGN hMainRgn = CreateRoundRectRgn(0, 0, winW, winH, cornerRadius, cornerRadius);
    SetWindowRgn(hLauncherWindow, hMainRgn, TRUE);

    SetWindowSubclass(hEdit, EditSubclassProc, 0, 0);
    SetWindowSubclass(hListBox, ListBoxSubclassProc, 0, 0);
    
    LoadHistory();
    BackgroundCrawl();
    RefreshMatches("");

    ShowWindow(hLauncherWindow, SW_SHOW);
    UpdateWindow(hLauncherWindow);

    HWND hForce = GetForegroundWindow();
    DWORD foreThread = GetWindowThreadProcessId(hForce, NULL);
    DWORD appThread = GetCurrentThreadId();

    if (foreThread != appThread) {
        AttachThreadInput(foreThread, appThread, TRUE);
        SetForegroundWindow(hLauncherWindow);
        SetFocus(hEdit);
        AttachThreadInput(foreThread, appThread, FALSE);
    } else {
        SetForegroundWindow(hLauncherWindow);
        SetFocus(hEdit);
    }

    return true;
}