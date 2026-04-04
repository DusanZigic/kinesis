#pragma once

namespace Launcher {
    enum class Mode {
        VSCode,
        WSL
    };

    struct Context {
        Mode type;
        std::string windowTitle;
        int logoResourceID;
        std::string historyFileName;
        bool isEngineFound = false;
        std::string executablePath;
        std::string cliPath;
        Gdiplus::Image* logoImage = nullptr;
        float logoImageAspectRatio;
        std::string placeholder;
        std::vector<std::string> history;
    };

    struct LayoutMetrics {
        int mainWinW, mainWinH, mainWinX, mainWinY;
        int margin, currentY, innerWidth, editH;
        int pathH, listH;
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

            mainFont  = new Gdiplus::Font(L"Consolas", mainFontSize,  Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            smallFont = new Gdiplus::Font(L"Consolas", smallFontSize, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        }

        static void Release() {
            if (hBgBrush)  {DeleteObject(hBgBrush); hBgBrush  = nullptr;}
            if (logoAttr)  {delete logoAttr;        logoAttr  = nullptr;}
            if (mainFont)  {delete mainFont;        mainFont  = nullptr;}
            if (smallFont) {delete smallFont;       smallFont = nullptr;}
        }

    };

    struct FontAssets {
        Gdiplus::Font* mainFont  = nullptr;
        Gdiplus::Font* smallFont = nullptr;
        
        void Release() {
            delete mainFont;   mainFont  = nullptr;
            delete smallFont;  smallFont = nullptr;
        }
    };

    void Initialize();
    void Show(Mode mode);
    void ReleaseResources();
}