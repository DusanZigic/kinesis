#include "common.hpp"
#include "vscodelauncher.hpp"

static bool launcherClassRegistered = false;
static HWND hLauncherWindow = NULL;
static HBRUSH hLauncherBgBrush = NULL;
static HWND hEdit = NULL;
static HBRUSH hEditBgBrush = NULL;
static HBRUSH hListBoxBgBrush = NULL;
static HWND hListBox = NULL;
static HWND hPathLabel = NULL;
static HFONT hGlobalFont = NULL;
static HFONT hSmallFont = NULL;

static std::string VSCodeExe = "";
static std::string VSCodeCli = "";
static bool isVSCodeFound = false;

static Gdiplus::Image* vsCodeoBGImage = nullptr;

static const size_t maxPathsN = 5;
static const int maxSubFolderDepth = 5;
static std::vector<std::string> crawlerRootPaths;
static std::vector<std::string> currentMatches;
static std::vector<std::string> history;
static std::vector<std::string> allCrawledFolders;
static int pendingIndex = -1;

static std::atomic<bool> isScanning(false);
static std::mutex crawlMutex;

static std::string GetEnv(const std::string& var) {
    char buf[MAX_PATH];
    DWORD res = GetEnvironmentVariableA(var.c_str(), buf, MAX_PATH);
    return (res > 0 && res < MAX_PATH) ? std::string(buf) : "";
}

static std::string FindVSCodeRoot() {
    std::vector<std::string> searchBases = {
        GetEnv("LOCALAPPDATA") + "\\Programs\\Microsoft VS Code",
        GetEnv("ProgramFiles") + "\\Microsoft VS Code",
        "C:\\Program Files\\Microsoft VS Code",
        "C:\\Program Files (x86)\\Microsoft VS Code"
    };

    for (const auto& base : searchBases) {
        if (base.length() < 5) continue;
        fs::path rootPath(base);
        fs::path exePath = rootPath / "Code.exe";
        fs::path cliPath = rootPath / "resources" / "app" / "out" / "cli.js";
        if (fs::exists(exePath) && fs::exists(cliPath)) {
            return rootPath.string();
        }
    }

    char pathBuf[MAX_PATH];
    if (SearchPathA(NULL, "code", ".cmd", MAX_PATH, pathBuf, NULL)) {
        fs::path cmdPath(pathBuf);
        fs::path rootPath = cmdPath.parent_path().parent_path();
        if (fs::exists(rootPath / "Code.exe")) {
            return rootPath.string();
        }
    }

    return "";
}

static void InitializeVSCodePath() {
    std::string root = FindVSCodeRoot();
    if (!root.empty()) {
        std::string exe = root + "\\Code.exe";
        std::string cli = root + "\\resources\\app\\out\\cli.js";
        if (fs::exists(exe) && fs::exists(cli)) {
            VSCodeExe = exe;
            VSCodeCli = cli;
            isVSCodeFound = true;
        }
    }
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
}

static void ScanDirectory(const std::string& path, std::vector<std::string>& results, int depth) {
    if (depth > maxSubFolderDepth) return;

    std::string searchPath = path + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
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

            if (strcmp(fd.cFileName, ".") != 0 && strcmp(fd.cFileName, "..") != 0) {
                if (strcmp(fd.cFileName, "node_modules") == 0 ||
                    strcmp(fd.cFileName, ".git") == 0 ||
                    strcmp(fd.cFileName, "bin") == 0 ||
                    strcmp(fd.cFileName, ".vs") == 0 ||
                    strcmp(fd.cFileName, "obj") == 0) continue;
                std::string fullPath = path + "\\" + fd.cFileName;
                results.push_back(fullPath);
                ScanDirectory(fullPath, results, depth + 1);
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }
}

