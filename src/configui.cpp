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

    static const int PADDING_X = 30;
    static const int SECTION_SPACING = 40;
    static const int SUBSECTION_SPACING = 25;
    static const int COLUMN_X = 350;
    static const int ROW_HEIGHT = 40;

    static std::vector<ClickZone> clickZones;

    static void* currentlyRecording = nullptr;

    static void DrawToggle(Gdiplus::Graphics& graphics, int x, int y, bool enabled) {
        Gdiplus::SmoothingMode prevMode = graphics.GetSmoothingMode();
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int w = 40, h = 20;
        Gdiplus::Rect pill(x, y + 5, w, h);

        Gdiplus::SolidBrush bgBrush(enabled ? COL_ACCENT : Gdiplus::Color(100, 100, 100));
        graphics.FillPie(&bgBrush, x, y + 5, h, h, 90, 180);
        graphics.FillPie(&bgBrush, x + w - h, y + 5, h, h, 270, 180);
        graphics.FillRectangle(&bgBrush, x + h/2, y + 5, w - h, h);

        Gdiplus::SolidBrush knobBrush(Gdiplus::Color::White);
        int knobX = enabled ? (x + w - h + 2) : (x + 2);
        graphics.FillEllipse(&knobBrush, knobX, y + 7, h - 4, h - 4);

        graphics.SetSmoothingMode(prevMode);
    }

    static void DrawKeyBox(Gdiplus::Graphics& graphics, int x, int y, unsigned int vkCode, bool isRecording) {
        Gdiplus::Font font(L"Consolas", 10, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush textBrush(COL_TEXT);
        Gdiplus::Pen borderPen(isRecording ? COL_ACCENT : Gdiplus::Color(80, 80, 80), 1);

        std::wstring wKey;
        if (vkCode == VK_TAB) wKey = L"TAB";
        else if (vkCode == 192) wKey = L"~";
        else if (vkCode == VK_SPACE) wKey = L"SPC";
        else wKey = (wchar_t)vkCode;

        graphics.DrawRectangle(&borderPen, x, y, 30, 25);
        graphics.DrawString(wKey.c_str(), -1, &font, Gdiplus::PointF(x + 8, y + 4), &textBrush);
    }

    static void RegisterZone(int x, int y, int w, int h, ControlType type, void* target) {
        RECT r = { x, y, x + w, y + h };
        clickZones.push_back({r, type, target});
    }

    static void AddPropertyRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset,  bool* toggleTarget, unsigned int* keyTarget = nullptr) {
        Gdiplus::Font font(L"Consolas", 10);
        Gdiplus::SolidBrush textBrush(COL_TEXT);

        std::wstring wLabel(label.begin(), label.end());
        graphics.DrawString(wLabel.c_str(), -1, &font, Gdiplus::PointF(PADDING_X, (float)yOffset), &textBrush);

        if (toggleTarget) {
            DrawToggle(graphics, COLUMN_X, yOffset, *toggleTarget);
            RegisterZone(COLUMN_X, yOffset, 40, 25, TOGGLE, toggleTarget);
        }

        if (keyTarget) {
            int xPos = COLUMN_X + 60;
            bool isRecording = (currentlyRecording == keyTarget);
            DrawKeyBox(graphics, xPos, yOffset, *keyTarget, isRecording);
            RegisterZone(xPos, yOffset, 35, 25, KEYBOX, keyTarget);
        }

        yOffset += ROW_HEIGHT;
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

        int yOffset = 20;
        Gdiplus::Font sectionFont(L"Consolas", 9, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush accentBrush(COL_ACCENT);

        graphics.DrawString(L"SWITCHERS", -1, &sectionFont, Gdiplus::PointF(PADDING_X, (float)yOffset), &accentBrush);
        yOffset += 30;
        AddPropertyRow(graphics, "Enable Task Switcher", yOffset, &Config::enableTaskSwitcher);
        AddPropertyRow(graphics, "Enable Tab Switcher", yOffset, &Config::enableTabSwitcher);

        yOffset += SECTION_SPACING;

        graphics.DrawString(L"LAUNCHERS", -1, &sectionFont, Gdiplus::PointF(PADDING_X, (float)yOffset), &accentBrush);
        yOffset += 30;
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

        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int winW = 500;
        int winH = 600;

        hConfigWindow = CreateWindowExA(
            WS_EX_TOPMOST,
            szClassName,
            "Kinesis Settings",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            (screenW - winW) / 2,
            (screenH - winH) / 2,
            winW, winH,
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