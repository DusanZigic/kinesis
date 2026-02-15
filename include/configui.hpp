#pragma once

namespace ConfigUI {
    enum ControlType {
        TOGGLE,
        KEYBOX
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