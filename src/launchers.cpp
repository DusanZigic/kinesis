#include "launchers.hpp"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <commctrl.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <thread>
#include <atomic>
#include <mutex>

#include "common.hpp"

namespace fs = std::filesystem;

namespace Launcher {
    struct Context {
        Mode type;
        std::wstring windowTitle;
        int logoResourceID;
        std::wstring historyFileName;
        bool isEngineFound = false;
        std::wstring executablePath;
        std::wstring cliPath;
        Gdiplus::Image* logoImage = nullptr;
        float logoImageAspectRatio;
        std::wstring placeholder;
        std::vector<std::wstring> history;
    };

    struct LayoutMetrics {
        int mainWinW, mainWinH, mainWinX, mainWinY;
        int margin, innerWidth;
        int editH, editY;
        int listH, listY;
        int pathH, pathY;
        float mainFontSize, smallFontSize;
        int logoImgHeight, logoImgWidth;
        int logoFinalX, logoFinalY;
    };

    struct UIStyle {
        inline static const Gdiplus::Color COL_BG     = Gdiplus::Color(255,  30,  30,  30);
        inline static const Gdiplus::Color COL_SEL    = Gdiplus::Color(128, 100, 100, 100);
        inline static const Gdiplus::Color COL_TXT    = Gdiplus::Color(255, 255, 255, 255);
        inline static const Gdiplus::Color COL_SELTXT = Gdiplus::Color(255, 200, 200, 200);
        inline static const Gdiplus::Color COL_DIMTXT = Gdiplus::Color(255, 150, 150, 150);
        static HBRUSH hBgBrush;
        static Gdiplus::ImageAttributes* logoAttr;
        static Gdiplus::Font* mainFont;
        static Gdiplus::Font* smallFont;
        static HFONT hWin32MainFont;  
        static HFONT hWin32SmallFont;

        static void Initialize() {
            hBgBrush = CreateSolidBrush(COL_BG.ToCOLORREF());

            logoAttr = new Gdiplus::ImageAttributes();
            Gdiplus::ColorMatrix matrix = {
                0.75f, 0.00f, 0.00f, 0.00f, 0.00f,
                0.00f, 0.75f, 0.00f, 0.00f, 0.00f,
                0.00f, 0.00f, 0.75f, 0.00f, 0.00f,
                0.00f, 0.00f, 0.00f, 0.20f, 0.00f,
                0.05f, 0.05f, 0.05f, 0.00f, 1.00f 
            };
            logoAttr->SetColorMatrix(&matrix);
        }

