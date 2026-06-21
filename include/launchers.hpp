#pragma once

namespace Launcher {
    enum class Mode {
        VSCode,
        WSL
    };

    void Initialize();
    void Show(Mode mode);
    void ReleaseResources();
}