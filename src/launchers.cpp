#include "common.hpp"
#include "launchers.hpp"

namespace fs = std::filesystem;

namespace Launcher {
    static Context ctxVSCode;
    static Context ctxWSL;
    static Context* activeCtx = nullptr;
    
    static HWND hLauncherWindow = NULL;
    static HWND hEdit = NULL;
    static HWND hListBox = NULL;
    static HWND hPathLabel = NULL;
    
    static bool launcherClassRegistered = false;
    static LayoutMetrics layoutMetrics;

    HBRUSH UIStyle::hBgBrush = nullptr;
    Gdiplus::ImageAttributes* UIStyle::logoAttr = nullptr;
    Gdiplus::Font* UIStyle::mainFont = nullptr;
    Gdiplus::Font* UIStyle::smallFont = nullptr;
    HFONT UIStyle::hWin32MainFont  = nullptr;
    HFONT UIStyle::hWin32SmallFont = nullptr;

    static std::vector<std::string> allCrawledFolders;
    static std::vector<std::string> currentMatches;
    static std::mutex crawlMutex;
    static std::atomic<bool> isScanning(false);
    
    static std::string historyBaseDir = "";
    static std::vector<std::string> crawlerRootPaths;
    static const int maxSubFolderDepth = 5;
    static const int maxPathsN = 5;

    static int pendingIndex = -1;

    static std::string GetEnv(const std::string& var) {
        char buf[MAX_PATH];
        DWORD res = GetEnvironmentVariableA(var.c_str(), buf, MAX_PATH);
        return (res > 0 && res < MAX_PATH) ? std::string(buf) : "";
    }

    static void FindVSCode(Context& ctx) {
        std::vector<std::string> searchBases = {
            GetEnv("LOCALAPPDATA") + "\\Programs\\Microsoft VS Code",
            GetEnv("ProgramFiles") + "\\Microsoft VS Code",
            "C:\\Program Files\\Microsoft VS Code"
        };

        for (const auto& base : searchBases) {
            if (base.length() < 5) continue;
            
            fs::path rootPath(base);
            if (!fs::exists(rootPath)) continue;

            fs::path cmdPath;
            for (auto it = fs::recursive_directory_iterator(rootPath); it != fs::recursive_directory_iterator(); ++it) {
                if (it.depth() > 3) {
                    it.pop();
                    if (it == fs::recursive_directory_iterator()) break;
                    continue;
                }
                if (it->path().filename() == "code.cmd") {
                    cmdPath = it->path();
                    break;
                }
            }
            if (cmdPath.empty() || !fs::exists(cmdPath)) continue;

            std::ifstream file(cmdPath);
            if (!file.is_open()) continue;

            fs::path scriptDir = cmdPath.parent_path();

            std::string line;
            while (std::getline(file, line)) {
                if (line.find("Code.exe") != std::string::npos && line.find("cli.js") != std::string::npos) {
                    size_t exeOpen = line.find('"');
                    size_t exeClose = line.find('"', exeOpen + 1);
                    size_t cliOpen = line.find('"', exeClose + 1);
                    size_t cliClose = line.find('"', cliOpen + 1);

                    if (exeOpen != std::string::npos && exeClose != std::string::npos &&
                        cliOpen != std::string::npos && cliClose != std::string::npos) {
                        
                        std::string exeRaw = line.substr(exeOpen + 1, exeClose - exeOpen - 1);
                        std::string cliRaw = line.substr(cliOpen + 1, cliClose - cliOpen - 1);

                        auto ResolveRelative = [&](std::string p) {
                            if (p.find("%~dp0") == 0) {
                                p.replace(0, 5, "");
                                return (scriptDir / p).lexically_normal();
                            }
                            return fs::path(p);
                        };

                        fs::path exePath = ResolveRelative(exeRaw);
                        fs::path cliPath = ResolveRelative(cliRaw);

                        if (fs::exists(exePath) && fs::exists(cliPath)) {
                            ctx.executablePath = exePath.u8string();
                            ctx.cliPath = cliPath.u8string();
                            ctx.isEngineFound = true;
                            return;
                        }
                    }
                }
            }
            file.close();
        }
        ctx.isEngineFound = false;
    }

