#pragma once

namespace ConfigUI {
    struct LayoutMetrics {
        int windowW, windowH;
        int windowX, windowY;
        int paddingX;
        int columnX;
        int upperMargin, rowHeight, headerSpacing, sectionSpacing;
        int fontSizeHeader, fontSizeLabel, fontSizeKeyBinding, fontSizeDescription;
        
        int toggleW, toggleH;
        int keyBoxW, keyBoxH;
        
        float scale;
    };

    struct FontAssets {
        Gdiplus::Font* headerFont      = nullptr;
        Gdiplus::Font* labelFont       = nullptr;
        Gdiplus::Font* keyBindingFont  = nullptr;
        Gdiplus::Font* descriptionFont = nullptr;
        
        void Release() {
            delete labelFont;       labelFont       = nullptr;
            delete headerFont;      headerFont      = nullptr;
            delete keyBindingFont;  keyBindingFont  = nullptr;
            delete descriptionFont; descriptionFont = nullptr;
        }
    };

    enum ControlType {
        TOGGLE,
        KEYBOX,
        CHECKBOX,
    };

    struct ClickZone {
        RECT area;
        ControlType type;
        void* target;
    };

    void OpenConfigUI();
    void CloseConfigUI();
    bool IsConfigUIOpen();
}