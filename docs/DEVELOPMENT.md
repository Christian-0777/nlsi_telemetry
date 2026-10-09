# Development

## Windows build requirements

- Windows x64.
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload and MSVC x64 tools.
- CMake 3.20 or newer.
- Qt 6.12 Widgets, Network, and Svg modules for MSVC 2022 x64. Set `CMAKE_PREFIX_PATH` to the Qt installation prefix when configuring.
- The native Qt application uses the verified TruckSim GPS revision-13 shared-memory map; the official x64/x86 game-plugin binaries and MIT notices are packaged by the installer builder. The TruckSim GPS Server GUI is not a build or runtime dependency.
- The extracted official SCS Telemetry SDK 1.15 is needed only for the separate legacy native plugin target (`build.bat`), not for the Qt desktop application/release installer.
- Inno Setup 6 to compile the installer.

The native desktop application uses Qt Widgets and does not require a Python runtime. The existing Python agent and Tkinter GUI remain in the repository as a reference implementation.

## Build the native desktop application

Configure and build with the Visual Studio 2022 x64 generator:

```bat
cmake -S . -B build\cmake-v1.4.5-beta -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build build\cmake-v1.4.5-beta --config Debug
cmake --build build\cmake-v1.4.5-beta --config Release
ctest --test-dir build\cmake-v1.4.5-beta -C Debug --output-on-failure
```

Use the version-specific build directory for this release; the repository-root `build` directory may contain a cache from another generator and v1.4.1 artifacts must not be reused or overwritten.

Debug binaries are written to `build\debug\v1.4.5-beta`; Release binaries are written to `build\releases\v1.4.5-beta`, with version-scoped intermediate and test outputs. CMake deploys the Qt runtime, SVG/network dependencies, and platform plugin beside the application executable when `windeployqt` is available in the selected Qt installation. Previous beta build directories and release outputs are not reused or overwritten.

## Tests

From the repository root, run:

```bat
py -3 -m unittest discover -s test -v
```

These tests exercise the agent model, GUI view-model, persisted history, and build staging without a live game or plugin.

## Build the native plugin

The separate legacy SCS ABI plugin remains available when explicitly needed:

```bat
build.bat "C:\SCS\scs_sdk_1_15"
```

It locates MSVC with `vswhere`, calls the Visual Studio x64 developer environment, and builds `build\v<VERSION>\nlsi_telemetry.dll` from `src\nlsi_telemetry.cpp` and `src\nlsi_telemetry.def`. The version comes from `version.json`; previous version folders are not overwritten.

## Build a complete native release

Use the existing Visual Studio CMake build and native installer script:

```bat
build-release.bat
```

The script builds the `NLSI-Exclusive-Logbook` Release application, x64/x86 SCS position plugins, and native tests in version-specific directories, runs Python compatibility tests and Release CTest, then calls `build-installer.bat` to stage Qt/MSVC, official TruckSim plugin files, notices, and release documentation before compiling `installer\NLSI-Exclusive-Logbook.iss`. All build and package output paths are version-specific. The expected installer output is `build\releases\v1.4.5-beta\NLSI-Exclusive-Logbook-v1.4.5-beta-Setup.exe`; prior releases remain unchanged.

For the native installer alone, after building the Release application, run:

```bat
build-installer.bat
```

## Project layout

- `src\` — SCS native plugin implementation and exports.
- `agent.py` — local UDP receiver, event/session model, and optional legacy console dashboard (`--console`).
- `gui_app.py` — Tkinter presentation layer, event/job history views, and worker-thread orchestration.
- `test\` — unit tests.
- `installer\` — Inno Setup definition, staging builder, launcher, and Steam game plugin helper.
- `docs\` — user and development documentation; `POC-README.md` retains the original detailed README.

Do not commit generated `build\` or `installer\staging\` contents, SDK files, `.env` secrets, or telemetry output.
