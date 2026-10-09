# Release process

`version.json` is the authoritative application version and release channel.
The native Qt application gets its runtime version from the CMake configure
metadata; `native/resources/app.rc` and `installer/NLSI-Exclusive-Logbook.iss`
carry matching Windows executable/installer metadata. Local builds do not
publish or upload anything.

## Build and verify

Configure a version-specific Visual Studio 2022 x64 CMake directory such as
`build\cmake-v1.4.3-beta`. Do not reuse or reconfigure the existing
`build\cmake` cache or overwrite the completed v1.4.2-beta release outputs.

```bat
build-release.bat
```

The release script builds only the native Qt Release target, runs Python
compatibility/packaging tests and Release CTest, then invokes
`build-installer.bat`. The installer builder verifies executable version
metadata, official TruckSim GPS x64/x86 plugin hashes, license notices, and the
automatic safe-install policy before staging Qt/MSVC dependencies and
compiling the Inno Setup definition.

Expected outputs for v1.4.3-beta:

- `build\releases\v1.4.3-beta\NLSI-Exclusive-Logbook.exe`
- `build\releases\v1.4.3-beta\NLSI-Exclusive-Logbook-v1.4.3-beta-Setup.exe`
- `build\intermediate\v1.4.3-beta\installer-payload-v1.4.3-beta`

Previously built v1.4.2-beta artifacts remain unchanged. The v1.4.3 Beta build
retains revision-13 raw sample capture locally. No
authenticated backend or gameplay upload endpoint is implemented; release
notes and the installer must continue to describe synchronization as local
Pending only. The updater reads public release metadata but does not download or
install updates.

Fresh Alpha and Beta installers default to
`C:\Program Files (x86)\NLSI Exclusive Logbook`; Stable defaults to
`C:\Program Files\NLSI Exclusive Logbook`. Existing installations retain their
detected directory and a user-selected custom directory remains in effect for
upgrades because the installer keeps the same AppId and enables
`UsePreviousAppDir`. Installer operations require administrator privileges
for Program Files and game plugin installation. The installer keeps user data under
`%LOCALAPPDATA%\NLSI\Exclusive Logbook`, and installs the official plugin only
to detected matching architecture folders for supported Steam ETS2/ATS
installations. The GPL server GUI/implementation is not packaged. Plugin and
SCS license/copyright notices are included. Live gameplay and installation
into actual game folders must be validated separately before making those
claims for a release.

## Publish

After reviewing and publishing the source release, create a GitHub Release for
the matching version tag and attach its installer. The published-release
workflow announces releases in Discord after `DISCORD_RELEASE_WEBHOOK` has
been configured; see `docs/DISCORD-RELEASES.md`. No commit, tag, upload, or
GitHub Release creation occurs in the local build command.
