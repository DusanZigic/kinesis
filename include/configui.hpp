#pragma once

namespace ConfigUI {
    struct LayoutMetrics {
        int windowW, windowH;
        int windowX, windowY;
        int headerHeight;
        int paddingX;
        int upperMargin, rowHeight, sectionSpacing;
        int fontSizeLabel, fontSizeKeyBinding, fontSizeCheckBox, fontSizeDescription;
        int toggleW, toggleH;
        int keyBoxW, keyBoxH;
        int checkBoxColumnN, checkBoxColumnWidth, checkBoxSize;
        float labelWidth;
        int footerHeight, footerTop;
        int contentVisibleHeight;
        int borderPenWidth, separatorPenWidth;
        int scrollBarW, scrollBarMargin, scrollBarUpperPadding, scrollBarLowerPadding;
        float scale;
    };

    struct FontAssets {
        Gdiplus::Font* labelFont       = nullptr;
        Gdiplus::Font* keyBindingFont  = nullptr;
        Gdiplus::Font* checkBoxFont    = nullptr;
        Gdiplus::Font* descriptionFont = nullptr;
        
        void Release() {
            delete labelFont;       labelFont       = nullptr;
            delete keyBindingFont;  keyBindingFont  = nullptr;
            delete checkBoxFont;    checkBoxFont    = nullptr;
            delete descriptionFont; descriptionFont = nullptr;
        }
    };

    enum ControlType {
        TOGGLE,
        KEYBOX,
        CHECKBOX,
        BUTTON,
        SCROLLBAR,
    };

    struct ClickZone {
        RECT area;
        ControlType type;
        void* target;
    };

    void OpenConfigUI();
    bool IsConfigUIOpen();
}