# Development

## Windows build requirements

- Windows x64.
- Visual Studio 2022 Build Tools with the **Desktop development with C++** workload and MSVC x64 tools.
- CMake 3.20 or newer.
- Qt 6.12 Widgets for MSVC 2022 x64. Set `CMAKE_PREFIX_PATH` to the Qt installation prefix when configuring.
- The extracted official SCS Telemetry SDK 1.15. The normal local SDK path used by the release script is `C:\SCS\scs_sdk_1_15`.
- Inno Setup 6 to compile the installer.

The extracted SDK directory must contain `include\scssdk_telemetry.h`. The SDK is a build-time dependency and is not included in the release package.

The native desktop application uses Qt Widgets and does not require a Python runtime. The existing Python agent and Tkinter GUI remain in the repository as a reference implementation.

## Build the native desktop application

Configure and build with the Visual Studio 2022 x64 generator:

```bat
cmake -S . -B build\cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build build\cmake --config Debug
cmake --build build\cmake --config Release
ctest --test-dir build\cmake -C Debug --output-on-failure
```

Debug binaries are written to `build\debug`; Release binaries are written to `build\releases\v1.3.2-alpha`. CMake deploys the required Qt runtime and platform plugin beside each application executable when `windeployqt` is available in the selected Qt installation.

## Tests

From the repository root, run:

```bat
py -3 -m unittest discover -s test -v
```

These tests exercise the agent model, GUI view-model, persisted history, and build staging without a live game or plugin.

## Build the native plugin

Use the existing native build script:

```bat
build.bat "C:\SCS\scs_sdk_1_15"
```

It locates MSVC with `vswhere`, calls the Visual Studio x64 developer environment, and builds `build\v<VERSION>\nlsi_telemetry.dll` from `src\nlsi_telemetry.cpp` and `src\nlsi_telemetry.def`. The version comes from `version.json`; previous version folders are not overwritten.

## Build a complete release

The unified release script validates the SDK, builds and verifies the DLL, runs tests, invokes the existing installer builder, then checks the final installer and build metadata:

```bat
build-release.bat
build-release.bat "D:\path\to\scs_sdk_1_15"
build-release.bat clean
build-release.bat clean "C:\SCS\scs_sdk_1_15"
```

`clean` removes only the current version's generated output directory. The existing `build-installer.bat`/`installer\build_installer.py` flow itself repeats its test and DLL build before staging and compiling with Inno Setup.

## Project layout

- `src\` — SCS native plugin implementation and exports.
- `agent.py` — local UDP receiver, event/session model, and optional legacy console dashboard (`--console`).
- `gui_app.py` — Tkinter presentation layer, event/job history views, and worker-thread orchestration.
- `test\` — unit tests.
- `installer\` — Inno Setup definition, staging builder, launcher, and Steam game plugin helper.
- `docs\` — user and development documentation; `POC-README.md` retains the original detailed README.

Do not commit generated `build\` or `installer\staging\` contents, SDK files, `.env` secrets, or telemetry output.