        static void UpdateScaleDependentResources(float mainFontSize, float smallFontSize) {
            if (mainFont)  {delete mainFont;  mainFont =  nullptr;}
            if (smallFont) {delete smallFont; smallFont = nullptr;}

            if (hWin32MainFont)  { DeleteObject(hWin32MainFont);  hWin32MainFont  = nullptr;}
            if (hWin32SmallFont) { DeleteObject(hWin32SmallFont); hWin32SmallFont = nullptr;}

            mainFont  = new Gdiplus::Font(L"Consolas", mainFontSize,  Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            smallFont = new Gdiplus::Font(L"Consolas", smallFontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

            hWin32MainFont = CreateFontW(
                -(int)mainFontSize,
                0, 0, 0,
                FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN,
                L"Consolas"
            );

            hWin32SmallFont = CreateFontW(
                -(int)smallFontSize,
                0, 0, 0,
                FW_NORMAL,
                FALSE, FALSE, FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                FIXED_PITCH | FF_MODERN,
                L"Consolas"
            );
        }

        static void Release() {
            if (hBgBrush)        {DeleteObject(hBgBrush);        hBgBrush        = nullptr;}
            if (logoAttr)        {delete logoAttr;               logoAttr        = nullptr;}
            if (mainFont)        {delete mainFont;               mainFont        = nullptr;}
            if (smallFont)       {delete smallFont;              smallFont       = nullptr;}
            if (hWin32MainFont)  {DeleteObject(hWin32MainFont);  hWin32MainFont  = nullptr;}
            if (hWin32SmallFont) {DeleteObject(hWin32SmallFont); hWin32SmallFont = nullptr;}
        }

    };

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

    static std::vector<std::wstring> allCrawledFolders;
    static std::vector<std::wstring> currentMatches;
    static std::mutex crawlMutex;
    static std::atomic<bool> isScanning(false);
    
    static std::wstring historyBaseDir = L"";
    static std::vector<std::wstring> crawlerRootPaths;
    static const int maxSubFolderDepth = 5;
    static const int maxPathsN = 5;

    static int pendingIndex = -1;

    static std::wstring GetEnv(const std::wstring& var) {
        wchar_t buf[MAX_PATH];
        DWORD res = GetEnvironmentVariableW(var.c_str(), buf, MAX_PATH);
        return (res > 0 && res < MAX_PATH) ? std::wstring(buf) : L"";
    }

    static void FindVSCode(Context& ctx) {
        std::vector<std::wstring> searchBases = {
            GetEnv(L"LOCALAPPDATA") + L"\\Programs\\Microsoft VS Code",
            GetEnv(L"ProgramFiles") + L"\\Microsoft VS Code",
            L"C:\\Program Files\\Microsoft VS Code"
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

            std::wifstream file(cmdPath);
            if (!file.is_open()) continue;

            fs::path scriptDir = cmdPath.parent_path();

            std::wstring line;
            while (std::getline(file, line)) {
                if (line.find(L"Code.exe") != std::string::npos && line.find(L"cli.js") != std::string::npos) {
                    size_t exeOpen = line.find('"');
                    size_t exeClose = line.find('"', exeOpen + 1);
                    size_t cliOpen = line.find('"', exeClose + 1);
                    size_t cliClose = line.find('"', cliOpen + 1);

                    if (exeOpen != std::string::npos && exeClose != std::string::npos &&
                        cliOpen != std::string::npos && cliClose != std::string::npos) {
                        
                        std::wstring exeRaw = line.substr(exeOpen + 1, exeClose - exeOpen - 1);
                        std::wstring cliRaw = line.substr(cliOpen + 1, cliClose - cliOpen - 1);

                        auto ResolveRelative = [&](std::wstring p) {
                            if (p.find(L"%~dp0") == 0) {
                                p.replace(0, 5, L"");
                                return (scriptDir / p).lexically_normal();
                            }
                            return fs::path(p);
                        };

                        fs::path exePath = ResolveRelative(exeRaw);
                        fs::path cliPath = ResolveRelative(cliRaw);

                        if (fs::exists(exePath) && fs::exists(cliPath)) {
                            ctx.executablePath = exePath.wstring();
                            ctx.cliPath = cliPath.wstring();
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
        wchar_t pathBuf[MAX_PATH];
        if (SearchPathW(NULL, L"wsl", L".exe", MAX_PATH, pathBuf, NULL) > 0) {
            ctx.executablePath = std::wstring(pathBuf);
            ctx.cliPath = L"";
            ctx.isEngineFound = true;
            return;
        }
        ctx.isEngineFound = false;
    }

    static void SetUpStoragePath() {
        std::wstring baseAppPath = Common::GetKnownFolderPathW(FOLDERID_LocalAppData);
        if (!baseAppPath.empty()) {
            std::wstring kinesisPath = baseAppPath + L"\\Kinesis";
            std::wstring historyPath = kinesisPath + L"\\History";
            CreateDirectoryW(kinesisPath.c_str(), NULL);
            CreateDirectoryW(historyPath.c_str(), NULL);
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
        std::wstring wideHistoryPath = historyBaseDir + L"\\" + ctx.historyFileName;
        std::ifstream file(wideHistoryPath.c_str(), std::ios::in);
        if (file.is_open()) {
            ctx.history.clear();
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty()) {
                    std::wstring wPath = Common::UTF8ToW(line);
                    if (fs::exists(wPath)) {
                        ctx.history.push_back(wPath);
                    }
                }
            }
            file.close();
        }
    }

    static const std::vector<std::wstring> GetOneDrivePaths() {
        std::vector<std::wstring> paths;
        HKEY hKey;

        const wchar_t* subkey = L"Software\\Microsoft\\OneDrive\\Accounts";

        if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t accountName[256];
            DWORD nameSize = sizeof(accountName) / sizeof(wchar_t);

            for (DWORD i = 0; RegEnumKeyExW(hKey, i, accountName, &nameSize, NULL, NULL, NULL, NULL) == ERROR_SUCCESS; ++i) {
                HKEY hAccountKey;
                if (RegOpenKeyExW(hKey, accountName, 0, KEY_READ, &hAccountKey) == ERROR_SUCCESS) {
                    wchar_t path[MAX_PATH];
                    DWORD pathSize = sizeof(path);
                    if (RegQueryValueExW(hAccountKey, L"UserFolder", NULL, NULL, (LPBYTE)path, &pathSize) == ERROR_SUCCESS) {
                        if (fs::exists(path)) {
                            paths.push_back(std::wstring(path));
                        }
                    }
                    RegCloseKey(hAccountKey);
                }
                nameSize = sizeof(accountName) / sizeof(wchar_t);
            }
            RegCloseKey(hKey);
        }

        if (paths.empty()) {
            std::wstring personal = GetEnv(L"OneDrive");
            if (!personal.empty() && fs::exists(personal)) paths.push_back(personal);
            
            std::wstring business = GetEnv(L"OneDriveCommercial");
            if (!business.empty() && fs::exists(business)) paths.push_back(business);
        }

        return paths;
    }

    static std::vector<std::wstring> GetWSLDistros() {
        std::vector<std::wstring> distros;
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

            std::wstring currentDistro;
            for (size_t i = 0; i < rawBuffer.size(); i += 2) {
                wchar_t c = rawBuffer[i];
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
            std::wstring p = Common::GetKnownFolderPathW(id);
            if (!p.empty()) crawlerRootPaths.push_back(p);
        }

        std::vector<std::wstring> oneDrivePaths = GetOneDrivePaths();
        for (const auto& path : oneDrivePaths) {
            if (std::find(crawlerRootPaths.begin(), crawlerRootPaths.end(), path) == crawlerRootPaths.end()) {
                crawlerRootPaths.push_back(path);
            }
        }

        std::vector<std::wstring> distros = GetWSLDistros();
        std::error_code ec;
        for (const auto& distro : distros) {
            std::wstring basePaths[] = { 
                L"\\\\wsl.localhost\\" + distro + L"\\home",
                L"\\\\wsl$\\" + distro + L"\\home" 
            };
            for (const auto& homeBase : basePaths) {
                if (fs::exists(homeBase, ec)) {
                    for (auto const& userEntry : fs::directory_iterator(homeBase, ec)) {
                        if (!ec && userEntry.is_directory(ec)) {
                            crawlerRootPaths.push_back(userEntry.path().wstring());
                        }
                    }
                    break;
                }
            }
        }
    }

    static void ScanDirectory(const std::wstring& path, std::vector<std::wstring>& results, int depth) {
        if (depth > maxSubFolderDepth) return;

        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW((path + L"\\*").c_str(), &fd);
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
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) {
                    continue;
                }
                if (wcscmp(fd.cFileName, L"node_modules") == 0 ||
                    wcscmp(fd.cFileName, L".git") == 0 ||
                    wcscmp(fd.cFileName, L"bin") == 0 ||
                    wcscmp(fd.cFileName, L".vs") == 0 ||
                    wcscmp(fd.cFileName, L"obj") == 0) {
                        continue;
                }
                std::wstring fullPath = path + L"\\" + fd.cFileName;
                results.push_back(fullPath);
                ScanDirectory(fullPath, results, depth + 1);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }

    static void BackgroundCrawl() {
        if (isScanning.exchange(true)) return;
        std::thread([]() {
            std::vector<std::wstring> tempFolders;
            for (const auto& root : crawlerRootPaths) ScanDirectory(root, tempFolders, 0);
            {
                std::lock_guard<std::mutex> lock(crawlMutex);
                allCrawledFolders.swap(tempFolders);
            }
            isScanning = false;
        }).detach();
    }

    static void RefreshMatches(std::wstring input) {
        if (!activeCtx->isEngineFound) {
            SendMessage(hListBox, LB_RESETCONTENT, 0, 0);
            SetWindowTextW(hPathLabel, L"ERROR: executable not found! Check your installation.");
            return;
        }

        currentMatches.clear();
        SendMessage(hListBox, LB_RESETCONTENT, 0, 0);

        std::wstring lowerInput = input;
        std::transform(lowerInput.begin(), lowerInput.end(), lowerInput.begin(), ::tolower);

        auto addMatch = [&](const std::wstring& path) {
            currentMatches.push_back(path);
            PCWSTR displayName = PathFindFileNameW(path.c_str());
            SendMessageW(hListBox, LB_ADDSTRING, 0, (LPARAM)displayName);
        };

        if (input.empty()) {
            for (size_t i = 0; i < activeCtx->history.size() && i < maxPathsN; ++i) {
                addMatch(activeCtx->history[i]);
            }
        } else {
            for (const auto& path : activeCtx->history) {
                if (currentMatches.size() >= maxPathsN) break;

                std::wstring lowerPath = path;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
                if (lowerPath.find(lowerInput) != std::wstring::npos) {
                    addMatch(path);
                }
            }
            std::lock_guard<std::mutex> lock(crawlMutex);
            for (const auto& path : allCrawledFolders) {
                if (currentMatches.size() >= maxPathsN) break;
                
                if (std::find(activeCtx->history.begin(), activeCtx->history.end(), path) != activeCtx->history.end()) continue;
                std::wstring lowerPath = path;
                std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
                if (lowerPath.find(lowerInput) != std::wstring::npos) {
                    addMatch(path);
                }
            }
        }

        if (!currentMatches.empty()) {
            SendMessage(hListBox, LB_SETCURSEL, 0, 0);
            SetWindowTextW(hPathLabel, currentMatches[0].c_str());
        } else {
            if (isScanning) {
                SetWindowTextW(hPathLabel, activeCtx->placeholder.c_str());
            } else {
                SetWindowTextW(hPathLabel, input.empty() ? L"" : L"No matches found.");
            }
        }

        InvalidateRect(hListBox, NULL, FALSE);
        UpdateWindow(hListBox);
    }

    void Initialize() {
        UIStyle::Initialize();

        SetUpStoragePath();

        ctxVSCode.type = Mode::VSCode;
        ctxVSCode.historyFileName = L"vscodelauncher_history.txt";
        ctxVSCode.logoResourceID = 101;
        FindVSCode(ctxVSCode);
        ctxVSCode.logoImage = LoadImageFromResource(ctxVSCode.logoResourceID);
        ctxVSCode.logoImageAspectRatio = (float)ctxVSCode.logoImage->GetWidth() / ctxVSCode.logoImage->GetHeight();
        ctxVSCode.placeholder = L"Search for VS Code projects...";
        LoadHistory(ctxVSCode);

        ctxWSL.type = Mode::WSL;
        ctxWSL.historyFileName = L"wsllauncher_history.txt";
        ctxWSL.logoResourceID = 102;
        FindWSL(ctxWSL);
        ctxWSL.logoImage = LoadImageFromResource(ctxWSL.logoResourceID);
        ctxWSL.logoImageAspectRatio = (float)ctxWSL.logoImage->GetWidth() / ctxWSL.logoImage->GetHeight();
        ctxWSL.placeholder = L"Search for WSL directories...";
        LoadHistory(ctxWSL);

        InitializeCrawlerRootPaths();
        BackgroundCrawl();
    }

    static void EnsureEnginePathValid(Context& ctx) {
        if (ctx.type == Mode::VSCode) {
            if (ctx.executablePath.empty() || !fs::exists(ctx.executablePath) || !fs::exists(ctx.cliPath)) {
                FindVSCode(ctx);
            }
        } else if (ctx.type == Mode::WSL) {
            if (ctx.executablePath.empty() || !fs::exists(ctx.executablePath)) {
                FindWSL(ctx);
            }
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
        layoutMetrics.innerWidth = layoutMetrics.mainWinW - (layoutMetrics.margin * 2);
        int spacing = layoutMetrics.margin / 2;

        layoutMetrics.editH = layoutMetrics.mainWinH * 0.12;
        layoutMetrics.editY = layoutMetrics.margin;

        layoutMetrics.pathH = layoutMetrics.mainWinH * 0.10;
        layoutMetrics.pathY = layoutMetrics.mainWinH - layoutMetrics.pathH - layoutMetrics.margin;

        int editBottom = layoutMetrics.editY + layoutMetrics.editH;
        layoutMetrics.listY = editBottom + spacing;
        layoutMetrics.listH = layoutMetrics.pathY - spacing - layoutMetrics.listY;

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
        std::wstring wideHistoryPath = historyBaseDir + L"\\" + ctx.historyFileName;
        std::ofstream file(wideHistoryPath.c_str(), std::ios::out | std::ios::trunc);
        if (file.is_open()) {
            for (const auto& wline : ctx.history) {
                file << Common::WToUTF8(wline) << "\n";
            }
            file.close();
        }
    }

    static void AddToHistory(const std::wstring& newPath) {
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

    static std::wstring ExtractDistroFromPath(const std::wstring& path) {
        std::wstring distroName = L"";
        std::wstring prefix = L"\\\\wsl.localhost\\";
        size_t start = path.find(prefix);
        if (start != std::wstring::npos) {
            start += prefix.length();
            size_t end = path.find('\\', start);
            if (end != std::wstring::npos) {
                distroName = path.substr(start, end - start);
                return distroName;
            }
        } else {
            prefix = L"\\\\wsl$\\";
            start = path.find(prefix);
            if (start != std::wstring::npos) {
                start += prefix.length();
                size_t end = path.find('\\', start);
                if (end != std::wstring::npos) {
                    distroName = path.substr(start, end - start);
                    return distroName;
                }
            }
        }

        return L"";
    }

    static std::wstring ResolveWSLPath(const std::wstring& windowsPath, const std::wstring& distroName) {
        if (!distroName.empty()) {
            std::wstring searchKey = L"\\" + distroName;
            size_t pos = windowsPath.find(searchKey);
            if (pos != std::wstring::npos) {
                std::wstring linuxPath = windowsPath.substr(pos + searchKey.length());
                std::replace(linuxPath.begin(), linuxPath.end(), '\\', '/');
                return linuxPath.empty() ? L"/" : linuxPath;
            }
        } else {
            if (windowsPath.length() >= 3 && windowsPath[1] == ':' && windowsPath[2] == '\\') {
                std::wstring linuxPath = windowsPath;
                wchar_t driveLetter = tolower(linuxPath[0]);
                linuxPath = L"/mnt/" + std::wstring(1, driveLetter) + linuxPath.substr(2);
                std::replace(linuxPath.begin(), linuxPath.end(), '\\', '/');
                return linuxPath.empty() ? L"/" : linuxPath;
            }
        }
        return L"/";
    }

    static bool LaunchDeElevated(const std::wstring& path, const std::wstring& args, bool hide) {
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

        BSTR bstrPath = SysAllocString(path.c_str());
        VARIANT vArgs;
        VariantInit(&vArgs);
        vArgs.vt = VT_BSTR;
        vArgs.bstrVal = SysAllocString(args.c_str());
        
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
        std::wstring path = currentMatches[selected];
        AddToHistory(path);

        if (activeCtx->type == Mode::VSCode) {
            std::wstring fullArgs =
                L"/c \"set ELECTRON_RUN_AS_NODE=1 && \"" + 
                activeCtx->executablePath + L"\" \"" + 
                activeCtx->cliPath + L"\" \"" + path + L"\"\"";
            LaunchDeElevated(L"cmd.exe", fullArgs, true);        
        
        } else if (activeCtx->type == Mode::WSL) {
            std::wstring distroName = ExtractDistroFromPath(path);
            std::wstring linuxPath = ResolveWSLPath(path, distroName);
            std::wstring wslArgs = L"";
            if (!distroName.empty()) wslArgs += L"-d " + distroName + L" ";
            wslArgs += L"--cd \"" + linuxPath + L"\"";
            LaunchDeElevated(L"wsl.exe", wslArgs, false);
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
                    SetWindowTextW(hPathLabel, currentMatches[next].c_str());
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
                        SetWindowTextW(hPathLabel, currentMatches[pendingIndex].c_str());
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

                wchar_t buffer[MAX_PATH];
                SendMessageW(pdis->hwndItem, LB_GETTEXT, pdis->itemID, (LPARAM)buffer);
                SetBkMode(pdis->hDC, TRANSPARENT);

                Gdiplus::SolidBrush textBrush(sel ? UIStyle::COL_TXT : UIStyle::COL_SELTXT);
                Gdiplus::StringFormat format;
                format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

                Gdiplus::RectF layoutRect(
                    (float)pdis->rcItem.left + 15.0f,
                    (float)pdis->rcItem.top, 
                    (float)(pdis->rcItem.right - pdis->rcItem.left) - 15.0f,
                    (float)(pdis->rcItem.bottom - pdis->rcItem.top)
                );

                graphics.DrawString(buffer, -1, UIStyle::mainFont, layoutRect, &format, &textBrush);
                
                return TRUE;
            }
            case WM_COMMAND: {
                if (HIWORD(wParam) == EN_CHANGE) {
                    wchar_t buffer[MAX_PATH];
                    GetWindowTextW(hEdit, buffer, MAX_PATH);
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
            WNDCLASSW wc {};
            wc.lpfnWndProc   = LauncherWindProc;
            wc.hInstance     = GetModuleHandle(NULL);
            wc.lpszClassName = L"KinesisLauncher";
            wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
            wc.hbrBackground = NULL; 
            if (RegisterClassW(&wc)) {
                launcherClassRegistered = true;
            }
        }
        
        PrepareWindowMetrics();
        UIStyle::UpdateScaleDependentResources(layoutMetrics.mainFontSize, layoutMetrics.smallFontSize);

        hLauncherWindow = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            L"KinesisLauncher",
            nullptr,
            WS_POPUP,
            layoutMetrics.mainWinX, layoutMetrics.mainWinY, layoutMetrics.mainWinW, layoutMetrics.mainWinH,
            NULL,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );
        SetLayeredWindowAttributes(hLauncherWindow, 0, 245, LWA_ALPHA);

        hEdit = CreateWindowExW(
            0,
            L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            layoutMetrics.margin, layoutMetrics.editY, layoutMetrics.innerWidth, layoutMetrics.editH,
            hLauncherWindow,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );
        
        hListBox = CreateWindowExW(
            0,
            L"LISTBOX",
            nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
            layoutMetrics.margin, layoutMetrics.listY, layoutMetrics.innerWidth, layoutMetrics.listH,
            hLauncherWindow,
            NULL,
            GetModuleHandle(NULL),
            NULL
        );

        hPathLabel = CreateWindowExW(
            0,
            L"STATIC",
            L"",
            WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
            layoutMetrics.margin, layoutMetrics.pathY, layoutMetrics.innerWidth, layoutMetrics.pathH,
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
            SetWindowTextW(hEdit, L"");
        }

        int cornerRadius = layoutMetrics.mainWinH * 0.06;
        HRGN hMainRgn = CreateRoundRectRgn(0, 0, layoutMetrics.mainWinW, layoutMetrics.mainWinH, cornerRadius, cornerRadius);
        SetWindowRgn(hLauncherWindow, hMainRgn, TRUE);

        SetWindowSubclass(hEdit, EditSubclassProc, 0, 0);
        SetWindowSubclass(hListBox, ListBoxSubclassProc, 0, 0);

        LoadHistory(*activeCtx);
        BackgroundCrawl();
        RefreshMatches(L"");

        AllowSetForegroundWindow(ASFW_ANY);
        keybd_event(0xFC, 0, 0, 0);
        keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
        Common::SmoothShowWindow(hLauncherWindow);
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