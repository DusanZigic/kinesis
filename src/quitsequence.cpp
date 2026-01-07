#include "common.hpp"
#include "quitsequence.hpp"

static bool isRegistered = false;
static HWND hQuitDlg = NULL;
static HFONT hFont = NULL;
static HBRUSH hBgBrush = NULL;

static int winW = 0;
static int winH = 0;
static int winX = 0;
static int winY = 0;
static int fontSize = 0;

static LRESULT CALLBACK QuitDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ACTIVATEAPP: {
            if (wParam == FALSE) { 
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                PostQuitMessage(0);
                return 0;
            } else if (wParam == VK_ESCAPE) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            HGDIOBJ hOldFont = SelectObject(hdc, hFont);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);

            RECT rect;
            GetClientRect(hwnd, &rect);

            rect.top = winH*0.20;
            DrawTextA(hdc, "quit kinesis?", -1, &rect, DT_CENTER | DT_SINGLELINE);

            rect.top = winH - winH*0.20 - fontSize;
            SetTextColor(hdc, RGB(180, 180, 180));
            DrawTextA(hdc, "[Enter] Confirm    [Esc] Cancel", -1, &rect, DT_CENTER | DT_SINGLELINE);

            SelectObject(hdc, hOldFont);
            EndPaint(hwnd, &ps);
            break;
        }
        case WM_CLOSE: {
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            if (hFont) {
                DeleteObject(hFont);
                hFont = NULL;
            }
            SetWindowRgn(hwnd, NULL, FALSE);
            hQuitDlg = NULL;
            return 0;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void InitiateQuitSequence() {
    if (hQuitDlg && IsWindow(hQuitDlg)) {
        SetForegroundWindow(hQuitDlg);
        return;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    winW = screenW * 0.250;
    winH = screenH * 0.175;
    winX = (screenW - winW) / 2;
    winY = (screenH - winH) / 3;

    fontSize = winH * 0.12;

    if (!isRegistered) {
        WNDCLASSA wndClass {};
        wndClass.lpfnWndProc = QuitDialogProc;
        wndClass.hInstance = GetModuleHandle(NULL);
        wndClass.lpszClassName = "QuitDialogClass";
        wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
        hBgBrush = CreateSolidBrush(RGB(30, 30, 30));
        wndClass.hbrBackground = hBgBrush;
        RegisterClassA(&wndClass);
        isRegistered = true;
    }

    if (!hFont) {
        hFont = CreateFontA(
            -fontSize,
            0, 0, 0,
            FW_SEMIBOLD,
            FALSE, FALSE, FALSE, 
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, 
            CLEARTYPE_QUALITY,
            VARIABLE_PITCH | FF_SWISS,
            "Consolas"
        );
    }

    hQuitDlg = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "QuitDialogClass",
        NULL,
        WS_POPUP | WS_BORDER,
        winX, winY, winW, winH,
        NULL, NULL,
        GetModuleHandle(NULL),
        NULL
    );

    int cornerRadius = winH * 0.06;
    HRGN hMainRgn = CreateRoundRectRgn(0, 0, winW, winH, cornerRadius, cornerRadius);
    SetWindowRgn(hQuitDlg, hMainRgn, TRUE);

    AllowSetForegroundWindow(ASFW_ANY);
    keybd_event(0xFC, 0, 0, 0);
    keybd_event(0xFC, 0, KEYEVENTF_KEYUP, 0);
    ShowWindow(hQuitDlg, SW_SHOW);
    SetForegroundWindow(hQuitDlg);
    SetActiveWindow(hQuitDlg);
    SetFocus(hQuitDlg);
}