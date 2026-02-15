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
    static void* hoveredTarget = nullptr;

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

        layoutMetrics.fontSizeHeader      = (int)(12 * layoutMetrics.scale);
        layoutMetrics.fontSizeLabel       = (int)(11 * layoutMetrics.scale);
        layoutMetrics.fontSizeKeyBinding  = (int)(10 * layoutMetrics.scale);
        layoutMetrics.fontSizeDescription = (int)( 9 * layoutMetrics.scale);

        layoutMetrics.footerHeight = (int)(60 * layoutMetrics.scale);
        layoutMetrics.footerTop    = layoutMetrics.windowH - layoutMetrics.footerHeight;

    }

    static void UpdateFonts() {
        fontAssets.Release();
        fontAssets.labelFont       = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeLabel);
        fontAssets.headerFont      = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeHeader,      Gdiplus::FontStyleBold);
        fontAssets.keyBindingFont  = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeKeyBinding,  Gdiplus::FontStyleBold);
        fontAssets.descriptionFont = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeDescription, Gdiplus::FontStyleRegular);
    }

    static void RegisterZone(int x, int y, int w, int h, ControlType type, void* target) {
        if (type == ControlType::BUTTON || y + h < layoutMetrics.footerTop) {
            RECT r = {x, y, x + w, y + h};
            clickZones.push_back({r, type, target});
        }
    }

    static void DrawLabel(Gdiplus::Graphics& graphics, const std::string& text, int yOffset, int xPos = -1) {
        float finalX = (xPos == -1) ? (float)layoutMetrics.paddingX : (float)xPos;
        float textY = (float)yOffset + (layoutMetrics.rowHeight - layoutMetrics.fontSizeLabel) / 2.0f;
        Gdiplus::SolidBrush textBrush(COL_TEXT);
        std::wstring wText(text.begin(), text.end());
        graphics.DrawString(wText.c_str(), -1, fontAssets.labelFont, Gdiplus::PointF(finalX, textY), &textBrush);
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
        if (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU) wKey = L"ALT";
        else if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) wKey = L"CTRL";
        else if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) wKey = L"SHIFT";
        else if (vkCode == VK_TAB) wKey = L"TAB";
        else if (vkCode == VK_OEM_3) wKey = L"~";
        else if (vkCode == VK_SPACE) wKey = L"SPC";
        else if (vkCode >= 0x30 && vkCode <= 0x5A) wKey = (wchar_t)vkCode;
        else wKey = L"???";

        Gdiplus::Rect rect(x, y, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH);
        graphics.DrawRectangle(&borderPen, rect);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect((float)x, (float)y, (float)layoutMetrics.keyBoxW, (float)layoutMetrics.keyBoxH);
        graphics.DrawString(wKey.c_str(), -1, fontAssets.keyBindingFont, layoutRect, &format, &textBrush);
    }

    static void AddAppCheckbox(Gdiplus::Graphics& graphics, const std::string& appName, int x, int y) {
        bool isChecked = (Config::tabbedApps.find(appName) != Config::tabbedApps.end());
    
        int boxSize = (int)(16 * layoutMetrics.scale);
        int boxY = y + (layoutMetrics.rowHeight - boxSize) / 2;
    
        Gdiplus::Rect rect(x, boxY, boxSize, boxSize);
        Gdiplus::Pen borderPen(isChecked ? COL_ACCENT : Gdiplus::Color(150, 150, 150), 1.5f);
    
        Gdiplus::GraphicsPath path;
        float r = 4.0f * layoutMetrics.scale;
        path.AddArc((float)rect.X, (float)rect.Y, r, r, 180, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y, r, r, 270, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y + rect.Height - r, r, r, 0, 90);
        path.AddArc((float)rect.X, (float)rect.Y + rect.Height - r, r, r, 90, 90);
        path.CloseFigure();
    
        graphics.DrawPath(&borderPen, &path);

        if (isChecked) {
            Gdiplus::SolidBrush accentBrush(COL_ACCENT);
            graphics.FillPath(&accentBrush, &path); 
        }

        int textX = x + boxSize + (int)(8 * layoutMetrics.scale);
        DrawLabel(graphics, appName, y, textX);

        int totalWidth = (int)(160 * layoutMetrics.scale); 
        RegisterZone(x, y, totalWidth, layoutMetrics.rowHeight, ControlType::CHECKBOX, (void*)&appName);
    }

    static void AddToggleRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset, bool* target) {
        DrawLabel(graphics, label, yOffset);
        DrawToggle(graphics, layoutMetrics.columnX, yOffset, *target);
        RegisterZone(layoutMetrics.columnX, yOffset, layoutMetrics.toggleW, layoutMetrics.toggleH, ControlType::TOGGLE, target);
        yOffset += layoutMetrics.rowHeight;
    }

    static void AddBindingRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset, unsigned int* modTarget, unsigned int* keyTarget = nullptr) {
        DrawLabel(graphics, label, yOffset);    

        int x = 0;
        if (modTarget) {
            x = layoutMetrics.columnX;
            DrawKeyBox(graphics, x, yOffset, *modTarget, (currentlyRecording == modTarget));
            RegisterZone(x, yOffset, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH, ControlType::KEYBOX, modTarget);
        }
    
        if (keyTarget) {
            x += layoutMetrics.keyBoxW + (int)(10 * layoutMetrics.scale);
            DrawLabel(graphics, "+", yOffset, x - 5);
        
            DrawKeyBox(graphics, x, yOffset, *keyTarget, (currentlyRecording == keyTarget));
            RegisterZone(x, yOffset, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH, ControlType::KEYBOX, keyTarget);
        }
        
        yOffset += layoutMetrics.rowHeight;
    }

    static void AddDescription(Gdiplus::Graphics& g, const std::string& text, int& yOffset) {
        Gdiplus::SolidBrush subBrush(COL_SUBTEXT);
        std::wstring wText(text.begin(), text.end());
        g.DrawString(wText.c_str(), -1, fontAssets.descriptionFont, Gdiplus::PointF((float)layoutMetrics.paddingX, (float)yOffset), &subBrush);
        yOffset += (int)(25 * layoutMetrics.scale);
    }

    static void DrawSeparator(Gdiplus::Graphics& g, int& yOffset) {
        yOffset += (int)(10 * layoutMetrics.scale);
        Gdiplus::Pen pen(Gdiplus::Color(50, 50, 50), 1.0f);
        g.DrawLine(&pen, layoutMetrics.paddingX, yOffset, layoutMetrics.windowW - layoutMetrics.paddingX, yOffset);
        yOffset += (int)(20 * layoutMetrics.scale);
    }

    static void DrawButtonText(Gdiplus::Graphics& graphics, const std::string& text, Gdiplus::Rect btnRect) {
        std::wstring wText(text.begin(), text.end());
    
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::SolidBrush textBrush(COL_TEXT);
    
        Gdiplus::RectF textRect((float)btnRect.X, (float)btnRect.Y, (float)btnRect.Width, (float)btnRect.Height);
    
        graphics.DrawString(wText.c_str(), -1, fontAssets.labelFont, textRect, &format, &textBrush);
}

    static void DrawFooterButtons(Gdiplus::Graphics& graphics, int windowWidth) {
        int btnW = (int)(100 * layoutMetrics.scale);
        int btnH = (int)(32 * layoutMetrics.scale);
        int btnY = layoutMetrics.footerTop + (layoutMetrics.footerHeight - btnH) / 2;

        int margin = (int)(20 * layoutMetrics.scale);
        int exitX = windowWidth - btnW - margin;
        int saveX = exitX - btnW - (int)(10 * layoutMetrics.scale);
        int defX  = saveX - btnW - (int)(10 * layoutMetrics.scale);

        auto DrawBtn = [&](int x, int y, int w, int h, const std::string& label, void* target, Gdiplus::Color baseColor) {
            Gdiplus::Rect rect(x, y, w, h);
            Gdiplus::Color drawingColor = baseColor;
            if (hoveredTarget == target) {
                drawingColor = Gdiplus::Color(
                    baseColor.GetA(),
                    (BYTE)(255 < baseColor.GetR() + 30 ? 255 : baseColor.GetR() + 30),
                    (BYTE)(255 < baseColor.GetG() + 30 ? 255 : baseColor.GetG() + 30),
                    (BYTE)(255 < baseColor.GetB() + 30 ? 255 : baseColor.GetB() + 30)
                );
            }
            Gdiplus::SolidBrush br(drawingColor);
            graphics.FillRectangle(&br, rect);
            DrawButtonText(graphics, label, rect);
            RegisterZone(x, y, w, h, ControlType::BUTTON, target);
        };

        DrawBtn(defX,  btnY, btnW, btnH, "Defaults", (void*)1, Gdiplus::Color(255, 60, 60, 60));
        DrawBtn(saveX, btnY, btnW, btnH, "Save",     (void*)2, COL_ACCENT);
        DrawBtn(exitX, btnY, btnW, btnH, "Exit",     (void*)3, Gdiplus::Color(255, 180, 50, 50));
    }

    static void Render(HDC hdc) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        
        graphics.Clear(COL_BG);
        
        clickZones.clear();

        int yOffset = layoutMetrics.headerSpacing;

        AddToggleRow(graphics, "Enable Task Switcher", yOffset, &Config::enableTaskSwitcher);
        AddBindingRow(graphics, "All Apps Shortcut", yOffset, &Config::allAppsSwitcherMod, &Config::allAppsSwitcherKey);
        AddBindingRow(graphics, "Same App Shortcut", yOffset, &Config::sameAppsSwitcherMod, &Config::sameAppsSwitcherKey);

        DrawSeparator(graphics, yOffset);

        AddToggleRow(graphics, "VS Code Launcher", yOffset, &Config::enableVSCodeLauncher);
        AddToggleRow(graphics, "WSL Terminal Launcher", yOffset, &Config::enableWSLTerminalLauncher);
        AddDescription(graphics, "* Launchers use mandatory Ctrl + Alt modifiers.", yOffset);
        AddBindingRow(graphics, "VS Code Key", yOffset, &Config::VSCodeLauncherKey);
        AddBindingRow(graphics, "WSL Terminal Key", yOffset, &Config::WSLTerminalLauncherKey);

        DrawSeparator(graphics, yOffset);

        AddToggleRow(graphics, "Enable Tab Switcher (Alt+Number)", yOffset, &Config::enableTabSwitcher);
        AddDescription(graphics, "Select apps to enable tab switching:", yOffset);
        int startX = layoutMetrics.paddingX;
        int currentX = startX;
        int colWidth = (int)(160 * layoutMetrics.scale);
        for (size_t i = 0; i < Config::DEFAULT_TAB_APPS.size(); ++i) {
            if (i > 0 && i % 3 == 0) {
                yOffset += layoutMetrics.rowHeight;
                currentX = startX;
            }
            AddAppCheckbox(graphics, Config::DEFAULT_TAB_APPS[i], currentX, yOffset);
            currentX += colWidth;
        }
        yOffset += layoutMetrics.rowHeight;

        Gdiplus::Pen sepPen(Gdiplus::Color(50, 50, 50), 1.0f);
        graphics.DrawLine(&sepPen, 0, layoutMetrics.footerTop, layoutMetrics.windowW, layoutMetrics.footerTop);

        Gdiplus::SolidBrush footerBg(COL_BG); 
        graphics.FillRectangle(&footerBg, 0, layoutMetrics.footerTop + 1, layoutMetrics.windowW, layoutMetrics.footerHeight);
        DrawFooterButtons(graphics, layoutMetrics.windowW);
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

                Render(memDC);

                BitBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, memDC, 0, 0, SRCCOPY);

                DeleteObject(memBitmap);
                DeleteDC(memDC);
                EndPaint(hWnd, &ps);

                return 0;
            }
            case WM_LBUTTONDOWN: {
                POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                for (const auto& zone : clickZones) {
                    if (PtInRect(&zone.area, pt)) {
                        switch (zone.type) {
                            case ControlType::KEYBOX: {
                                currentlyRecording = zone.target;
                                InvalidateRect(hWnd, NULL, FALSE);
                                break;
                            }
                            case ControlType::TOGGLE: {
                                bool* val = (bool*)zone.target;
                                *val = !(*val);
                                InvalidateRect(hWnd, NULL, FALSE);
                                break;
                            }
                            case ControlType::CHECKBOX: {
                                std::string appName = *(std::string*)zone.target;
                                if (Config::tabbedApps.count(appName)) {
                                    Config::tabbedApps.erase(appName);
                                } else {
                                    Config::tabbedApps.insert(appName);
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                                break;
                            }
                            case ControlType::BUTTON: {
                                int buttonId = (int)(intptr_t)zone.target;
                                switch (buttonId) {
                                    case 1:
                                        Config::SaveDefaultConfig(Config::GetConfigPath());
                                        break;
                                    case 2:
                                        // Config::SaveConfig();
                                        break;
                                    case 3:
                                        PostMessage(hWnd, WM_CLOSE, 0, 0);
                                        break;
                                }
                                InvalidateRect(hWnd, NULL, FALSE);
                                break;
                            }
                        }
                        break;
                    }
                }
                return 0;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (currentlyRecording) {
                    unsigned int* target = (unsigned int*)currentlyRecording;
                    unsigned int vk = (unsigned int)wParam;

                    if (vk == VK_LMENU    || vk == VK_RMENU)    vk = VK_MENU;
                    if (vk == VK_LCONTROL || vk == VK_RCONTROL) vk = VK_CONTROL;
                    if (vk == VK_LSHIFT   || vk == VK_RSHIFT)   vk = VK_SHIFT;

                    *target = vk;                    
                    currentlyRecording = nullptr;
                    InvalidateRect(hWnd, NULL, FALSE);
                    return 0;
                }
                break;
            }
            case WM_NCHITTEST: {
                LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
                if (hit == HTCLIENT) {
                    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ScreenToClient(hWnd, &pt);
                    if (pt.y < 40) return HTCAPTION;
                }
                return hit;
            }
            case WM_MOUSEMOVE: {
                int x = GET_X_LPARAM(lParam);
                int y = GET_Y_LPARAM(lParam);
                void* lastHover = hoveredTarget;
                hoveredTarget = nullptr;
                for (const auto& zone : clickZones) {
                    if (x >= zone.area.left && x <= zone.area.right && y >= zone.area.top && y <= zone.area.bottom) {
                        hoveredTarget = zone.target;
                        break;
                    }
                }
                if (hoveredTarget != lastHover) {
                    InvalidateRect(hWnd, NULL, FALSE);
                }
                return 0;
            }
            case WM_ERASEBKGND: {
                return 1;
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
            WS_EX_APPWINDOW | WS_EX_LAYERED,
            szClassName,
            "Kinesis Configuration",
            WS_POPUP | WS_VISIBLE | WS_SYSMENU,
            layoutMetrics.windowX, layoutMetrics.windowY,
            layoutMetrics.windowW, layoutMetrics.windowH,
            NULL, NULL,
            GetModuleHandle(NULL),
            NULL
        );

        int cornerRadius = layoutMetrics.windowH * 0.06;
        HRGN hConfigRgn = CreateRoundRectRgn(0, 0, layoutMetrics.windowW, layoutMetrics.windowH, cornerRadius, cornerRadius);
        SetWindowRgn(hConfigWindow, hConfigRgn, TRUE);

        if (hConfigWindow) {
            AllowSetForegroundWindow(ASFW_ANY);
            keybd_event(0xFC, 0, 0, 0);
            keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
            SmoothShowWindow(hConfigWindow);
            SetForegroundWindow(hConfigWindow);
            SetActiveWindow(hConfigWindow);
            return;
        }
    }

    bool IsConfigUIOpen() {
        return hConfigWindow != NULL;
    }
}