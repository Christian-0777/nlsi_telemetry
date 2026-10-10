# Release process

`version.json` is the authoritative application version and release channel.
The native Qt application gets its runtime version from the CMake configure
metadata; `native/resources/app.rc` and `installer/NLSI-Exclusive-Logbook.iss`
carry matching Windows executable/installer metadata. Local builds do not
publish or upload anything.

## Build and verify

Configure a version-specific Visual Studio 2022 x64 CMake directory such as
`build\cmake-v1.5.1-beta`. Do not reuse or reconfigure the existing
`build\cmake` cache or overwrite completed earlier release outputs.

```bat
build-release.bat
```

The release script builds only the native Qt Release target, runs Python
compatibility/packaging tests and Release CTest, then invokes
`build-installer.bat`. The installer builder verifies executable version
metadata, official TruckSim GPS x64/x86 plugin hashes, license notices, and the
automatic safe-install policy before staging Qt/MSVC dependencies and
compiling the Inno Setup definition.

Expected outputs for v1.5.1-beta:

- `build\releases\v1.5.1-beta\NLSI-Exclusive-Logbook.exe`
- `build\releases\v1.5.1-beta\NLSI-Exclusive-Logbook-v1.5.1-beta-Setup.exe`
- `build\intermediate\installer-payload-v1.5.1-beta`

Previously built release artifacts remain unchanged. The v1.5.1 Beta build
aligns the application metadata and release output with the current beta
version while preserving the completed-job and installer safeguards from the
existing release branch. No
authenticated backend or gameplay upload endpoint is implemented; release
notes and the installer must continue to describe synchronization as local
Pending only. The updater reads public release metadata but does not download or
install updates.

Fresh Alpha, Beta, and Stable installers default to
`C:\Program Files\NLSI Exclusive Logbook`. A validated existing installation
retains its registered directory and cannot silently be redirected to a second
copy. Installer operations require administrator privileges
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
