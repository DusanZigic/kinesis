#include "common.hpp"
#include "config.hpp"
#include "configui.hpp"

namespace ConfigUI {
    static HWND hConfigWindow = NULL;
    static const char* szClassName = "KinesisConfigWindow";

    static const Gdiplus::Color COL_BG(255, 30, 30, 30);
    static const Gdiplus::Color COL_ACCENT(255, 0, 120, 215);
    static const Gdiplus::Color COL_TEXT(255, 240, 240, 240);
    static const Gdiplus::Color COL_SUBTEXT(255, 150, 150, 150);

    static LayoutMetrics layoutMetrics;
    static FontAssets fontAssets;

    static std::vector<ClickZone> clickZones;

    static void* currentlyRecording = nullptr;

    static void UpdateLayoutMetrics() {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        layoutMetrics.scale = 1.5f * (float)screenH / 1080.0f;

        layoutMetrics.windowH = (int)(screenH * 0.75f);
        layoutMetrics.windowW = (int)(layoutMetrics.windowH * 0.8f);
        layoutMetrics.windowY = (screenH - layoutMetrics.windowH) / 2;
        layoutMetrics.windowX = (screenW - layoutMetrics.windowW) / 2;

        layoutMetrics.paddingX       = (int)(30 * layoutMetrics.scale);
        layoutMetrics.columnX        = (int)(layoutMetrics.windowW * 0.65f);

        layoutMetrics.upperMargin    = (int)(30 * layoutMetrics.scale);
        layoutMetrics.headerSpacing  = (int)(40 * layoutMetrics.scale);
        layoutMetrics.rowHeight      = (int)(40 * layoutMetrics.scale);
        layoutMetrics.sectionSpacing = (int)(45 * layoutMetrics.scale);

        layoutMetrics.toggleW = (int)(44 * layoutMetrics.scale);
        layoutMetrics.toggleH = (int)(22 * layoutMetrics.scale);
        layoutMetrics.keyBoxW = (int)(35 * layoutMetrics.scale);
        layoutMetrics.keyBoxH = (int)(28 * layoutMetrics.scale);

        layoutMetrics.fontSizeHeader     = (int)(12 * layoutMetrics.scale);
        layoutMetrics.fontSizeLabel      = (int)(11 * layoutMetrics.scale);
        layoutMetrics.fontSizeKeyBinding = (int)(10 * layoutMetrics.scale);
    }

