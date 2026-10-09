# Development

## Windows build requirements

- Windows x64.
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload and MSVC x64 tools.
- CMake 3.20 or newer.
- Qt 6.12 Widgets for MSVC 2022 x64. Set `CMAKE_PREFIX_PATH` to the Qt installation prefix when configuring.
- The native Qt application uses the verified TruckSim GPS revision-13 shared-memory map; the official x64/x86 game-plugin binaries and MIT notices are packaged by the installer builder. The TruckSim GPS Server GUI is not a build or runtime dependency.
- The extracted official SCS Telemetry SDK 1.15 is needed only for the separate legacy native plugin target (`build.bat`), not for the Qt desktop application/release installer.
- Inno Setup 6 to compile the installer.

The native desktop application uses Qt Widgets and does not require a Python runtime. The existing Python agent and Tkinter GUI remain in the repository as a reference implementation.

## Build the native desktop application

Configure and build with the Visual Studio 2022 x64 generator:

```bat
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build build\cmake --config Debug
cmake --build build\cmake --config Release
ctest --test-dir build\cmake -C Debug --output-on-failure
```

The VS Code CMake Tools workspace is configured to use `build\cmake`; the repository-root `build` directory may contain a cache from another generator and must not be reused for the native MSVC build.

Debug binaries are written to `build\debug`; Release binaries are written to `build\releases\v1.4.0-alpha`. CMake deploys the required Qt runtime and platform plugin beside each application executable when `windeployqt` is available in the selected Qt installation.

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

The script builds the `NLSI-Exclusive-Logbook` Release application and native tests in `build\cmake`, runs Python compatibility tests and Release CTest, then calls `build-installer.bat` to stage Qt/MSVC, official TruckSim plugin files, notices, and release documentation before compiling `installer\NLSI-Exclusive-Logbook.iss`. It does not delete or reconfigure the existing CMake build directory. The expected installer output is `build\releases\v1.4.0-alpha\NLSI-Exclusive-Logbook-v1.4.0-alpha-Setup.exe`.

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
