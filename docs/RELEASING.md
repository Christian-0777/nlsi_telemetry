# Release process

`version.json` is the authoritative application version and release channel.
The native Qt application gets its runtime version from the CMake configure
metadata; `native/resources/app.rc` and `installer/NLSI-Exclusive-Logbook.iss`
carry matching Windows executable/installer metadata. Local builds do not
publish or upload anything.

## Build and verify

Use the existing Visual Studio 2022 x64 CMake configuration at `build\cmake`.
Do not configure over the repository-root `build\` directory: it may contain a
Ninja cache belonging to another toolchain.

```bat
build-release.bat
```

The release script builds only the native Qt Release target, runs Python
compatibility/packaging tests and Release CTest, then invokes
`build-installer.bat`. The installer builder verifies executable version
metadata, official TruckSim GPS x64/x86 plugin hashes, license notices, and the
automatic safe-install policy before staging Qt/MSVC dependencies and
compiling the Inno Setup definition.

Expected outputs for v1.3.8-alpha:

- `build\releases\v1.3.8-alpha\NLSI-Exclusive-Logbook.exe`
- `build\releases\v1.3.8-alpha\NLSI-Exclusive-Logbook-v1.3.8-alpha-Setup.exe`

The installer keeps application files under Program Files, user data under
`%LOCALAPPDATA%\NLSI\Exclusive Logbook`, and installs the official plugin only
to detected matching architecture folders for supported Steam ETS2/ATS
installations. The GPL server GUI/implementation is not packaged. Plugin and
SCS license/copyright notices are included. Live gameplay and installation
into actual game folders must be validated separately before making those
claims for a release.

## Publish

After reviewing and publishing the source release, create a GitHub Release for
the matching version tag and attach its installer. No commit, tag, upload, or
GitHub Release creation occurs in the local build command.
