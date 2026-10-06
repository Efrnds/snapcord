# Snapcord

A **native, lightweight, open-source** Discord client focused on **voice calls**.
Built with C++ and Qt 6, with no embedded browser, to use as little CPU and memory as possible.

> **Status:** early development (Phase 0). It does not connect to Discord yet.

## ⚠️ Disclaimer

Snapcord is **not affiliated with Discord**. Using third-party clients violates the
[Discord Terms of Service](https://discord.com/terms) and **may get your account banned**.
Use at your own risk.

## Features (planned)

- Voice calls with end-to-end encryption (DAVE), push-to-talk and per-user volume
- QR code login
- Layout faithful to the official Discord client
- Low memory and near-zero idle CPU usage
- English and Brazilian Portuguese interface

## Platforms

- Windows 10/11 (primary target for now)
- Linux and macOS (built by CI; official releases later)

## Building (Windows)

Requirements:

- Visual Studio 2022 Build Tools with C++
- Qt 6.8 (MSVC 64-bit) with the WebSockets module
- CMake 3.25+ and Ninja
- vcpkg

```powershell
# Adjust the paths if Qt and vcpkg are installed elsewhere
$env:QT_ROOT_DIR = "$HOME\Qt\6.8.3\msvc2022_64"
$env:VCPKG_ROOT  = "$HOME\vcpkg"

.\scripts\build.ps1 -Run
```

On Linux and macOS:

```sh
export QT_ROOT_DIR=/path/to/Qt/6.8.3/<platform>
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset release
cmake --build --preset release
```

## Translations

English is the source language. Translations live in [`translations/`](translations) as
Qt Linguist `.ts` files. To refresh them after changing interface text, run the
`update_translations` CMake target and edit the files with Qt Linguist.

## License

[GPLv3](LICENSE): anyone can use, study, modify and redistribute Snapcord, as long as
modified versions are also released under the GPLv3.