static void BackgroundCrawl() {
    if (!isVSCodeFound) return;
    
    if (isScanning.exchange(true)) return;

    std::thread worker([]() {
        std::vector<std::string> tempFolders;

        for (const auto& crawlerRootPath : crawlerRootPaths) {
            ScanDirectory(crawlerRootPath, tempFolders, 0);
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
    if (!isVSCodeFound) {
        SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
        SetWindowTextA(hPathLabel, "ERROR: VS Code executable not found! Check your installation.");
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

    InvalidateRect(hListBox, NULL, FALSE);
    UpdateWindow(hListBox);
}

void InitializeVSCodeLauncher() {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    InitializeVSCodePath();

    LoadHistory();
    InitializeCrawlerRootPaths();
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

static void OpenFolderInVSCode(int selected) {
    if (!isVSCodeFound) {
        MessageBoxA(hLauncherWindow, "Could not find VSCode root folder, executable and/or 'cli.js' file!", "Error", MB_ICONERROR);
        return; 
    }

    AddToHistory(currentMatches[selected]);
    SetEnvironmentVariableA("ELECTRON_RUN_AS_NODE", "1");
    std::string cmd = "\"" + VSCodeExe + "\" \"" + VSCodeCli + "\" \"" + currentMatches[selected] + "\"";
    std::vector<char> cmdBuffer(cmd.begin(), cmd.end());
    cmdBuffer.push_back('\0');
    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    if (CreateProcessA(VSCodeExe.c_str(), cmdBuffer.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    SetEnvironmentVariableA("ELECTRON_RUN_AS_NODE", NULL);
}

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR /*uIdSubClass*/, DWORD_PTR /*dwRefData*/) {
    if (uMsg == WM_KEYDOWN) {
        switch (wParam) {
            case VK_RETURN: {
                if (!currentMatches.empty()) {
                    int sel = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && sel < (int)currentMatches.size()) {
                        OpenFolderInVSCode(sel);
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

                    InvalidateRect(hListBox, NULL, FALSE);

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
            
            float aspectRatio = (float)vsCodeoBGImage->GetWidth() / vsCodeoBGImage->GetHeight();
            int imgHeight = (int)(rcMain.bottom * 0.55);
            int imgWidth = (int)(imgHeight * aspectRatio);

            POINT ptListOffset = {0, 0};
            MapWindowPoints(hwnd, hParent, &ptListOffset, 1);

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcList.right, rcList.bottom);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            FillRect(memDC, &rcList, hLauncherBgBrush);

            if (vsCodeoBGImage) {
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
                    vsCodeoBGImage,
                    Gdiplus::Rect(finalX, finalY, imgWidth, imgHeight),
                    0, 0,
                    vsCodeoBGImage->GetWidth(),
                    vsCodeoBGImage->GetHeight(),
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
                    SetTimer(hwnd, 1, 50, NULL);
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
                OpenFolderInVSCode(sel);
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

            bool isSelected = (pdis->itemState & ODS_SELECTED);

            if (isSelected) {
                Gdiplus::Graphics graphics(pdis->hDC);
                Gdiplus::SolidBrush selBrush(Gdiplus::Color(128, 100, 100, 100));
                graphics.FillRectangle(
                    &selBrush,
                    (int)pdis->rcItem.left,
                    (int)pdis->rcItem.top,
                    (int)(pdis->rcItem.right - pdis->rcItem.left),
                    (int)(pdis->rcItem.bottom - pdis->rcItem.top)
                );
            }
            
            char buffer[256];
            SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)buffer);

            SetTextColor(pdis->hDC, isSelected ? RGB(255, 255, 255) : RGB(200, 200, 200));
            SetBkMode(pdis->hDC, TRANSPARENT);

            RECT textRect = pdis->rcItem;
            textRect.left += 15;

            HGDIOBJ oldFont = SelectObject(pdis->hDC, hGlobalFont);
            DrawTextA(pdis->hDC, buffer, -1, &textRect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            SelectObject(pdis->hDC, oldFont);

            return TRUE;
        }
        case WM_COMMAND: {
            if (lParam == (LPARAM)hListBox) {
                if (HIWORD(wParam) == LBN_SELCHANGE) {
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }

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
                InvalidateRect(hwnd, NULL, TRUE);
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
            RemoveWindowSubclass(hEdit, EditSubclassProc, 0);
            RemoveWindowSubclass(hListBox, ListBoxSubclassProc, 0);
            
            if (hGlobalFont) {
                DeleteObject(hGlobalFont);
                hGlobalFont = NULL;
            }
            if (hSmallFont) {
                DeleteObject(hSmallFont);
                hSmallFont = NULL;
            }
            SetWindowRgn(hwnd, NULL, FALSE);
            hLauncherWindow = NULL;

            if (vsCodeoBGImage) {
                delete vsCodeoBGImage;
                vsCodeoBGImage = nullptr;
            }

            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void RegisterLauncherClassOnce() {
    if (launcherClassRegistered) return;

    hLauncherBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    hEditBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    hListBoxBgBrush = CreateSolidBrush(RGB(45, 45, 45));

    WNDCLASSA wndClass {};
    wndClass.lpfnWndProc = LauncherWindProc;
    wndClass.hInstance = GetModuleHandle(NULL);
    wndClass.lpszClassName = "VSCodeLauncher";
    wndClass.hbrBackground = hLauncherBgBrush;
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wndClass);
    
    launcherClassRegistered = true;
}

void ShowLauncher() {
    if (hLauncherWindow != NULL) {
        SetForegroundWindow(hLauncherWindow);
        return;
    }

    if (vsCodeoBGImage == nullptr) vsCodeoBGImage = LoadImageFromResource(101);

    RegisterLauncherClassOnce();

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int winW = screenW * 0.50;
    int winH = screenH * 0.40;
    int winX = (screenW - winW) / 2;
    int winY = (screenH - winH) / 3;

    hLauncherWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "VSCodeLauncher",
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
    
    if (!isVSCodeFound) {
        EnableWindow(hEdit, FALSE);
        SetWindowTextA(hEdit, "");
    }


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

    AllowSetForegroundWindow(ASFW_ANY);
    keybd_event(0xFC, 0, 0, 0);
    keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
    ShowWindow(hLauncherWindow, SW_SHOW);
    SetForegroundWindow(hLauncherWindow);
    SetActiveWindow(hLauncherWindow);
    if (!isVSCodeFound) {
        SetFocus(hLauncherWindow);
    } else {
        SetFocus(hEdit);
    }
}