# Release process

`version.json` is the authoritative version. The current source release is v0.2.0. The installer is a GitHub Release asset; local builds do not publish or upload anything.

## Build and verify

Requirements and test coverage are described in [Development](DEVELOPMENT.md). From the repository root, run:

```bat
build-release.bat
```

The script uses `C:\SCS\scs_sdk_1_15` by default. To select another extracted SDK, pass its path:

```bat
build-release.bat "D:\path\to\scs_sdk_1_15"
```

For a clean generated build:

```bat
build-release.bat clean
```

The process validates `include\scssdk_telemetry.h`, builds `build\nlsi_telemetry.dll` using `build.bat`, verifies the DLL, runs the Python tests, then calls the existing `build-installer.bat` and `installer\build_installer.py` package flow. The installer builder also repeats tests and the DLL build before staging files and compiling with Inno Setup 6.

For v0.2.0, verify that these outputs exist:

- `build\v0.2.0\NLSI-Telemetry-Setup-v0.2.0.exe`
- `build\v0.2.0\build-info.json`
- `build\nlsi_telemetry.dll`

Inspect the generated Inno script and setup package before distribution. Generated `build\` outputs and `installer\staging\` are ignored by Git and should not be committed.

## Publish

After the source release has been reviewed and published to the repository, create a GitHub Release for the matching version tag and attach `NLSI-Telemetry-Setup-v0.2.0.exe`. No upload or GitHub Release creation occurs in the local build command.
