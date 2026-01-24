#include "common.hpp"
#include "config.hpp"
#include "taskswitcher.hpp"

static SwitcherMode currentMode = SwitcherMode::None;

static bool classRegistered = false;
static HWND hSwitcherWindow = NULL;
static HFONT hSwitcherFont = NULL;
static HBRUSH hSwitcherBackBrush = NULL;

static std::set<std::string> seenProcessNames;

static std::vector<HTHUMBNAIL> sessionThumbs;

static SwitcherLayout cachedLayout;

static std::vector<HWND> sessionWindows;
static size_t sessionIndex = 0;
static size_t lastAllAppsIndex = 0;

static const double MAX_SWITCHER_RELATIVE_WIDTH = 0.85;

bool IsSwitcherActive() {
    return currentMode != SwitcherMode::None;
}

SwitcherLayout CalculateSwitcherLayout(size_t count, SwitcherMode mode) {
    SwitcherLayout layout;

    double screenW = GetSystemMetrics(SM_CXSCREEN);
    double screenH = GetSystemMetrics(SM_CYSCREEN);
    
    layout.margin      = (int)(0.013*screenW);
    layout.spacing     = (int)(0.010*screenW);
    layout.titleHeight = (int)(0.046*screenH);
    layout.fontSize    = (int)(0.450*layout.titleHeight);

    if (mode == SwitcherMode::AllApps) {
        layout.maxCols = 7;
        layout.cols = (count < (size_t)layout.maxCols) ? (int)count : layout.maxCols;
        layout.rows = (int)std::ceil((double)count / layout.cols);
        layout.thumbW = (int)(0.07 * screenW);
        layout.thumbH = layout.thumbW;
    }
    else if (mode == SwitcherMode::SameApp) {
        layout.maxCols = 4;
        layout.cols = (count < (size_t)layout.maxCols) ? (int)count : layout.maxCols;
        layout.rows = (int)std::ceil((double)count / layout.cols);
        double maxSwitcherWidth = screenW*MAX_SWITCHER_RELATIVE_WIDTH;
        int usableWidth = (int)maxSwitcherWidth - 2*layout.margin - (layout.cols - 1)*layout.spacing;
        double  aspect = screenW/screenH;
        layout.thumbW = usableWidth / layout.cols;
        layout.thumbH = (int)(layout.thumbW / aspect);
    }
        
    layout.winW = layout.cols*layout.thumbW + (layout.cols - 1)*layout.spacing + 2*layout.margin;
    layout.winH = layout.rows*layout.thumbH + (layout.rows - 1)*layout.spacing + 2*layout.margin + layout.titleHeight;

    double maxHeight = screenH * 0.95;
    if (layout.winH > maxHeight) {
        double scaleFactor = maxHeight / layout.winH;
        layout.thumbW = (int)(layout.thumbW * scaleFactor);
        layout.thumbH = (int)(layout.thumbH * scaleFactor);
        layout.winW   = (int)(layout.winW   * scaleFactor);
        layout.winH   = (int)(layout.winH   * scaleFactor);
    }

    return layout;
}

RECT GetThumbRect(const SwitcherLayout& layout, size_t index, size_t count) {
    int col = (int)index % layout.cols;
    int row = (int)index / layout.cols;
    
    RECT r;
    r.left   = layout.margin + col*(layout.thumbW + layout.spacing);
    r.top    = layout.margin + layout.titleHeight + row*(layout.thumbH + layout.spacing);
    r.right  = r.left + layout.thumbW;
    r.bottom = r.top + layout.thumbH;

    int windowsInThisRow = (row == layout.rows - 1) ? (count % layout.cols) : layout.cols;
    if (windowsInThisRow == 0) {
        windowsInThisRow = layout.cols;
    }

    if (windowsInThisRow < layout.cols) {
        int rowWidth = windowsInThisRow*layout.thumbW + (windowsInThisRow - 1)*layout.spacing;
        int totalWidth = layout.cols*layout.thumbW + (layout.cols - 1)*layout.spacing;
        int offset = (totalWidth - rowWidth)/2;
        r.left += offset;
        r.right += offset;
    }

    return r;
}

