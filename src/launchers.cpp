#include "common.hpp"
#include "launchers.hpp"

static LauncherContext ctxVSCode;
static LauncherContext ctxWSL;
static LauncherContext* activeCtx = nullptr;

static bool launcherClassRegistered = false;
static HWND hLauncherWindow = NULL;
static HWND hEdit = NULL;
static HWND hListBox = NULL;
static HWND hPathLabel = NULL;

static HBRUSH hLauncherBgBrush = NULL;
static HBRUSH hEditBgBrush = NULL;
static HBRUSH hListBoxBgBrush = NULL;
static HFONT hGlobalFont = NULL;
static HFONT hSmallFont = NULL;

static std::string historyBaseDir = "";
static const int maxSubFolderDepth = 5;
static const int maxPathsN = 5;
static std::vector<std::string> crawlerRootPaths;
static std::vector<std::string> currentMatches;
static std::vector<std::string> allCrawledFolders;
static int pendingIndex = -1;

static std::atomic<bool> isScanning(false);
static std::mutex crawlMutex;

static std::string GetEnv(const std::string& var) {
    char buf[MAX_PATH];
    DWORD res = GetEnvironmentVariableA(var.c_str(), buf, MAX_PATH);
    return (res > 0 && res < MAX_PATH) ? std::string(buf) : "";
}

static void FindVSCode(LauncherContext& launcherCtx) {
    std::vector<std::string> searchBases = {
        GetEnv("LOCALAPPDATA") + "\\Programs\\Microsoft VS Code",
        GetEnv("ProgramFiles") + "\\Microsoft VS Code",
        "C:\\Program Files\\Microsoft VS Code"
    };
    for (const auto& base : searchBases) {
        if (base.length() < 5) continue;
        fs::path rootPath(base);
        fs::path exePath = rootPath / "Code.exe";
        fs::path cliPath = rootPath / "resources" / "app" / "out" / "cli.js";
        if (fs::exists(exePath) && fs::exists(cliPath)) {
            launcherCtx.executablePath = exePath.u8string();
            launcherCtx.cliPath = cliPath.u8string();
            launcherCtx.isEngineFound = true;
            return;
        }
    }
    launcherCtx.isEngineFound = false;
}

static void FindWSL(LauncherContext& launcherCtx) {
    char pathBuf[MAX_PATH];
    if (SearchPathA(NULL, "wsl", ".exe", MAX_PATH, pathBuf, NULL) > 0) {
        launcherCtx.executablePath = std::string(pathBuf);
        launcherCtx.isEngineFound = true;
        return;
    }
    launcherCtx.isEngineFound = false;
}

