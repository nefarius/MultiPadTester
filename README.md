# <img src="assets/RetroPad_128x128.png" align="left" />MultiPad Tester

[![Build status](https://img.shields.io/github/actions/workflow/status/nefarius/MultiPadTester/build.yml)](https://github.com/nefarius/MultiPadTester/actions)
[![Discord](https://img.shields.io/discord/346756263763378176.svg)](https://discord.nefarius.at/)
[![Mastodon Follow](https://img.shields.io/mastodon/follow/109321120351128938?domain=https%3A%2F%2Ffosstodon.org%2F)](https://fosstodon.org/@Nefarius)
[![Assisted by Cursor AI](https://img.shields.io/badge/Assisted%20by-Cursor%20AI-8B5CF6?style=flat)](https://cursor.com/)

Gamepad/controller tester and visualizer for Windows, supporting multiple input APIs.

## About

MultiPad Tester is a self-contained C++23 Windows desktop application for testing and visualizing gamepad input. It queries four different input backends in parallel and renders a real-time gamepad visualization for every connected controller using [Dear ImGui](https://github.com/ocornut/imgui) and DirectX 11. The tabbed interface lets you quickly switch between backends and see at a glance how many devices each one detects.

## Features

- **Multiple input backends** &mdash; XInput, Raw Input, DirectInput and HIDAPI (SetupDi / HID) are all supported out of the box.
- **Real-time visualization** &mdash; Every connected controller is drawn as an interactive gamepad widget showing buttons, sticks and triggers as they are actuated.
- **Tabbed UI** &mdash; Each backend gets its own tab with a live connected-device count, so you can compare how different APIs see the same hardware.
- **Self-contained** &mdash; Single Win32 executable, no runtime dependencies beyond the operating system.

## Configuration

Settings are stored in an INI file so they persist across runs. You can change them via the **Preferences** and **About** entries in the window title bar’s system menu (right-click the title bar).

- **Location** &mdash; `%APPDATA%\MultiPadTester\config.ini` (the folder is created on first run if needed).
- **Contents** &mdash; Refresh rate (60 / 75 / 120 / 144 Hz or monitor default), VSync on/off, and the last window position and size. If a saved position would place the window on a monitor that no longer exists, the app falls back to the default position on startup.

## How to build

### Prerequisites

- [Visual Studio 2022](https://visualstudio.microsoft.com/) (or newer) with the **Desktop development with C++** workload
- Windows 10/11 SDK
- Git (for submodule checkout)

### Build steps

Clone the repository with its vcpkg submodule:

```PowerShell
git clone --recursive https://github.com/nefarius/MultiPadTester.git
cd MultiPadTester
```

Bootstrap vcpkg, then configure and build:

```PowerShell
vcpkg/bootstrap-vcpkg.bat
cmake --preset default
cmake --build --preset release
```

The resulting binary is at `build/Release/MultiPadTester.exe`.

## Sources & 3rd party credits

This project benefits from these awesome projects (in no particular order):

- [Dear ImGui](https://github.com/ocornut/imgui) &mdash; immediate-mode GUI library used for all rendering
- [vcpkg](https://github.com/microsoft/vcpkg) &mdash; C++ package manager for dependency acquisition