    static void FindWSL(Context& ctx) {
        char pathBuf[MAX_PATH];
        if (SearchPathA(NULL, "wsl", ".exe", MAX_PATH, pathBuf, NULL) > 0) {
            ctx.executablePath = std::string(pathBuf);
            ctx.isEngineFound = true;
            return;
        }
        ctx.isEngineFound = false;
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

    static void LoadHistory(Context& ctx) {
        std::string fullPath = historyBaseDir + "\\" + ctx.historyFileName;
        std::ifstream file(fullPath);
        if (file.is_open()) {
            ctx.history.clear();
            std::string line;
            while (std::getline(file, line)) if (!line.empty()) ctx.history.push_back(line);
        }
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

    void Initialize() {
        UIStyle::Initialize();

        SetUpStoragePath();

        ctxVSCode.type = Mode::VSCode;
        ctxVSCode.historyFileName = "vscodelauncher_history.txt";
        ctxVSCode.logoResourceID = 101;
        FindVSCode(ctxVSCode);
        ctxVSCode.logoImage = LoadImageFromResource(ctxVSCode.logoResourceID);
        ctxVSCode.logoImageAspectRatio = (float)ctxVSCode.logoImage->GetWidth() / ctxVSCode.logoImage->GetHeight();
        ctxVSCode.placeholder = "Search for VS Code projects...";
        LoadHistory(ctxVSCode);

        ctxWSL.type = Mode::WSL;
        ctxWSL.historyFileName = "wsllauncher_history.txt";
        ctxWSL.logoResourceID = 102;
        FindWSL(ctxWSL);
        ctxWSL.logoImage = LoadImageFromResource(ctxWSL.logoResourceID);
        ctxWSL.logoImageAspectRatio = (float)ctxWSL.logoImage->GetWidth() / ctxWSL.logoImage->GetHeight();
        ctxWSL.placeholder = "Search for WSL directories...";
        LoadHistory(ctxWSL);

        InitializeCrawlerRootPaths();
        // BackgroundCrawl();
    }

    static void EnsureEnginePathValid(Context& ctx) {
        if (ctx.type == Mode::VSCode) {
            if (ctx.executablePath.empty() || !fs::exists(ctx.executablePath)) {
                FindVSCode(ctx);
            }
        } else if (ctx.type == Mode::WSL) {
            if (!fs::exists(ctx.executablePath)) FindWSL(ctx);
        }
    }

    static void PrepareWindowMetrics() {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        layoutMetrics.mainWinW = screenW * 0.50;
        layoutMetrics.mainWinH = screenH * 0.40;
        layoutMetrics.mainWinX = (screenW - layoutMetrics.mainWinW) / 2;
        layoutMetrics.mainWinY = (screenH - layoutMetrics.mainWinH) / 3;

        layoutMetrics.margin     = layoutMetrics.mainWinW * 0.02;
        layoutMetrics.currentY   = layoutMetrics.margin;
        layoutMetrics.innerWidth = layoutMetrics.mainWinW - (layoutMetrics.margin * 2);

        layoutMetrics.editH = layoutMetrics.mainWinH * 0.12;

        layoutMetrics.pathH = layoutMetrics.mainWinH * 0.10;
        layoutMetrics.listH = layoutMetrics.mainWinH - layoutMetrics.currentY - layoutMetrics.pathH - (layoutMetrics.margin * 2);

        layoutMetrics.mainFontSize  = layoutMetrics.mainWinH * 0.06;
        layoutMetrics.smallFontSize = layoutMetrics.mainFontSize * 0.8;
    }

    static void AlignUIElements() {
        RECT rectMainWin;
        GetClientRect(hLauncherWindow, &rectMainWin);

        POINT listOffset = {0, 0};
        MapWindowPoints(hListBox, hLauncherWindow, &listOffset, 1);

        layoutMetrics.logoImgHeight = (int)(0.55f * rectMainWin.bottom);
        layoutMetrics.logoImgWidth  = (int)(activeCtx->logoImageAspectRatio * layoutMetrics.logoImgHeight);

        int logoXInMain = (rectMainWin.right  / 2) - (layoutMetrics.logoImgWidth  / 2);
        int logoYInMain = (rectMainWin.bottom / 2) - (layoutMetrics.logoImgHeight / 2);

        layoutMetrics.logoFinalX = logoXInMain - listOffset.x; 
        layoutMetrics.logoFinalY = logoYInMain - listOffset.y;
    }

    static void SaveHistory(const Context& ctx) {
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

    static bool LaunchDeElevated(const std::string& path, const std::string& args, bool hide) {
        IShellWindows* psw = NULL;
        HRESULT hr = CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER, IID_IShellWindows, (void**)&psw);
        if (FAILED(hr)) return false;

        HWND hwnd = 0;
        IDispatch* pdisp = NULL;
        VARIANT vEmpty;
        VariantInit(&vEmpty);
        VARIANT vDesktop;
        VariantInit(&vDesktop);
        vDesktop.vt = VT_I4;
        vDesktop.lVal = CSIDL_DESKTOP;

        hr = psw->FindWindowSW(&vDesktop, &vEmpty, SWC_DESKTOP, (long*)&hwnd, SWFO_NEEDDISPATCH, &pdisp);
        psw->Release();
        if (FAILED(hr) || !pdisp) return false;

        IServiceProvider* psp = NULL;
        hr = pdisp->QueryInterface(IID_IServiceProvider, (void**)&psp);
        pdisp->Release();
        if (FAILED(hr)) return false;

        IShellBrowser* psb = NULL;
        hr = psp->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&psb);
        psp->Release();
        if (FAILED(hr)) return false;

        IShellView* psv = NULL;
        hr = psb->QueryActiveShellView(&psv);
        psb->Release();
        if (FAILED(hr)) return false;

        IDispatch* pdispView = NULL;
        hr = psv->GetItemObject(SVGIO_BACKGROUND, IID_IDispatch, (void**)&pdispView);
        psv->Release();
        if (FAILED(hr)) return false;

        IShellFolderViewDual* psfvd = NULL;
        hr = pdispView->QueryInterface(IID_IShellFolderViewDual, (void**)&psfvd);
        pdispView->Release();
        if (FAILED(hr)) return false;

        IDispatch* pdispApp = NULL;
        hr = psfvd->get_Application(&pdispApp);
        psfvd->Release();
        if (FAILED(hr)) return false;

        IShellDispatch2* psd = NULL;
        hr = pdispApp->QueryInterface(IID_IShellDispatch2, (void**)&psd);
        pdispApp->Release();
        if (FAILED(hr)) return false;

        BSTR bstrPath = SysAllocString(std::wstring(path.begin(), path.end()).c_str());
        VARIANT vArgs;
        VariantInit(&vArgs);
        vArgs.vt = VT_BSTR;
        vArgs.bstrVal = SysAllocString(std::wstring(args.begin(), args.end()).c_str());
        
        VARIANT vVerb, vDir, vShow;
        VariantInit(&vVerb);
        VariantInit(&vDir);
        VariantInit(&vShow);
        vShow.vt = VT_I4;
        vShow.lVal = hide ? SW_HIDE : SW_SHOWNORMAL;

        hr = psd->ShellExecute(bstrPath, vArgs, vDir, vVerb, vShow);

        SysFreeString(bstrPath);
        VariantClear(&vArgs);
        psd->Release();

        return SUCCEEDED(hr);
    }

    static void ExecuteLaunch(int selected) {
        std::string path = currentMatches[selected];
        AddToHistory(path);

        if (activeCtx->type == Mode::VSCode) {
            std::string fullArgs =
                "/c \"set ELECTRON_RUN_AS_NODE=1 && \"" + 
                activeCtx->executablePath + "\" \"" + 
                activeCtx->cliPath + "\" \"" + path + "\"\"";
            LaunchDeElevated("cmd.exe", fullArgs, true);        
        
        } else if (activeCtx->type == Mode::WSL) {
            std::string distroName = ExtractDistroFromPath(path);
            std::string linuxPath = ResolveWSLPath(path, distroName);
            std::string wslArgs = "";
            if (!distroName.empty()) wslArgs += "-d " + distroName + " ";
            wslArgs += "--cd \"" + linuxPath + "\"";
            LaunchDeElevated("wsl.exe", wslArgs, false);
        }
    }

    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
        switch (uMsg) {
            case WM_KEYDOWN: {
                if (wParam == VK_RETURN && !currentMatches.empty()) {
                    int sel = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
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
                    int count = (int)SendMessage(hListBox, LB_GETCOUNT, 0, 0);
                    if (count <= 0) {
                        return 0;
                    }

                    int cur = (int)SendMessage(hListBox, LB_GETCURSEL, 0, 0);
                    int next = (wParam == VK_DOWN) ? (cur + 1) : (cur - 1);
                    if (next < 0) next = count - 1;
                    if (next >= count) next = 0;

                    SendMessage(hListBox, LB_SETCURSEL, next, 0);
                    SetWindowTextA(hPathLabel, currentMatches[next].c_str());
                    InvalidateRect(hListBox, NULL, FALSE);
                    return 0;
                }
                break;
            }
            case WM_GETDLGCODE: {
                if (lParam && ((MSG*)lParam)->message == WM_KEYDOWN) {
                    if (wParam == VK_RETURN) {
                        return DLGC_WANTALLKEYS;
                    }
                }
                break;
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
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBmp = CreateCompatibleBitmap(hdc, rcList.right, rcList.bottom);
                HGDIOBJ oldBmp = SelectObject(memDC, memBmp);
                FillRect(memDC, &rcList, UIStyle::hBgBrush);

                if (activeCtx && activeCtx->logoImage) {
                    Gdiplus::Graphics graphics(memDC);
                    graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
                    graphics.SetInterpolationMode(Gdiplus::InterpolationModeLowQuality);
                    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);

                    graphics.DrawImage(
                        activeCtx->logoImage,
                        Gdiplus::Rect(layoutMetrics.logoFinalX, layoutMetrics.logoFinalY, layoutMetrics.logoImgWidth, layoutMetrics.logoImgHeight),
                        0, 0,
                        activeCtx->logoImage->GetWidth(), activeCtx->logoImage->GetHeight(),
                        Gdiplus::UnitPixel,
                        UIStyle::logoAttr
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
                SetTextColor(hdc, UIStyle::COL_TXT.ToCOLORREF());
                SetBkColor(hdc, UIStyle::COL_BG.ToCOLORREF());
                return (INT_PTR)UIStyle::hBgBrush;
            }
            case WM_CTLCOLORLISTBOX: {
                SetBkMode((HDC)wParam, TRANSPARENT);
                return (LRESULT)GetStockObject(NULL_BRUSH);
            }
            case WM_CTLCOLORSTATIC: {
                HDC hdc = (HDC)wParam;
                SetTextColor(hdc, UIStyle::COL_DIMTXT.ToCOLORREF());
                SetBkColor(hdc, UIStyle::COL_BG.ToCOLORREF());
                return (INT_PTR)UIStyle::hBgBrush;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                Gdiplus::Graphics graphics(hdc);

                Gdiplus::SolidBrush bgBrush(UIStyle::COL_BG);
                RECT rc;
                GetClientRect(hwnd, &rc);

                graphics.FillRectangle(&bgBrush, 0, 0, rc.right, rc.bottom);

                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_DRAWITEM: {
                PDRAWITEMSTRUCT pdis = (PDRAWITEMSTRUCT)lParam;
                if (pdis->itemID == (UINT)-1) return TRUE;

                Gdiplus::Graphics graphics(pdis->hDC);
                graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                bool sel = pdis->itemState & ODS_SELECTED;
                if (sel) {
                    Gdiplus::SolidBrush selBrush(UIStyle::COL_SEL);
                    graphics.FillRectangle(
                        &selBrush,
                        (int)pdis->rcItem.left,
                        (int)pdis->rcItem.top,
                        pdis->rcItem.right - pdis->rcItem.left,
                        pdis->rcItem.bottom - pdis->rcItem.top
                    );
                }

                char buffer[256];
                SendMessage(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)buffer);
                SetBkMode(pdis->hDC, TRANSPARENT);
                std::wstring wText = ConvertToWide(buffer);

                Gdiplus::SolidBrush textBrush(sel ? UIStyle::COL_TXT : UIStyle::COL_SELTXT);
                Gdiplus::StringFormat format;
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                Gdiplus::RectF layoutRect(
                    (float)pdis->rcItem.left + 15.0f,
                    (float)pdis->rcItem.top, 
                    (float)(pdis->rcItem.right - pdis->rcItem.left) - 15.0f,
                    (float)(pdis->rcItem.bottom - pdis->rcItem.top)
                );

                graphics.DrawString(wText.c_str(), -1, UIStyle::mainFont, layoutRect, &format, &textBrush);
                
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

    void Show(Mode mode) {
        if (hLauncherWindow) return;

        switch (mode) {
            case Mode::VSCode:
                activeCtx = &ctxVSCode;
                break;
            case Mode::WSL:
                activeCtx = &ctxWSL;
                break;
            default:
                activeCtx = &ctxVSCode;
                break;
        }

        EnsureEnginePathValid(*activeCtx);

        if (!launcherClassRegistered) {
            WNDCLASSA wc {};
            wc.lpfnWndProc = LauncherWindProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "KinesisLauncher";
            wc.hCursor = LoadCursor(NULL, IDC_ARROW);
            RegisterClassA(&wc);
            launcherClassRegistered = true;
        }
        
        PrepareWindowMetrics();
        UIStyle::UpdateScaleDependentResources(layoutMetrics.mainFontSize, layoutMetrics.smallFontSize);

        hLauncherWindow = CreateWindowExA(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            "KinesisLauncher",
            NULL,
            WS_POPUP | WS_VISIBLE,
            layoutMetrics.mainWinX, layoutMetrics.mainWinY, layoutMetrics.mainWinW, layoutMetrics.mainWinH,
            NULL, NULL,
            GetModuleHandle(NULL),
            NULL
        );
        SetLayeredWindowAttributes(hLauncherWindow, 0, 245, LWA_ALPHA);

        hEdit = CreateWindowExA(
            0,
            "EDIT",
            "",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            layoutMetrics.margin, layoutMetrics.currentY, layoutMetrics.innerWidth, layoutMetrics.editH,
            hLauncherWindow,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );
        
        layoutMetrics.currentY += layoutMetrics.editH + (layoutMetrics.margin / 2);
        
        hListBox = CreateWindowExA(
            0,
            "LISTBOX",
            NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
            layoutMetrics.margin, layoutMetrics.currentY, layoutMetrics.innerWidth, layoutMetrics.listH,
            hLauncherWindow,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );

        layoutMetrics.currentY += layoutMetrics.listH + (layoutMetrics.margin / 2);

        hPathLabel = CreateWindowExA(
            0,
            "STATIC",
            "",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            layoutMetrics.margin, layoutMetrics.currentY, layoutMetrics.innerWidth,
            layoutMetrics.pathH,
            hLauncherWindow,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );

        AlignUIElements();

        SendMessage(hListBox, LB_SETITEMHEIGHT, 0, (LPARAM)(layoutMetrics.mainWinH * 0.12));

        SendMessage(hEdit,      WM_SETFONT, (WPARAM)UIStyle::hWin32MainFont,  TRUE);
        SendMessage(hListBox,   WM_SETFONT, (WPARAM)UIStyle::hWin32MainFont,  TRUE);    
        SendMessage(hPathLabel, WM_SETFONT, (WPARAM)UIStyle::hWin32SmallFont, TRUE);

        if (!activeCtx->isEngineFound) {
            EnableWindow(hEdit, FALSE);
            SetWindowTextA(hEdit, "");
        }

        int cornerRadius = layoutMetrics.mainWinH * 0.06;
        HRGN hMainRgn = CreateRoundRectRgn(0, 0, layoutMetrics.mainWinW, layoutMetrics.mainWinH, cornerRadius, cornerRadius);
        SetWindowRgn(hLauncherWindow, hMainRgn, TRUE);

        SetWindowSubclass(hEdit, EditSubclassProc, 0, 0);
        SetWindowSubclass(hListBox, ListBoxSubclassProc, 0, 0);

        LoadHistory(*activeCtx);
        // BackgroundCrawl();
        RefreshMatches("");

        AllowSetForegroundWindow(ASFW_ANY);
        keybd_event(0xFC, 0, 0, 0);
        keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
        SmoothShowWindow(hLauncherWindow);
        SetForegroundWindow(hLauncherWindow);
        SetActiveWindow(hLauncherWindow);
        if (!activeCtx->isEngineFound) {
            SetFocus(hLauncherWindow);
        } else {
            SetFocus(hEdit);
        }
    }

    void ReleaseResources() {
        delete ctxVSCode.logoImage; ctxVSCode.logoImage = nullptr;
        delete ctxWSL.logoImage;    ctxWSL.logoImage    = nullptr;
        UIStyle::Release();
    }
}