static std::string GetKnownFolderPath(REFKNOWNFOLDERID rfid) {
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

static void SetUpStoragePath() {
    std::string baseAppPath = GetKnownFolderPath(FOLDERID_LocalAppData);
    if (!baseAppPath.empty()) {
        std::string kinesisPath = baseAppPath + "\\Kinesis";
        std::string historyPath = kinesisPath + "\\History";
        CreateDirectoryA(kinesisPath.c_str(), NULL);
        CreateDirectoryA(historyPath.c_str(), NULL);
        historyBaseDir = historyPath;
    }
}

static void LoadHistory(LauncherContext& ctx) {
    std::string fullPath = historyBaseDir + "\\" + ctx.historyFileName;
    std::ifstream file(fullPath);
    if (file.is_open()) {
        ctx.history.clear();
        std::string line;
        while (std::getline(file, line)) if (!line.empty()) ctx.history.push_back(line);
    }
}

static void SaveHistory(const LauncherContext& ctx) {
    std::string fullPath = historyBaseDir + "\\" + ctx.historyFileName;
    std::ofstream file(fullPath, std::ios::trunc);
    if (file.is_open()) {
        for (const auto& entry : ctx.history) file << entry << "\n";
    }
}

static void AddToHistory(const std::string& newPath) {
    auto& history = activeCtx->history;
    auto it = std::find(history.begin(), history.end(), newPath);
    if (it != history.end()) {
        history.erase(it);
    }
    history.insert(history.begin(), newPath);
    if (history.size() > 50) {
        history.pop_back();
    }
    SaveHistory(*activeCtx);
}

static const std::vector<std::string> GetOneDrivePaths() {
    std::vector<std::string> paths;
    HKEY hKey;

    const char* subkey = "Software\\Microsoft\\OneDrive\\Accounts";

    if (RegOpenKeyExA(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char accountName[256];
        DWORD nameSize = sizeof(accountName);

        for (DWORD i = 0; RegEnumKeyExA(hKey, i, accountName, &nameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; ++i) {
            HKEY hAccountKey;
            if (RegOpenKeyExA(hKey, accountName, 0, KEY_READ, &hAccountKey) == ERROR_SUCCESS) {
                char path[MAX_PATH];
                DWORD pathSize = sizeof(path);
                if (RegQueryValueExA(hAccountKey, "UserFolder", NULL, NULL, (LPBYTE)path, &pathSize) == ERROR_SUCCESS) {
                    if (fs::exists(path)) {
                        paths.push_back(std::string(path));
                    }
                }
                RegCloseKey(hAccountKey);
            }
            nameSize = sizeof(accountName);
        }
        RegCloseKey(hKey);
    }
    
    if (paths.empty()) {
        std::string personal = GetEnv("OneDrive");
        if (!personal.empty() && fs::exists(personal)) paths.push_back(personal);
        
        std::string business = GetEnv("OneDriveCommercial");
        if (!business.empty() && fs::exists(business)) paths.push_back(business);
    }

    return paths;
}

static std::vector<std::string> GetWSLDistros() {
    std::vector<std::string> distros;
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return distros;
    }

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    char cmd[] = "wsl.exe -l -q";
    if (CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWrite);
        
        std::vector<char> rawBuffer;
        char chunk[1024];
        DWORD bytesRead;
        while (ReadFile(hRead, chunk, sizeof(chunk), &bytesRead, NULL) && bytesRead > 0) {
            rawBuffer.insert(rawBuffer.end(), chunk, chunk + bytesRead);
        }

        std::string currentDistro;
        for (size_t i = 0; i < rawBuffer.size(); i += 2) {
            char c = rawBuffer[i];
            if (c == '\r' || c == '\n' || c == '\0') {
                if (!currentDistro.empty()) {
                    distros.push_back(currentDistro);
                    currentDistro.clear();
                }
            } else {
                currentDistro += c;
            }
        }
        if (!currentDistro.empty()) {
            distros.push_back(currentDistro);
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(hRead);
    return distros;
}

static void InitializeCrawlerRootPaths() {
    KNOWNFOLDERID roots[] = { FOLDERID_Documents, FOLDERID_Desktop, FOLDERID_Downloads };
    for (const auto& id : roots) {
        std::string p = GetKnownFolderPath(id);
        if (!p.empty()) crawlerRootPaths.push_back(p);
    }

    std::vector<std::string> oneDrivePaths = GetOneDrivePaths();
    for (const auto& path : oneDrivePaths) {
        if (std::find(crawlerRootPaths.begin(), crawlerRootPaths.end(), path) == crawlerRootPaths.end()) {
            crawlerRootPaths.push_back(path);
        }
    }

    std::vector<std::string> distros = GetWSLDistros();
    std::error_code ec;
    for (const std::string& distro : distros) {
        std::string basePaths[] = { 
            "\\\\wsl.localhost\\" + distro + "\\home",
            "\\\\wsl$\\" + distro + "\\home" 
        };
        for (const std::string& homeBase : basePaths) {
            if (fs::exists(homeBase, ec)) {
                for (auto const& userEntry : fs::directory_iterator(homeBase, ec)) {
                    if (!ec && userEntry.is_directory(ec)) {
                        crawlerRootPaths.push_back(userEntry.path().string());
                    }
                }
                break;
            }
        }
    }
}

static void ScanDirectory(const std::string& path, std::vector<std::string>& results, int depth) {
    if (depth > maxSubFolderDepth) return;

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA((path + "\\*").c_str(), &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                continue;
            }
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) ||
                (fd.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE)) {
                continue;
            }
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
                continue;
            }
            if (strcmp(fd.cFileName, "node_modules") == 0 ||
                strcmp(fd.cFileName, ".git") == 0 ||
                strcmp(fd.cFileName, "bin") == 0 ||
                strcmp(fd.cFileName, ".vs") == 0 ||
                strcmp(fd.cFileName, "obj") == 0) {
                    continue;
            }
            std::string fullPath = path + "\\" + fd.cFileName;
            results.push_back(fullPath);
            ScanDirectory(fullPath, results, depth + 1);
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void BackgroundCrawl() {
    if (isScanning.exchange(true)) return;
    std::thread([]() {
        std::vector<std::string> tempFolders;
        for (const auto& root : crawlerRootPaths) ScanDirectory(root, tempFolders, 0);
        {
            std::lock_guard<std::mutex> lock(crawlMutex);
            allCrawledFolders.swap(tempFolders);
        }
        isScanning = false;
    }).detach();
}

static std::string ExtractDistroFromPath(const std::string& path) {
    std::string distroName = "";
    std::string prefix = "\\\\wsl.localhost\\";
    size_t start = path.find(prefix);
    if (start != std::string::npos) {
        start += prefix.length();
        size_t end = path.find('\\', start);
        if (end != std::string::npos) {
            distroName = path.substr(start, end - start);
            return distroName;
        }
    } else {
        prefix = "\\\\wsl$\\";
        start = path.find(prefix);
        if (start != std::string::npos) {
            start += prefix.length();
            size_t end = path.find('\\', start);
            if (end != std::string::npos) {
                distroName = path.substr(start, end - start);
                return distroName;
            }
        }
    }

    return "";
}

static std::string ResolveWSLPath(const std::string& windowsPath, const std::string& distroName) {
    if (!distroName.empty()) {
        std::string searchKey = "\\" + distroName;
        size_t pos = windowsPath.find(searchKey);
        if (pos != std::string::npos) {
            std::string linuxPath = windowsPath.substr(pos + searchKey.length());
            std::replace(linuxPath.begin(), linuxPath.end(), '\\', '/');
            return linuxPath.empty() ? "/" : linuxPath;
        }
    } else {
        if (windowsPath.length() >= 3 && windowsPath[1] == ':' && windowsPath[2] == '\\') {
            std::string linuxPath = windowsPath;
            char driveLetter = tolower(linuxPath[0]);
            linuxPath = "/mnt/" + std::string(1, driveLetter) + linuxPath.substr(2);
            std::replace(linuxPath.begin(), linuxPath.end(), '\\', '/');
            return linuxPath.empty() ? "/" : linuxPath;
        }
    }
    return "/";
}

static void ExecuteLaunch(int selected) {
    std::string path = currentMatches[selected];
    AddToHistory(path);

    if (activeCtx->type == LauncherMode::VSCode) {
        SetEnvironmentVariableA("ELECTRON_RUN_AS_NODE", "1");
        std::string cmd = "\"" + activeCtx->executablePath + "\" \"" + activeCtx->cliPath + "\" \"" + path + "\"";
        std::vector<char> cmdBuffer(cmd.begin(), cmd.end());
        cmdBuffer.push_back('\0');
        STARTUPINFOA si {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi;
        if (CreateProcessA(activeCtx->executablePath.c_str(), cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        SetEnvironmentVariableA("ELECTRON_RUN_AS_NODE", NULL);
    
    } else if (activeCtx->type == LauncherMode::WSL) {
        std::string distroName = ExtractDistroFromPath(path);
        std::string linuxPath = ResolveWSLPath(path, distroName);
        std::string cmdLine = "wsl.exe";
        if (!distroName.empty()) {
            cmdLine += " -d " + distroName;
        }   
        cmdLine += " --cd \"" + linuxPath + "\"";
        std::vector<char> cmdBuffer(cmdLine.begin(), cmdLine.end());
        cmdBuffer.push_back('\0');
        
        STARTUPINFOA si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_SHOW;
        PROCESS_INFORMATION pi {};
        if (CreateProcessA(activeCtx->executablePath.c_str(), cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
}

static void RefreshMatches(std::string input) {
    if (!activeCtx->isEngineFound) {
        SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
        SetWindowTextA(hPathLabel, "ERROR: executable not found! Check your installation.");
        return;
    }

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
        for (size_t i = 0; i < activeCtx->history.size() && i < maxPathsN; ++i) {
            addMatch(activeCtx->history[i]);
        }
    } else {
        for (const auto& path : activeCtx->history) {
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
            
            if (std::find(activeCtx->history.begin(), activeCtx->history.end(), path) != activeCtx->history.end()) continue;
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
            SetWindowTextA(hPathLabel, activeCtx->placeholder.c_str());
        } else {
            SetWindowTextA(hPathLabel, input.empty() ? "" : "No matches found.");
        }
    }

    InvalidateRect(hListBox, NULL, FALSE);
    UpdateWindow(hListBox);
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (uMsg == WM_KEYDOWN) {
        if (wParam == VK_RETURN && !currentMatches.empty()) {
            int sel = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                ExecuteLaunch(sel);
            }
            DestroyWindow(hLauncherWindow);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            DestroyWindow(hLauncherWindow);
            return 0;
        }
        if (wParam == VK_DOWN || wParam == VK_UP) {
            int count = SendMessage(hListBox, LB_GETCOUNT, 0, 0);
            int cur = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            int next = (wParam == VK_DOWN) ? (cur + 1) % count : (cur - 1 + count) % count;
            SendMessage(hListBox, LB_SETCURSEL, next, 0);
            SetWindowTextA(hPathLabel, currentMatches[next].c_str());
            InvalidateRect(hListBox, NULL, FALSE);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK ListBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    switch (uMsg) {
        case WM_ERASEBKGND: {
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rcList;
            GetClientRect(hwnd, &rcList);

            HWND hParent = GetParent(hwnd);
            RECT rcMain;
            GetClientRect(hParent, &rcMain);
            
            float aspectRatio = (float)activeCtx->logoImage->GetWidth() / activeCtx->logoImage->GetHeight();
            int imgHeight = (int)(rcMain.bottom * 0.55);
            int imgWidth = (int)(imgHeight * aspectRatio);

            POINT ptListOffset = {0, 0};
            MapWindowPoints(hwnd, hParent, &ptListOffset, 1);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcList.right, rcList.bottom);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            FillRect(memDC, &rcList, hLauncherBgBrush);

            if (activeCtx->logoImage) {
                Gdiplus::Graphics graphics(memDC);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
                graphics.SetPageUnit(Gdiplus::UnitPixel);

                int mainCenterX = rcMain.right / 2;
                int mainCenterY = rcMain.bottom / 2;

                int logoXInMain = mainCenterX - (imgWidth / 2);
                int logoYInMain = mainCenterY - (imgHeight / 2);

                int finalX = logoXInMain - ptListOffset.x;
                int finalY = logoYInMain - ptListOffset.y;

                static Gdiplus::ImageAttributes attr;
                static bool attrInit = false;
                if (!attrInit) {
                    Gdiplus::ColorMatrix matrix = {1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,0.12f,0, 0,0,0,0,1};
                    attr.SetColorMatrix(&matrix);
                    attrInit = true;
                }

                graphics.DrawImage(
                    activeCtx->logoImage,
                    Gdiplus::Rect(finalX, finalY, imgWidth, imgHeight),
                    0, 0,
                    activeCtx->logoImage->GetWidth(),
                    activeCtx->logoImage->GetHeight(),
                    Gdiplus::UnitPixel,
                    &attr
                );
            }

            SendMessage(hwnd, WM_PRINTCLIENT, (WPARAM)memDC, PRF_CLIENT);

            BitBlt(hdc, 0, 0, rcList.right, rcList.bottom, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);            
            return 0;
        }
        case WM_MOUSEMOVE: {
            LRESULT result = SendMessage(hwnd, LB_ITEMFROMPOINT, 0, lParam);
            if (HIWORD(result) == 0) {
                int hoveredIndex = LOWORD(result);
                int currentSel = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
                if (hoveredIndex != currentSel && hoveredIndex != pendingIndex && hoveredIndex < (int)currentMatches.size()) {
                    pendingIndex = hoveredIndex;
                    KillTimer(hwnd, 1);
                    SetTimer(hwnd, 1, 25, NULL);
                }
            } else {
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
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            int sel = (int)SendMessage(hwnd, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                ExecuteLaunch(sel);
            }
            DestroyWindow(hLauncherWindow);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

static LRESULT CALLBACK LauncherWindProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (INT_PTR)hEditBgBrush;
        }
        case WM_CTLCOLORLISTBOX: {
            SetBkMode((HDC)wParam, TRANSPARENT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(150, 150, 150));
            SetBkColor(hdc, RGB(30, 30, 30));
            return (INT_PTR)hEditBgBrush;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            FillRect(hdc, &rc, hLauncherBgBrush);

            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_DRAWITEM: {
            PDRAWITEMSTRUCT pdis = (PDRAWITEMSTRUCT)lParam;
            if (pdis->itemID == (UINT)-1) return TRUE;
            bool sel = pdis->itemState & ODS_SELECTED;
            if (sel) {
                Gdiplus::Graphics g(pdis->hDC);
                Gdiplus::SolidBrush b(Gdiplus::Color(128, 100, 100, 100));
                g.FillRectangle(
                    &b,
                    (int)pdis->rcItem.left,
                    (int)pdis->rcItem.top,
                    pdis->rcItem.right - pdis->rcItem.left,
                    pdis->rcItem.bottom - pdis->rcItem.top
                );
            }
            char buffer[256];
            SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)buffer);
            SetTextColor(pdis->hDC, sel ? RGB(255, 255, 255) : RGB(200, 200, 200));

            SetBkMode(pdis->hDC, TRANSPARENT);
            
            RECT textRect = pdis->rcItem;
            textRect.left += 15;
            HGDIOBJ oldFont = SelectObject(pdis->hDC, hGlobalFont);
            DrawTextA(pdis->hDC, buffer, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(pdis->hDC, oldFont);
            
            return TRUE;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == EN_CHANGE) {
                char buffer[256];
                GetWindowTextA(hEdit, buffer, 256);
                RefreshMatches(buffer);
            }
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hwnd);
             }
             return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_DESTROY: {
            hLauncherWindow = NULL;
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static Gdiplus::Image* LoadImageFromResource(int resourceID) {
    HRSRC hRes = FindResourceA(NULL, MAKEINTRESOURCEA(resourceID), (LPCSTR)RT_RCDATA);
    if (!hRes) return nullptr;

    DWORD resSize = SizeofResource(NULL, hRes);
    HGLOBAL hResData = LoadResource(NULL, hRes);
    if (!hResData) return nullptr;

    void* pRes = LockResource(hResData);
    if (!pRes) return nullptr;

    IStream* pStream = SHCreateMemStream((const BYTE*)pRes, resSize);
    if (!pStream) return nullptr;

    Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
    
    pStream->Release();

    if (img && img->GetLastStatus() != Gdiplus::Ok) {
        delete img;
        return nullptr;
    }

    return img;
}

void InitializeLauncher() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetUpStoragePath();

    ctxVSCode.type = LauncherMode::VSCode;
    ctxVSCode.historyFileName = "vscodelauncher_history.txt";
    ctxVSCode.logoResourceID = 101;
    FindVSCode(ctxVSCode);
    ctxVSCode.logoImage = LoadImageFromResource(ctxVSCode.logoResourceID);
    ctxVSCode.placeholder = "Search for VS Code projects...";
    LoadHistory(ctxVSCode);

    ctxWSL.type = LauncherMode::WSL;
    ctxWSL.historyFileName = "wsllauncher_history.txt";
    ctxWSL.logoResourceID = 102;
    FindWSL(ctxWSL);
    ctxWSL.logoImage = LoadImageFromResource(ctxWSL.logoResourceID);
    ctxWSL.placeholder = "Search for WSL directories...";
    LoadHistory(ctxWSL);

    hLauncherBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    hEditBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    hListBoxBgBrush = CreateSolidBrush(RGB(45, 45, 45));

    InitializeCrawlerRootPaths();
    BackgroundCrawl();
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

void ShowLauncher(LauncherMode mode) {
    if (hLauncherWindow) return;

    switch (mode) {
        case LauncherMode::VSCode:
            activeCtx = &ctxVSCode;
            break;
        case LauncherMode::WSL:
            activeCtx = &ctxWSL;
            break;
        default:
            return;
    }

    if (!launcherClassRegistered) {
        WNDCLASSA wc {};
        wc.lpfnWndProc = LauncherWindProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = "KinesisLauncher";
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassA(&wc);
        launcherClassRegistered = true;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int winW = screenW * 0.50;
    int winH = screenH * 0.40;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 3;

    hLauncherWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "KinesisLauncher",
        NULL,
        WS_POPUP | WS_VISIBLE,
        winX, winY, winW , winH,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    int margin     = winW * 0.02;
    int currentY   = margin;
    int innerWidth = winW - (margin * 2);

    int editH = winH * 0.12;
    hEdit = CreateWindowExA(
        0,
        "EDIT",
        "",
         WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
         margin, currentY, innerWidth, editH,
         hLauncherWindow,
         NULL,
         GetModuleHandle(NULL),
         NULL
    );
    currentY += editH + (margin / 2);

    int pathH = winH * 0.10;
    int listH = winH - currentY - pathH - (margin * 2);
    hListBox = CreateWindowExA(
        0,
        "LISTBOX",
        NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
        margin, currentY, innerWidth, listH,
        hLauncherWindow,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    currentY += listH + (margin / 2);

    hPathLabel = CreateWindowExA(
        0,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
        margin, currentY, innerWidth,
        pathH,
        hLauncherWindow,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!activeCtx->isEngineFound) {
        EnableWindow(hEdit, FALSE);
        SetWindowTextA(hEdit, "");
    }

    ApplyScaledFonts(winH);

    SendMessage(hListBox, LB_SETITEMHEIGHT, 0, (LPARAM)(winH * 0.12));

    int cornerRadius = winH * 0.06;
    HRGN hMainRgn = CreateRoundRectRgn(0, 0, winW, winH, cornerRadius, cornerRadius);
    SetWindowRgn(hLauncherWindow, hMainRgn, TRUE);

    SetWindowSubclass(hEdit, EditSubclassProc, 0, 0);
    SetWindowSubclass(hListBox, ListBoxSubclassProc, 0, 0);

    LoadHistory(*activeCtx);
    BackgroundCrawl();
    RefreshMatches("");

    AllowSetForegroundWindow(ASFW_ANY);
    keybd_event(0xFC, 0, 0, 0);
    keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
    ShowWindow(hLauncherWindow, SW_SHOW);
    SetForegroundWindow(hLauncherWindow);
    SetActiveWindow(hLauncherWindow);
    if (!activeCtx->isEngineFound) {
        SetFocus(hLauncherWindow);
    } else {
        SetFocus(hEdit);
    }
}

void ReleaseLauncherResources() {
    if (ctxVSCode.logoImage) {
        delete ctxVSCode.logoImage;
        ctxVSCode.logoImage = nullptr;
    }
    if (ctxWSL.logoImage) {
        delete ctxWSL.logoImage;
        ctxWSL.logoImage = nullptr;
    }

    if (hGlobalFont) {
        DeleteObject(hGlobalFont);
        hGlobalFont = NULL;
    }
    if (hSmallFont) {
        DeleteObject(hSmallFont);
        hSmallFont = NULL;
    }
    if (hLauncherBgBrush) {
        DeleteObject(hLauncherBgBrush);
        hLauncherBgBrush = nullptr;
    }
    if (hEditBgBrush) {
        DeleteObject(hEditBgBrush);
        hEditBgBrush = nullptr;
    }
}