HICON GetHighResIcon(HWND hwnd) {
    HICON hIcon = NULL;

    SendMessageTimeoutA(hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);

    if (!hIcon) {
        hIcon = (HICON)GetClassLongPtrA(hwnd, GCLP_HICON);
    }

    if (!hIcon) {
        SendMessageTimeoutA(hwnd, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, (PDWORD_PTR)&hIcon);
    }

    return hIcon;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowData* wData = (WindowData*)lParam;
    DWORD windowPid;
    GetWindowThreadProcessId(hwnd, &windowPid);
    std::string windowProcessName = GetProcessName(windowPid);
    if (windowProcessName == wData->targetProcessName) {
        if (!IsWindowVisible(hwnd)) return TRUE;

        int cloaked = 0;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;

        LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) return TRUE;

        if (GetWindowTextLength(hwnd) == 0) return TRUE;

        wData->windows.push_back(hwnd);
    }
    return TRUE;
}

static BOOL CALLBACK EnumAllWindowsProc(HWND hwnd, LPARAM lParam) {
    WindowData* wData = (WindowData*)lParam;

    if (!IsWindowVisible(hwnd)) return TRUE;

    if (GetWindowTextLength(hwnd) == 0) return TRUE;

    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    char className[256];
    GetClassNameA(hwnd, className, sizeof(className));
    if (strstr(className, "TrayWnd") || strstr(className, "Progman") || strstr(className, "ControlCenter")) {
        return TRUE;
    }

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) && !(exStyle & WS_EX_APPWINDOW)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != NULL && !(exStyle & WS_EX_APPWINDOW)) return TRUE;

    DWORD pid;
    GetWindowThreadProcessId(hwnd, &pid);
    std::string processName = GetProcessName(pid);

    if (seenProcessNames.find(processName) == seenProcessNames.end()) {
        seenProcessNames.insert(processName);
        wData->windows.push_back(hwnd);
    }

    return TRUE;
}

