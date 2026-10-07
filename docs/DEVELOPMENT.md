# Development

## Windows build requirements

- Windows x64.
- Python 3.10 or newer. The agent uses only Python's standard library.
- Visual Studio Build Tools with the **Desktop development with C++** workload and MSVC x64 tools.
- The extracted official SCS Telemetry SDK 1.15. The normal local SDK path used by the release script is `C:\SCS\scs_sdk_1_15`.
- Inno Setup 6 to compile the installer.

The extracted SDK directory must contain `include\scssdk_telemetry.h`. The SDK is a build-time dependency and is not included in the release package.

## Tests

From the repository root, run:

```bat
py -3 -m unittest discover -s test -v
```

These tests exercise the agent model and dashboard without a live game or plugin.

## Build the native plugin

Use the existing native build script:

```bat
build.bat "C:\SCS\scs_sdk_1_15"
```

It locates MSVC with `vswhere`, calls the Visual Studio x64 developer environment, and builds `build\nlsi_telemetry.dll` from `src\nlsi_telemetry.cpp` and `src\nlsi_telemetry.def`.

## Build a complete release

The unified release script validates the SDK, builds and verifies the DLL, runs tests, invokes the existing installer builder, then checks the final installer and build metadata:

```bat
build-release.bat
build-release.bat "D:\path\to\scs_sdk_1_15"
build-release.bat clean
build-release.bat clean "C:\SCS\scs_sdk_1_15"
```

`clean` removes only the generated `build` directory. The existing `build-installer.bat`/`installer\build_installer.py` flow itself repeats its test and DLL build before staging and compiling with Inno Setup.

## Project layout

- `src\` — SCS native plugin implementation and exports.
- `agent.py` — local UDP receiver, event/session model, and console dashboard.
- `test\` — unit tests.
- `installer\` — Inno Setup definition, staging builder, launcher, and Steam game plugin helper.
- `docs\` — user and development documentation; `POC-README.md` retains the original detailed README.

Do not commit generated `build\` or `installer\staging\` contents, SDK files, `.env` secrets, or telemetry output.