    static void UpdateFonts() {
        fontAssets.Release();
        fontAssets.labelFont      = new Gdiplus::Font(L"Segoe UI", (float)layoutMetrics.fontSizeLabel);
        fontAssets.headerFont     = new Gdiplus::Font(L"Segoe UI", (float)layoutMetrics.fontSizeHeader,     Gdiplus::FontStyleBold);
        fontAssets.keyBindingFont = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeKeyBinding, Gdiplus::FontStyleBold);
    }

    static void DrawToggle(Gdiplus::Graphics& graphics, int x, int y, bool enabled) {
        Gdiplus::SmoothingMode prevMode = graphics.GetSmoothingMode();
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int w = layoutMetrics.toggleW;
        int h = layoutMetrics.toggleH;

        Gdiplus::SolidBrush bgBrush(enabled ? COL_ACCENT : Gdiplus::Color(100, 100, 100));
        graphics.FillPie(&bgBrush, x, y, h, h, 90, 180);
        graphics.FillPie(&bgBrush, x + w - h, y, h, h, 270, 180);
        graphics.FillRectangle(&bgBrush, x + h / 2, y, w - h, h);
        
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color::White);
        int margin = (int)(2 * layoutMetrics.scale);
        int knobSize = h - (margin * 2);
        int knobX = enabled ? (x + w - h + margin) : (x + margin);
        graphics.FillEllipse(&knobBrush, knobX, y + margin, knobSize, knobSize);

        graphics.SetSmoothingMode(prevMode);
    }

    static void DrawKeyBox(Gdiplus::Graphics& graphics, int x, int y, unsigned int vkCode, bool isRecording) {
        Gdiplus::SolidBrush textBrush(COL_TEXT);
        Gdiplus::Pen borderPen(isRecording ? COL_ACCENT : Gdiplus::Color(80, 80, 80), (float)(1 * layoutMetrics.scale));

        std::wstring wKey;
        if (vkCode == VK_TAB) wKey = L"TAB";
        else if (vkCode == 192) wKey = L"~";
        else if (vkCode == VK_SPACE) wKey = L"SPC";
        else if (vkCode >= 'A' && vkCode <= 'Z') wKey = (wchar_t)vkCode;
        else wKey = L"?";

        Gdiplus::Rect rect(x, y, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH);
        graphics.DrawRectangle(&borderPen, rect);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect((float)x, (float)y, (float)layoutMetrics.keyBoxW, (float)layoutMetrics.keyBoxH);
        graphics.DrawString(wKey.c_str(), -1, fontAssets.keyBindingFont, layoutRect, &format, &textBrush);
    }

    static void RegisterZone(int x, int y, int w, int h, ControlType type, void* target) {
        RECT r = { x, y, x + w, y + h };
        clickZones.push_back({r, type, target});
    }

    static void AddPropertyRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset,  bool* toggleTarget, unsigned int* keyTarget = nullptr) {
        Gdiplus::SolidBrush textBrush(COL_TEXT);

        std::wstring wLabel(label.begin(), label.end());
        float textY = (float)yOffset + (layoutMetrics.rowHeight - layoutMetrics.fontSizeLabel) / 2.0f;
        graphics.DrawString(wLabel.c_str(), -1, fontAssets.labelFont, Gdiplus::PointF((float)layoutMetrics.paddingX, textY), &textBrush);

        int toggleY = yOffset + (layoutMetrics.rowHeight - layoutMetrics.toggleH) / 2;
        int keyBoxY = yOffset + (layoutMetrics.rowHeight - layoutMetrics.keyBoxH) / 2;

        if (toggleTarget) {
            DrawToggle(graphics, layoutMetrics.columnX, toggleY, *toggleTarget);
            RegisterZone(layoutMetrics.columnX, toggleY, layoutMetrics.toggleW, layoutMetrics.toggleH, TOGGLE, toggleTarget);
        }

        if (keyTarget) {
            int xPos = layoutMetrics.columnX + layoutMetrics.toggleW + (int)(15 * layoutMetrics.scale);
            DrawKeyBox(graphics, xPos, keyBoxY, *keyTarget, (currentlyRecording == keyTarget));
            RegisterZone(xPos, keyBoxY, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH, KEYBOX, keyTarget);
        }

        yOffset += layoutMetrics.rowHeight;
    }

    static void Render(HWND hWnd, HDC hdc) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        int width  = clientRect.right  - clientRect.left;
        int height = clientRect.bottom - clientRect.top;

        Gdiplus::SolidBrush bgBrush(COL_BG);
        graphics.FillRectangle(&bgBrush, 0, 0, width, height);

        clickZones.clear();

        int yOffset = layoutMetrics.headerSpacing;
        Gdiplus::SolidBrush accentBrush(COL_ACCENT);

        graphics.DrawString(L"SWITCHERS", -1, fontAssets.headerFont, Gdiplus::PointF((float)layoutMetrics.paddingX, (float)yOffset), &accentBrush);
        yOffset += layoutMetrics.headerSpacing;
        AddPropertyRow(graphics, "Enable Task Switcher", yOffset, &Config::enableTaskSwitcher);
        AddPropertyRow(graphics, "Enable Tab Switcher", yOffset, &Config::enableTabSwitcher);

        yOffset += layoutMetrics.sectionSpacing;

        graphics.DrawString(L"LAUNCHERS", -1, fontAssets.headerFont, Gdiplus::PointF((float)layoutMetrics.paddingX, (float)yOffset), &accentBrush);
        yOffset += layoutMetrics.headerSpacing;
        AddPropertyRow(graphics, "VS Code Launcher",      yOffset, &Config::enableVSCodeLauncher,      &Config::VSCodeLauncherKey);
        AddPropertyRow(graphics, "WSL Terminal Launcher", yOffset, &Config::enableWSLTerminalLauncher, &Config::WSLTerminalLauncherKey);
    }

    LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
            case WM_CLOSE: {
                DestroyWindow(hWnd);
                hConfigWindow = NULL;
                return 0;
            }
            case WM_DESTROY: {
                fontAssets.Release();
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);

                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBitmap = CreateCompatibleBitmap(hdc, ps.rcPaint.right, ps.rcPaint.bottom);
                SelectObject(memDC, memBitmap);

                Render(hWnd, memDC);

                BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, memDC, 0, 0, SRCCOPY);

                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(hWnd, &ps);

                return 0;
            }
            case WM_LBUTTONDOWN: {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                POINT pt = {x, y};

                for (const auto& zone : clickZones) {
                    if (PtInRect(&zone.area, pt)) {
                        if (zone.type == ControlType::KEYBOX) {
                            currentlyRecording = zone.target;
                            InvalidateRect(hWnd, NULL, FALSE);
                        } else if (zone.type == ControlType::TOGGLE) {
                            bool* val = (bool*)zone.target;
                            *val = !(*val);
                            InvalidateRect(hWnd, NULL, FALSE);
                        }
                        break;
                    }

                }
                return 0;
            }
            case WM_KEYDOWN: {
                if (currentlyRecording) {
                    unsigned int* targetVk = (unsigned int*)currentlyRecording;
                    *targetVk = (unsigned int)wParam;
                    currentlyRecording = nullptr;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                break;
            }
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    void OpenConfigUI() {
        if (hConfigWindow) {
            ShowWindow(hConfigWindow, SW_SHOW);
            SetForegroundWindow(hConfigWindow);
            return;
        }

        WNDCLASSEXA wc {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = szClassName;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassExA(&wc);

        UpdateLayoutMetrics();
        UpdateFonts();

        hConfigWindow = CreateWindowExA(
            WS_EX_TOPMOST,
            szClassName,
            "Kinesis Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            layoutMetrics.windowX, layoutMetrics.windowY,
            layoutMetrics.windowW, layoutMetrics.windowH,
            NULL, NULL,
            GetModuleHandle(NULL),
            NULL
        );

        if (hConfigWindow) {
            ShowWindow(hConfigWindow, SW_SHOW);
            UpdateWindow(hConfigWindow);
            return;
        }
    }

    bool IsConfigUIOpen() {
        return hConfigWindow != NULL;
    }
}