#include "common.hpp"
#include "config.hpp"
#include "configui.hpp"

namespace ConfigUI {
    static HWND hConfigWindow = NULL;
    static const char* szClassName = "KinesisConfigWindow";

    const Gdiplus::Color COL_BG(255, 30, 30, 30);
    const Gdiplus::Color COL_SURFACE(255, 45, 45, 45);
    const Gdiplus::Color COL_SELECTED(255, 200, 200, 200);
    const Gdiplus::Color COL_HOVER(255, 70, 70, 70);
    const Gdiplus::Color COL_TEXT_DIM(255, 160, 160, 160);
    const Gdiplus::Color COL_TEXT_BRIGHT(255, 255, 255, 255);
    const Gdiplus::Color COL_SEPARATOR(255, 50, 50, 50);
    const Gdiplus::Color COL_EXIT_RED(255, 180, 50, 50);

    static LayoutMetrics layoutMetrics;
    static FontAssets fontAssets;

    static std::vector<ClickZone> clickZones;

    static void* currentlyRecording = nullptr;
    static void* hoveredTarget = nullptr;

    static float currentScrollY = 0.0f;
    static float targetScrollY = 0.0f;
    static int maxScroll = 0;
    static const int SCROLL_STEP = 40;
    static const float LERP_FACTOR = 0.35f;

    static void UpdateLayoutMetrics() {
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);

        layoutMetrics.scale = 1.5f * (float)screenH / 1080.0f;

        layoutMetrics.windowW = (int)(520 * layoutMetrics.scale);
        layoutMetrics.windowH = (int)(600 * layoutMetrics.scale);
        layoutMetrics.windowY = (screenH - layoutMetrics.windowH) / 2;
        layoutMetrics.windowX = (screenW - layoutMetrics.windowW) / 2;

        layoutMetrics.paddingX = (int)(30 * layoutMetrics.scale);

        layoutMetrics.upperMargin    = (int)(32 * layoutMetrics.scale);
        layoutMetrics.rowHeight      = (int)(32 * layoutMetrics.scale);
        layoutMetrics.sectionSpacing = (int)(37 * layoutMetrics.scale);

        layoutMetrics.toggleW = (int)(34 * layoutMetrics.scale);
        layoutMetrics.toggleH = (int)(14 * layoutMetrics.scale);
        layoutMetrics.keyBoxW = (int)(36 * layoutMetrics.scale);
        layoutMetrics.keyBoxH = (int)(20 * layoutMetrics.scale);

        int twoKeysWidth  = 2*layoutMetrics.keyBoxW + layoutMetrics.paddingX;
        int reservedSpace = (twoKeysWidth > layoutMetrics.toggleW ? twoKeysWidth : layoutMetrics.toggleW) + (int)(20 * layoutMetrics.scale);
        layoutMetrics.labelWidth = (float)(layoutMetrics.windowW - layoutMetrics.paddingX - reservedSpace - layoutMetrics.paddingX);

        layoutMetrics.fontSizeLabel       = (int)(11 * layoutMetrics.scale);
        layoutMetrics.fontSizeKeyBinding  = (int)(10 * layoutMetrics.scale);
        layoutMetrics.fontSizeCheckBox    = (int)(10 * layoutMetrics.scale);
        layoutMetrics.fontSizeDescription = (int)( 9 * layoutMetrics.scale);

        layoutMetrics.footerHeight = (int)(60 * layoutMetrics.scale);
        layoutMetrics.footerTop    = layoutMetrics.windowH - layoutMetrics.footerHeight;

        layoutMetrics.borderPenWidth    = 1.5f * (float)layoutMetrics.scale;
        layoutMetrics.separatorPenWidth = 1.0f * (float)layoutMetrics.scale;

