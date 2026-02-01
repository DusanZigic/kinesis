# kinesis
Lightweight window management suite for Windows.

## Key Features

### Task Switching

* **global switcher** (`alt + tab`): navigate between all open application windows
* **contex switcher** (`alt + ~`): cycle through windows belonging to the current active app
* **tab switcher** (`alt + [1-9]`): switch tabs in browsers, IDEs, and file explorer


### Launchers

Overlay launchers for quick directory access:

* **VS Code Launcher**: open workspaces or folders
* **WSL Terminal Launcher**: jump directly into Linux environments
* **Features**: path history and real-time folder discovery

### System Integration
* **tray management**: access all settings via system tray icon
* **automatic startup**: option to toggle "Run on Startup" directly from the menu
* **elevation**: built-in "Run as Administrator" toggle for managing high-privilege windows
* **configuration**: open and edit the JSONC configuration file and option to reset to factory defaults
* **safety**: custom quit dialog sequence to prevent accidental closures

## Technical Highlights
* **native performance**: written in C++ using Win32 API and GDI
* **layered UI**: uses hardware-accelerated alpha blending for smooth transitions
* **low-level hooks**: optimized keyboard hooks to ensure reliable hotkey interception
* **single instance**: mutex-guarded execution to prevent resource conflicts

## Default Hotkeys
Hotkey | Action |
---| ---|
`alt + tab` | switch apps
`alt + ~` | switch same-app windows |
`alt + [1-9]` | browser/IDE/file explorer tab switching |
`ctrl + qlt + v` | VS Code workspace launcher |
`ctrl + qlt + l` | WSL terminal launcher |
`ctrl + alt + q` | open quit dialog |

## Build & Installation

### Releases
If you don't wish to build from source, you can download the latest pre-compiled `kinesis.exe` from the Releases tab. Just download it, place it anywhere, and run.

### Build Prerequisites

* **compiler**: g++ (MinGW-w64 recommended) with C++17 support
* **environment**: Windows 10/11
* **build tool**: PowerShell (to run the provided build script)

### Cloning & Building

1. clone the repository:
```powershell
git clone https://github.com/DusanZigic/kinesis.git
cd kinesis
```
2. debug build (with console output and debug symbols):
```powershell
./build.ps1
```
3. release build (optimized, standalone, no console window):
```powershell
./build.ps1 -release
```

### Portable & Standalone

kinesis is designed to be a single-file utility:

* **resource integration**: all assets (backgrounds, app icons) are compiled directly into the binary via `windres`

* **zero dependencies**: release version is linked statically (`-static`); only `kinesis.exe` is needed that can be found in the `bin/` folder to run the app on any machine

## Security & Transparency
kinesis uses a low-level keyboard kook (`WH_KEYBOARD_LL`) to intercept `alt + tab`, `alt + ~` and `alt + [1-9]`. This is the only way to override Windows' default switcher behavior and provide a custom, high-performance replacement.

kinesis does not log or transmit your keystrokes. The hook is used strictly for hotkey detection and logic.

### Digital Signatures & SmartScreen
Because kinesis is an open-source project and the binaries are not signed with a paid Certificate Authority (EV Certificate), you will likely encounter the Windows SmartScreen "Windows protected your PC" prompt when running the executable for the first time.

To run the app:
1. click "More info"
2. click "Run anyway"

### Antivirus False Positives
Some security software may flag the app as a "keylogger" due to the nature of the keyboard hook. This is a common false positive. You may need to add an exclusion for `kinesis.exe` in your antivirus settings.