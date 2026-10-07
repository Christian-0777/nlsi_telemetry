# Release process

`version.json` is the authoritative NLSI application version. For v0.3.2 the SCS telemetry API value remains independent (the plugin currently reports `1.01`). The installer is a release asset; local builds do not publish or upload anything.

## Build and verify

Requirements and test coverage are described in [Development](DEVELOPMENT.md). From the repository root, run:

```bat
build-release.bat
```

The script uses `C:\SCS\scs_sdk_1_15` by default. To select another extracted SDK, pass its path:

```bat
build-release.bat "D:\path\to\scs_sdk_1_15"
```

For a clean build of only the current version:

```bat
build-release.bat clean
```

The release script reads `version.json`, creates `build\v<VERSION>\`, calls `build.bat` to compile `src\nlsi_telemetry.cpp` and `src\nlsi_telemetry.def` into that directory, runs the tests, and then invokes the installer builder. The installer stages the matching versioned DLL, `agent.py`, `gui_app.py`, `.env.example`, and `img\logo.ico`; it does not take a DLL from the shared build root.

For v0.3.2, verify these outputs:

- `build\v0.3.2\nlsi_telemetry.dll`
- `build\v0.3.2\NLSI-Telemetry-Setup-v0.3.2.exe`
- `build\v0.3.2\build-info.json`

The package contains the seven-tab Tkinter UI and icon. Generated `build\` outputs and `installer\staging\` are ignored by Git and should not be committed.

## Publish

After reviewing and publishing the source release, create a GitHub Release for the matching version tag and attach its installer. No upload or GitHub Release creation occurs in the local build command.
