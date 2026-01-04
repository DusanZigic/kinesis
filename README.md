# Kinesis
A high-performance, lightweight productivity suite for Windows, built natively in C++ with the Win32 API. Kinesis redefines your workflow by intercepting system-level hooks to provide ultra-fast app switching, workspace launching, and window management.

## Key Features
* VS Code Workspace Launcher: Instant access to your projects without navigating menus.
* Smart App Cycler: A custom, logic-driven alternative to Alt-Tab for cycling through specific development tools.
* Tab Switcher: Intercepts browser/IDE shortcuts to streamline navigation.
* Minimal Footprint: No Electron, no heavy frameworks—just pure Win32 performance.
* Custom Quit Dialog: A centered, dismissible prompt to prevent accidental application exits.

## Getting Started
### Prerequisites
* Windows 10 or 11
* A C++ compiler (like MinGW/GCC or MSVC)

### Installation & Build
Clone the repository: 
```ps
git clone https://github.com/DusanZigic/kinesis; cd kinesis
```

Build the project using the provided PowerShell script:
```ps
./build.ps1
```

Run the executable:
```ps
./ks.exe
```

### Default Hotkeys
Hotkey | Action |
---| ---|
Alt + ~ | Custom App Cycling (Logic-based) |
Alt + [1-9] | Instant Browser/IDE Tab Switching |
Ctrl + Alt + V | VS Code Workspace Launcher |
Ctrl + Alt + Q | Open Quit Confirmation Dialog |

## How it Works (Technical Overview)
Kinesis operates at the system level to provide a more fluid experience than standard OS shortcuts:
* Low-Level Keyboard Hooks: Uses WH_KEYBOARD_LL to intercept keystrokes before they reach the active window. This allows for the "Tab Switcher" logic, where Alt + [Number] is captured and re-routed to the browser to switch tabs instantly, bypassing default Windows behavior.
* Focus Management: Employs AttachThreadInput and SetForegroundWindow to ensure the VS Code Launcher and Quit Dialog always grab focus, even when launched from background processes.
* GDI Rendering: The UI is rendered using pure GDI (Graphics Device Interface) for zero-latency drawing and a tiny memory footprint.