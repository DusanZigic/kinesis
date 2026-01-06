#include "common.hpp"
#include "appcycleswitcher.hpp"

static bool classRegistered = false;
static HWND hSwitcherWindow = NULL;
static HFONT hSwitcherFont = NULL;
static HBRUSH hSwitcherBackBrush = NULL;

static std::vector<HTHUMBNAIL> sessionThumbs;

static SwitcherLayout cachedLayout;

static bool isAltTildeSession = false;
static std::vector<HWND> sessionWindows;
static size_t sessionIndex = 0;

static const int MAX_COLS = 4;
static const double MAX_SWITCHER_RELATIVE_WIDTH = 0.85;

bool IsSwitcherActive() {
    return isAltTildeSession;
}

SwitcherLayout CalculateSwitcherLayout(size_t count) {
    SwitcherLayout layout;

    double screenW = GetSystemMetrics(SM_CXSCREEN);
    double screenH = GetSystemMetrics(SM_CYSCREEN);
    double  aspect = screenW/screenH;

    layout.margin      = (int)(0.013*screenW);
    layout.spacing     = (int)(0.010*screenW);
    layout.titleHeight = (int)(0.046*screenH);
    layout.fontSize    = (int)(0.450*layout.titleHeight);

    layout.cols = (count < MAX_COLS) ? (int)count : MAX_COLS;
    layout.rows = (int)std::ceil((double)count / layout.cols);

    double maxSwitcherWidth = screenW*MAX_SWITCHER_RELATIVE_WIDTH;

    int usableWidth = (int)maxSwitcherWidth - 2*layout.margin - (layout.cols - 1)*layout.spacing;
    
    layout.thumbW = usableWidth / layout.cols;
    layout.thumbH = (int)(layout.thumbW / aspect);

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

static LRESULT CALLBACK SwitcherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            FillRect(hdc, &ps.rcPaint, hSwitcherBackBrush);

            if (isAltTildeSession && !sessionWindows.empty()) {
                HFONT oldFont = (HFONT)SelectObject(hdc, hSwitcherFont);
                char title[256];
                GetWindowTextA(sessionWindows[sessionIndex], title, sizeof(title));
                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                RECT titleRect = {cachedLayout.margin, 0, cachedLayout.winH - cachedLayout.margin, cachedLayout.titleHeight};
                DrawTextA(hdc, title, -1, &titleRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                RECT highlightRect = GetThumbRect(cachedLayout, sessionIndex, sessionWindows.size());
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
    for (auto t : sessionThumbs) {
        DwmUnregisterThumbnail(t);
    }
    sessionThumbs.clear();

    if (sessionWindows.empty()) {
        return;
    }

    for (size_t i=0; i<sessionWindows.size(); ++i) {
        HTHUMBNAIL hCurrentThumb = NULL;
        if (SUCCEEDED(DwmRegisterThumbnail(hSwitcherWindow, sessionWindows[i], &hCurrentThumb))) {
            DWM_THUMBNAIL_PROPERTIES props {};
            props.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY;
            props.fVisible = TRUE;
            props.rcDestination = GetThumbRect(cachedLayout, i, sessionWindows.size());
            props.opacity = (i == sessionIndex) ? 255 : 170;

            DwmUpdateThumbnailProperties(hCurrentThumb, &props);
            sessionThumbs.push_back(hCurrentThumb);
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

void ResetAltTildeSession(DWORD vkCode) {
    if (!isAltTildeSession) return;

    if (vkCode == VK_MENU || vkCode == VK_RETURN) {
        if (sessionIndex < sessionWindows.size()) {
            HWND target = sessionWindows[sessionIndex];
            
            if (IsIconic(target)) ShowWindow(target, SW_RESTORE);
            AllowSetForegroundWindow(ASFW_ANY);
            SetForegroundWindow(target);
            SetActiveWindow(target);
        }
        for (auto t : sessionThumbs) {
            DwmUnregisterThumbnail(t);
        }
        sessionThumbs.clear();
        if (hSwitcherWindow) {
            DestroyWindow(hSwitcherWindow);
            hSwitcherWindow = NULL;
        }
        isAltTildeSession = false;
        sessionIndex = 0;
    }
}

void AppCycleSwitcher(DWORD vkCode) {
    if (!isAltTildeSession) {
        HWND activeWindow = GetForegroundWindow();
        if (activeWindow) {
            DWORD processId;
            GetWindowThreadProcessId(activeWindow, &processId);
            std::string windowProcessName = GetProcessName(processId);
            WindowData wData;
            wData.targetProcessName = windowProcessName;
            EnumWindows(EnumWindowsProc, (LPARAM)&wData);
            
            if (wData.windows.size() > 1) {
                sessionWindows = wData.windows;
                sessionIndex = 0;
                isAltTildeSession = true;
                cachedLayout = CalculateSwitcherLayout(sessionWindows.size());
                CreateSwitcherUI();
                ShowWindow(hSwitcherWindow, SW_SHOW);
                UpdateWindow(hSwitcherWindow);
                UpdateThumbnailGallery();
            }
        }
    }
    
    if (isAltTildeSession && !sessionWindows.empty()) {
        if (vkCode == VK_OEM_3 || vkCode == 0) {
            sessionIndex = (sessionIndex + 1) % sessionWindows.size();
        }
        else if (vkCode == VK_LEFT || vkCode == VK_RIGHT ||  vkCode == VK_UP   || vkCode == VK_DOWN) {
            HandleArrowNavigation(vkCode);
        }
        UpdateThumbnailGallery();
        InvalidateRect(hSwitcherWindow, NULL, TRUE);
    }
}