        layoutMetrics.scrollBarW = (int)(5 * layoutMetrics.scale);
        layoutMetrics.scrollBarMargin = (int)(4 * layoutMetrics.scale);
        layoutMetrics.scrollBarUpperPadding = (int)(5 * layoutMetrics.scale);
        layoutMetrics.scrollBarLowerPadding = (int)(10 * layoutMetrics.scale);
        int reservedForScroll = (maxScroll > 0) ? (layoutMetrics.scrollBarW + layoutMetrics.scrollBarMargin * 2) : 0;
        layoutMetrics.labelWidth -= (float)reservedForScroll;
    }

    static void UpdateFonts() {
        fontAssets.Release();
        fontAssets.labelFont       = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeLabel);
        fontAssets.keyBindingFont  = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeKeyBinding,  Gdiplus::FontStyleBold);
        fontAssets.checkBoxFont    = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeCheckBox,    Gdiplus::FontStyleRegular);
        fontAssets.descriptionFont = new Gdiplus::Font(L"Consolas", (float)layoutMetrics.fontSizeDescription, Gdiplus::FontStyleRegular);
    }

    static void RegisterZone(int x, int y, int w, int h, ControlType type, void* target) {
        if (type == ControlType::BUTTON || (y + h < layoutMetrics.footerTop && y > 0)) {
            RECT r = {x, y, x + w, y + h};
            clickZones.push_back({r, type, target});
        }
    }

    static void DrawLabel(Gdiplus::Graphics& graphics, const std::string& text, int yOffset, int xPos = -1) {
        float finalX = (xPos == -1) ? (float)layoutMetrics.paddingX : (float)xPos;
        Gdiplus::SolidBrush textBrush(COL_TEXT_DIM);
        std::wstring wText(text.begin(), text.end());
        Gdiplus::RectF layoutRect(finalX, (float)yOffset, layoutMetrics.labelWidth, (float)layoutMetrics.rowHeight);
        Gdiplus::StringFormat format;
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(wText.c_str(), -1, fontAssets.labelFont, layoutRect, &format, &textBrush);
    }

    static void DrawToggle(Gdiplus::Graphics& graphics, int yOffset, bool* target) {
        Gdiplus::SmoothingMode prevMode = graphics.GetSmoothingMode();
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        int w = layoutMetrics.toggleW;
        int h = layoutMetrics.toggleH;
        int x = layoutMetrics.windowW - layoutMetrics.paddingX - w;
        int y = yOffset + (layoutMetrics.rowHeight - h) / 2;

        Gdiplus::Color trackCol = *target ? COL_SELECTED : COL_SURFACE;
        Gdiplus::SolidBrush bgBrush(trackCol);
        graphics.FillPie(&bgBrush, x, y, h, h, 90, 180);
        graphics.FillPie(&bgBrush, x + w - h, y, h, h, 270, 180);
        graphics.FillRectangle(&bgBrush, x + h / 2, y, w - h, h);
        
        Gdiplus::SolidBrush knobBrush(Gdiplus::Color::White);
        int margin = (int)(2 * layoutMetrics.scale);
        int knobSize = h - (margin * 2);
        int knobX = *target ? (x + w - h + margin) : (x + margin);
        graphics.FillEllipse(&knobBrush, knobX, y + margin, knobSize, knobSize);

        graphics.SetSmoothingMode(prevMode);

        RegisterZone(x, y, w, h, ControlType::TOGGLE, target);
    }

    static void DrawKeyBox(Gdiplus::Graphics& graphics, int x, int y, unsigned int vkCode, bool isRecording) {
        std::wstring wKey;
        if (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU) wKey = L"ALT";
        else if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) wKey = L"CTRL";
        else if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) wKey = L"SHIFT";
        else if (vkCode == VK_TAB) wKey = L"TAB";
        else if (vkCode == VK_OEM_3) wKey = L"~";
        else if (vkCode == VK_SPACE) wKey = L"SPC";
        else if (vkCode >= 0x30 && vkCode <= 0x5A) wKey = (wchar_t)vkCode;
        else wKey = L"???";

        Gdiplus::SolidBrush bgBrush(COL_SURFACE);
        Gdiplus::Pen borderPen(isRecording ? COL_TEXT_BRIGHT : COL_HOVER, layoutMetrics.borderPenWidth);
        Gdiplus::Rect rect(x, y, layoutMetrics.keyBoxW, layoutMetrics.keyBoxH);
        Gdiplus::GraphicsPath path;
        float r = 6.0f * layoutMetrics.scale;
        path.AddArc((float)rect.X, (float)rect.Y, r, r, 180, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y, r, r, 270, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y + rect.Height - r, r, r, 0, 90);
        path.AddArc((float)rect.X, (float)rect.Y + rect.Height - r, r, r, 90, 90);
        path.CloseFigure();
        graphics.FillPath(&bgBrush, &path);
        graphics.DrawPath(&borderPen, &path);

        Gdiplus::SolidBrush textBrush(isRecording ? COL_TEXT_BRIGHT : COL_TEXT_DIM);
        Gdiplus::StringFormat format;
        format.SetAlignment(Gdiplus::StringAlignmentCenter);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF layoutRect((float)x, (float)y, (float)layoutMetrics.keyBoxW, (float)layoutMetrics.keyBoxH);
        graphics.DrawString(wKey.c_str(), -1, fontAssets.keyBindingFont, layoutRect, &format, &textBrush);
    }

    static std::string GetAppDisplayName(const std::string& appName) {
        auto it = Config::APP_NAMES.find(appName);
        if (it != Config::APP_NAMES.end()) {
            return it->second;
        }

        std::string displayName = appName;
        size_t lastDot = appName.find_last_of(".");
        if (lastDot != std::string::npos) {
            displayName = displayName.substr(0, lastDot);
        }
        for (size_t i = 1; i < displayName.length(); ++i) {
            if (isupper(displayName[i]) && !isspace(displayName[i-1])) {
                displayName.insert(i, " ");
                i++;
            }
        }

        return displayName;
}

    static void DrawCheckBoxText(Gdiplus::Graphics& graphics, const std::string& text, int yOffset, int xPos) {
        float finalX = (float)xPos;
        float textY  = (float)yOffset + (layoutMetrics.rowHeight - layoutMetrics.fontSizeLabel) / 2.0f;
        Gdiplus::SolidBrush textBrush(COL_TEXT_DIM);
        std::wstring wText(text.begin(), text.end());
        graphics.DrawString(wText.c_str(), -1, fontAssets.checkBoxFont, Gdiplus::PointF(finalX, textY), &textBrush);
    }

    static void AddAppCheckbox(Gdiplus::Graphics& graphics, const std::string& appName, int x, int y) {
        bool isChecked = (Config::tabbedApps.find(appName) != Config::tabbedApps.end());
    
        int boxSize = (int)(16 * layoutMetrics.scale);
        int boxY = y + (layoutMetrics.rowHeight - boxSize) / 2;
    
        Gdiplus::Rect rect(x, boxY, boxSize, boxSize);
        Gdiplus::Pen borderPen(isChecked ? COL_TEXT_BRIGHT : COL_HOVER, layoutMetrics.borderPenWidth);
    
        Gdiplus::GraphicsPath path;
        float r = 4.0f * layoutMetrics.scale;
        path.AddArc((float)rect.X, (float)rect.Y, r, r, 180, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y, r, r, 270, 90);
        path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y + rect.Height - r, r, r, 0, 90);
        path.AddArc((float)rect.X, (float)rect.Y + rect.Height - r, r, r, 90, 90);
        path.CloseFigure();
    
        graphics.DrawPath(&borderPen, &path);

        if (isChecked) {
            Gdiplus::SolidBrush accentBrush(COL_SELECTED);
            graphics.FillPath(&accentBrush, &path); 
        }

        int textX = x + boxSize + (int)(8 * layoutMetrics.scale);
        std::string displayName = GetAppDisplayName(appName);
        DrawCheckBoxText(graphics, displayName, y, textX);

        int totalWidth = (int)(160 * layoutMetrics.scale); 
        RegisterZone(x, y, totalWidth, layoutMetrics.rowHeight, ControlType::CHECKBOX, (void*)&appName);
    }

    static void AddToggleRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset, bool* target) {
        DrawLabel(graphics, label, yOffset);
        DrawToggle(graphics, yOffset, target);
        yOffset += layoutMetrics.rowHeight;
    }

    static void AddBindingRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset, unsigned int* modTarget, unsigned int* keyTarget = nullptr) {
        DrawLabel(graphics, label, yOffset);

        int margin = layoutMetrics.paddingX;
        int boxW = layoutMetrics.keyBoxW;
        int boxH = layoutMetrics.keyBoxH;
        int x = layoutMetrics.windowW - margin - boxW;
        int y = yOffset + (layoutMetrics.rowHeight - boxH) / 2;

        if (keyTarget) {
            DrawKeyBox(graphics, x, y, *keyTarget, (currentlyRecording == keyTarget));
            RegisterZone(x, y, boxW, boxH, ControlType::KEYBOX, keyTarget);

            x -= (boxW + (int)(20 * layoutMetrics.scale));
            DrawLabel(graphics, "+", yOffset, x + boxW + 5);
        }

        DrawKeyBox(graphics, x, y, *modTarget, (currentlyRecording == modTarget));
        RegisterZone(x, y, boxW, boxH, ControlType::KEYBOX, modTarget);

        yOffset += layoutMetrics.rowHeight;
    }

    static void AddCheckBoxRow(Gdiplus::Graphics& graphics, const std::string& label, int& yOffset) {
        DrawLabel(graphics, label, yOffset);
        yOffset += layoutMetrics.rowHeight;
    }

    static void AddDescription(Gdiplus::Graphics& graphics, const std::string& text, int& yOffset) {
        Gdiplus::SolidBrush subBrush(COL_TEXT_DIM);
        std::wstring wText(text.begin(), text.end());
        graphics.DrawString(wText.c_str(), -1, fontAssets.descriptionFont, Gdiplus::PointF((float)layoutMetrics.paddingX, (float)yOffset), &subBrush);
        yOffset += (int)(25 * layoutMetrics.scale);
    }

    static void DrawSeparator(Gdiplus::Graphics& g, int& yOffset) {
        yOffset += (int)(10 * layoutMetrics.scale);
        Gdiplus::Pen separatorPen(COL_SEPARATOR, layoutMetrics.separatorPenWidth);
        g.DrawLine(&separatorPen, layoutMetrics.paddingX, yOffset, layoutMetrics.windowW - layoutMetrics.paddingX, yOffset);
        yOffset += (int)(20 * layoutMetrics.scale);
    }

    static void DrawFooterButtons(Gdiplus::Graphics& graphics, int windowWidth) {
        int btnW = (int)(100 * layoutMetrics.scale);
        int btnH = (int)(32 * layoutMetrics.scale);
        int btnY = layoutMetrics.footerTop + (layoutMetrics.footerHeight - btnH) / 2;

        int margin = (int)(20 * layoutMetrics.scale);
        int exitX = windowWidth - btnW - margin;
        int saveX = exitX - btnW - (int)(10 * layoutMetrics.scale);
        int defX  = saveX - btnW - (int)(10 * layoutMetrics.scale);

        auto DrawBtn = [&](int x, int y, int w, int h, const std::string& label, void* target, int type) {
            Gdiplus::Rect rect(x, y, w, h);
            bool isHovered = (hoveredTarget == target);

            Gdiplus::Color bgCol, txtCol;

            if (type == 2) {
                bgCol = isHovered ? COL_TEXT_BRIGHT : COL_SELECTED;
                txtCol = COL_BG;
            } else if (type == 1) {
                bgCol = isHovered ? COL_HOVER : COL_SURFACE;
                txtCol = isHovered ? COL_TEXT_BRIGHT : COL_TEXT_DIM;
            } else {
                bgCol = isHovered ? COL_EXIT_RED : COL_BG;
                txtCol = isHovered ? COL_TEXT_BRIGHT : COL_TEXT_DIM;
            }

            Gdiplus::GraphicsPath path;
            float r = 6.0f * layoutMetrics.scale;
            path.AddArc((float)rect.X, (float)rect.Y, r, r, 180, 90);
            path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y, r, r, 270, 90);
            path.AddArc((float)rect.X + rect.Width - r, (float)rect.Y + rect.Height - r, r, r, 0, 90);
            path.AddArc((float)rect.X, (float)rect.Y + rect.Height - r, r, r, 90, 90);
            path.CloseFigure();

            Gdiplus::SolidBrush btnBrush(bgCol);
            graphics.FillPath(&btnBrush, &path);

            if (type == 3 && !isHovered) {
                Gdiplus::Pen borderPen(COL_HOVER, layoutMetrics.borderPenWidth);
                graphics.DrawPath(&borderPen, &path);
            }

            std::wstring wText(label.begin(), label.end());
            Gdiplus::StringFormat format;
            format.SetAlignment(Gdiplus::StringAlignmentCenter);
            format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::SolidBrush textBrush(txtCol);

            graphics.DrawString(wText.c_str(), -1, fontAssets.labelFont, Gdiplus::RectF((float)x, (float)y, (float)w, (float)h), &format, &textBrush);

            RegisterZone(x, y, w, h, ControlType::BUTTON, target);
        };

        DrawBtn(defX,  btnY, btnW, btnH, "Defaults", (void*)1, 1);
        DrawBtn(saveX, btnY, btnW, btnH, "Save",     (void*)2, 2);
        DrawBtn(exitX, btnY, btnW, btnH, "Exit",     (void*)3, 3);
    }

    static void DrawCustomScrollbar(Gdiplus::Graphics& graphics, int visibleHeight) {
        if (maxScroll <= 0) return;

        float visibleH = (float)visibleHeight;
        float totalH = visibleH + maxScroll;
        float ratio = visibleH / totalH;

        int barH = (int)(visibleH * ratio);
        int barY = (int)(currentScrollY * ratio);
        int barX = layoutMetrics.windowW - layoutMetrics.scrollBarW - layoutMetrics.scrollBarMargin;

        Gdiplus::SolidBrush barBrush(COL_SURFACE);

        graphics.FillEllipse(&barBrush,
            barX,
            barY + layoutMetrics.scrollBarUpperPadding,
            layoutMetrics.scrollBarW,
            layoutMetrics.scrollBarW
        );
        graphics.FillRectangle(&barBrush,
            barX,
            barY + layoutMetrics.scrollBarUpperPadding + (layoutMetrics.scrollBarW / 2),
            layoutMetrics.scrollBarW,
            barH - layoutMetrics.scrollBarLowerPadding - layoutMetrics.scrollBarW
        );
        graphics.FillEllipse(&barBrush,
            barX,
            barY + barH - layoutMetrics.scrollBarUpperPadding - layoutMetrics.scrollBarW,
            layoutMetrics.scrollBarW,
            layoutMetrics.scrollBarW
        );
    }

    static void Render(HDC hdc) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        
        int contentVisibleHeight = layoutMetrics.windowH - layoutMetrics.footerHeight;

        graphics.Clear(COL_BG);        
        clickZones.clear();

        Gdiplus::Region contentRegion(Gdiplus::Rect(0, 0, layoutMetrics.windowW, contentVisibleHeight));
        graphics.SetClip(&contentRegion);

        int yOffset = layoutMetrics.upperMargin - (int)currentScrollY;

        AddToggleRow(graphics, "Enable Task Switcher", yOffset, &Config::enableTaskSwitcher);
        AddBindingRow(graphics, "All Apps Shortcut", yOffset, &Config::allAppsSwitcherMod, &Config::allAppsSwitcherKey);
        AddBindingRow(graphics, "Same App Shortcut", yOffset, &Config::sameAppsSwitcherMod, &Config::sameAppsSwitcherKey);

        DrawSeparator(graphics, yOffset);

        AddToggleRow(graphics, "VS Code Launcher", yOffset, &Config::enableVSCodeLauncher);
        AddToggleRow(graphics, "WSL Terminal Launcher", yOffset, &Config::enableWSLTerminalLauncher);
        AddBindingRow(graphics, "VS Code Key", yOffset, &Config::VSCodeLauncherKey);
        AddBindingRow(graphics, "WSL Terminal Key", yOffset, &Config::WSLTerminalLauncherKey);
        AddDescription(graphics, "* Launchers use mandatory Ctrl + Alt modifiers.", yOffset);

        DrawSeparator(graphics, yOffset);

        AddToggleRow(graphics, "Enable Tab Switcher (Alt+Number)", yOffset, &Config::enableTabSwitcher);
        AddCheckBoxRow(graphics, "Tab switching active for:", yOffset);
        int startX = layoutMetrics.paddingX;
        int currentX = startX;
        int colWidth = (int)(160 * layoutMetrics.scale);
        for (size_t i = 0; i < Config::DEFAULT_TAB_APPS.size(); ++i) {
            if (i > 0 && i % 2 == 0) {
                yOffset += layoutMetrics.rowHeight;
                currentX = startX;
            }
            AddAppCheckbox(graphics, Config::DEFAULT_TAB_APPS[i], currentX, yOffset);
            currentX += colWidth;
        }
        yOffset += layoutMetrics.rowHeight;

        int absoluteBottom = yOffset + currentScrollY + (int)(20 * layoutMetrics.scale);
        maxScroll = absoluteBottom - contentVisibleHeight > 0 ? absoluteBottom - contentVisibleHeight : 0;

        graphics.ResetClip();

        DrawCustomScrollbar(graphics, contentVisibleHeight);

        Gdiplus::Pen separatorPen(COL_SEPARATOR, layoutMetrics.separatorPenWidth);
        graphics.DrawLine(&separatorPen, 0, layoutMetrics.footerTop, layoutMetrics.windowW, layoutMetrics.footerTop);

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
                KillTimer(hWnd, 1);
                currentScrollY = 0.0f;
                targetScrollY = 0.0f;
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
                                        // TODO: Config::SaveConfig();
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
            case WM_MOUSEWHEEL: {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                targetScrollY += (delta > 0) ? -SCROLL_STEP : SCROLL_STEP;

                if (targetScrollY < 0) targetScrollY = 0;
                if (targetScrollY > maxScroll) targetScrollY = (float)maxScroll;

                SetTimer(hWnd, 1, 16, NULL);
                return 0;
            }
            case WM_TIMER: {
                if (wParam == 1) {
                    float diff = targetScrollY - currentScrollY;
                    if (abs(diff) > 0.5f) {
                        currentScrollY += diff * LERP_FACTOR;
                        InvalidateRect(hWnd, NULL, FALSE);
                    } else {
                        currentScrollY = targetScrollY;
                        KillTimer(hWnd, 1);
                    }
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
        currentScrollY = 0.0f;
        targetScrollY = 0.0f;

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