static LRESULT CALLBACK SwitcherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, hSwitcherBackBrush);

            if (currentMode != SwitcherMode::None && !sessionWindows.empty()) {
                HFONT oldFont = (HFONT)SelectObject(hdc, hSwitcherFont);
                char title[256];
                GetWindowTextA(sessionWindows[sessionIndex], title, sizeof(title));
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                RECT titleRect = { 0, 0, cachedLayout.winW, cachedLayout.titleHeight };
                DrawTextA(hdc, title, -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                RECT highlightRect = GetThumbRect(cachedLayout, sessionIndex, sessionWindows.size());

                if (currentMode == SwitcherMode::AllApps) {
                    for (size_t i = 0; i < sessionWindows.size(); ++i) {
                        RECT r = GetThumbRect(cachedLayout, i, sessionWindows.size());
                        HICON hIcon = GetHighResIcon(sessionWindows[i]);
                        if (hIcon) {
                            int iconSize = (int)(cachedLayout.thumbH * 0.65);
                            int x = r.left + (cachedLayout.thumbW - iconSize) / 2;
                            int y = r.top + (cachedLayout.thumbH - iconSize) / 2;
                            DrawIconEx(hdc, x, y, hIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
                        }
                    }
                }

                InflateRect(&highlightRect, 6, 6);
                HPEN hPen = CreatePen(PS_SOLID, 3, RGB(211, 211, 211)); 
                HGDIOBJ oldPen = SelectObject(hdc, hPen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                RoundRect(hdc, highlightRect.left, highlightRect.top, highlightRect.right, highlightRect.bottom, 15, 15);

                SelectObject(hdc, oldFont);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);

                DeleteObject(hPen);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: {
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void CreateSwitcherUI() {
    if (hSwitcherWindow) return;

    if (!classRegistered) {
        hSwitcherBackBrush = CreateSolidBrush(RGB(25, 25, 25));
        
        WNDCLASSA wndClass {};
        wndClass.lpfnWndProc   = SwitcherWndProc;
        wndClass.hInstance     = GetModuleHandle(NULL);
        wndClass.lpszClassName = "SwitcherCanvas";
        wndClass.hbrBackground = hSwitcherBackBrush;
        wndClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
        
        if (RegisterClassA(&wndClass)) {
            classRegistered = true;
        }
    }

    if (!hSwitcherFont) {
        hSwitcherFont = CreateFontA(-cachedLayout.fontSize, 0, 0, 0, FW_SEMIBOLD, 
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, 
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, "Consolas");
    }

    int x = (GetSystemMetrics(SM_CXSCREEN) - cachedLayout.winW)/2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - cachedLayout.winH)/2;

    hSwitcherWindow = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW, 
        "SwitcherCanvas",
        NULL,
        WS_POPUP | WS_VISIBLE, 
        x, y, cachedLayout.winW, cachedLayout.winH, 
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    SetLayeredWindowAttributes(hSwitcherWindow, 0, 255, LWA_ALPHA);
}

static void UpdateThumbnailGallery() {
    if (sessionWindows.empty()) return;

    if (currentMode == SwitcherMode::AllApps) {
        for (auto t : sessionThumbs) DwmUnregisterThumbnail(t);
        sessionThumbs.clear();
        return; 
    }

    bool isInitialLoad = sessionThumbs.empty();

    DWM_THUMBNAIL_PROPERTIES props {};
    props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    props.fSourceClientAreaOnly = TRUE;
    props.fVisible = TRUE;

    for (size_t i=0; i<sessionWindows.size(); ++i) {
        HTHUMBNAIL hCurrentThumb = NULL;
        if (isInitialLoad) {
            if (SUCCEEDED(DwmRegisterThumbnail(hSwitcherWindow, sessionWindows[i], &hCurrentThumb))) {
                sessionThumbs.push_back(hCurrentThumb);
            } else {
                hCurrentThumb = sessionThumbs[i];
            }
            if (hCurrentThumb) {
                props.opacity = (i == sessionIndex) ? 255 : 170;
                props.rcDestination = GetThumbRect(cachedLayout, i, sessionWindows.size());
                DwmUpdateThumbnailProperties(hCurrentThumb, &props);
            }
        }
    }
}

void HandleArrowNavigation(DWORD vkCode) {
    if (sessionWindows.empty()) {
        return;
    }

    int count = (int)sessionWindows.size();
    int cols = cachedLayout.cols;

    int currentIndex = (int)sessionIndex;
    
    switch (vkCode) {
        case VK_LEFT: {
            currentIndex = (currentIndex - 1 + count) % count;
            break;
        }
        case VK_RIGHT: {
            currentIndex = (currentIndex + 1) % count;
            break;
        }
        case VK_UP: {
            if (currentIndex >= cols) {
                currentIndex -= cols;
            } else {
                int currentCol = currentIndex % cols;
                int lastRowStart = ((count - 1) / cols) * cols;
                int targetIndex = lastRowStart + currentCol;
                if (targetIndex >= count) {
                    currentIndex = count - 1;
                } else {
                    currentIndex = targetIndex;
                }
            }
            break;
        }
        case VK_DOWN: {
            int targetIndex = currentIndex + cols;
            if (targetIndex < count) {
                currentIndex = targetIndex;
            } else {
                int lastRowStart = ((count - 1) / cols) * cols;
                if (currentIndex < lastRowStart) {
                    currentIndex = count - 1;
                } else {
                    currentIndex = currentIndex % cols;
                }
            }
            break;
        }
    }

    sessionIndex = (size_t)currentIndex;
}

void ResetSwitcherSession(DWORD vkCode) {
    if (currentMode == SwitcherMode::None) return;

    if (vkCode != VK_ESCAPE) {
        if (sessionIndex < sessionWindows.size()) {
            HWND target = sessionWindows[sessionIndex];

            if (hSwitcherWindow) ShowWindow(hSwitcherWindow, SW_HIDE);
            if (IsIconic(target)) ShowWindow(target, SW_RESTORE);

            keybd_event(0xFC, 0, 0, 0);
            keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);

            AllowSetForegroundWindow(ASFW_ANY);
            SetForegroundWindow(target);
            SetActiveWindow(target);
        }
    }

    for (auto t : sessionThumbs) DwmUnregisterThumbnail(t);
    sessionThumbs.clear();

    if (hSwitcherWindow) {
        DestroyWindow(hSwitcherWindow);
        hSwitcherWindow = NULL;
    }

    currentMode = SwitcherMode::None;
    sessionIndex = 0;
    sessionWindows.clear();
    seenProcessNames.clear();
}

static void InitializeSwitcher(SwitcherMode mode, HWND anchorWindow) {
    if (!anchorWindow) return;

    bool isSwap = (currentMode != SwitcherMode::None && currentMode != mode);

    DWORD targetPid;
    GetWindowThreadProcessId(anchorWindow, &targetPid);
    if (targetPid == GetCurrentProcessId()) {
        anchorWindow = GetNextWindow(anchorWindow, GW_HWNDNEXT);
        if (!anchorWindow) return;
        GetWindowThreadProcessId(anchorWindow, &targetPid);
    }

    WindowData wData;
    seenProcessNames.clear(); 
    if (mode == SwitcherMode::SameApp) {
        wData.targetProcessName = GetProcessName(targetPid);
        EnumWindows(EnumWindowsProc, (LPARAM)&wData);
    } else {
        EnumWindows(EnumAllWindowsProc, (LPARAM)&wData);
    }

    if (wData.windows.size() <= 1 && mode == SwitcherMode::SameApp) {
        if (isSwap) {
            return;
        }
        currentMode = SwitcherMode::None;
        return;
    }

    if (isSwap) {
        for (auto t : sessionThumbs) DwmUnregisterThumbnail(t);
        sessionThumbs.clear();
        if (hSwitcherWindow) {
            DestroyWindow(hSwitcherWindow);
            hSwitcherWindow = NULL;
        }
    }

    sessionWindows = wData.windows;
    currentMode = mode;

    sessionIndex = 0;
    for (size_t i = 0; i < sessionWindows.size(); ++i) {
        if (sessionWindows[i] == anchorWindow) {
            sessionIndex = (int)i;
            break;
        }
    }

    cachedLayout = CalculateSwitcherLayout((int)sessionWindows.size(), currentMode);
    CreateSwitcherUI();
    UpdateThumbnailGallery();
    
    ShowWindow(hSwitcherWindow, SW_SHOW);
    InvalidateRect(hSwitcherWindow, NULL, TRUE);
}

void AppCycleSwitcher(DWORD vkCode, SwitcherMode requestedMode) {
    if (currentMode != SwitcherMode::None && requestedMode != SwitcherMode::None && requestedMode != currentMode) {
        if (sessionWindows.empty() || sessionIndex >= sessionWindows.size()) {
            InitializeSwitcher(requestedMode, GetForegroundWindow());
            return;
        }

        if (currentMode == SwitcherMode::AllApps) {
            lastAllAppsIndex = sessionIndex;
        }

        HWND anchor = sessionWindows[sessionIndex];
        if (!IsWindow(anchor)) {
            anchor = GetForegroundWindow();
        }
        
        InitializeSwitcher(requestedMode, anchor);
        
        if (currentMode != requestedMode) {
            return; 
        }

        if (requestedMode == SwitcherMode::AllApps) {
            sessionIndex = lastAllAppsIndex;
        }

        sessionIndex = (sessionIndex + 1) % sessionWindows.size();

        UpdateThumbnailGallery();
        InvalidateRect(hSwitcherWindow, NULL, FALSE);
        return;
    }

    if (currentMode == SwitcherMode::None) {
        InitializeSwitcher(requestedMode, GetForegroundWindow());
        
        if (!sessionWindows.empty()) {
            sessionIndex = (sessionIndex + 1) % sessionWindows.size();
        }
        return;
    }

    if (currentMode != SwitcherMode::None && !sessionWindows.empty()) {
        bool isAllAppMode  = (currentMode == SwitcherMode::AllApps && vkCode == Config::allAppsSwitcherKey);
        bool isSameAppMode = (currentMode == SwitcherMode::SameApp && vkCode == Config::sameAppsSwitcherKey);

        if (isAllAppMode || isSameAppMode) {
            sessionIndex = (sessionIndex + 1) % sessionWindows.size();
        }
        else if (vkCode == VK_LEFT || vkCode == VK_RIGHT || vkCode == VK_UP || vkCode == VK_DOWN) {
            HandleArrowNavigation(vkCode);
        }
        UpdateThumbnailGallery();
        InvalidateRect(hSwitcherWindow, NULL, FALSE);
    }
}