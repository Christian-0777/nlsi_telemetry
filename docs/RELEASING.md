# Building and publishing a release

`version.json` is the authoritative application version. Release installers are distributed as assets on the [GitHub Releases](https://github.com/Christian-0777/nlsi_telemetry/releases) page; generated installers and build output are not committed to the source repository.

## Build the release

1. Update the `version` value in `version.json` using `X.Y.Z` semantic versioning. The installer builder reads this value and uses it for the output folder and installer filename.
2. Run the Python tests:

   ```bat
   py -3 -m unittest discover -s test -v
   ```

3. Build the native telemetry DLL using the official SCS Telemetry SDK 1.15 and the installed Visual Studio C++ x64 tools:

   ```bat
   build.bat "C:\SCS\scs_sdk_1_15"
   ```

   The DLL is generated at `build\nlsi_telemetry.dll`. The SDK is only needed to build; do not copy its files into the release package or commit them.

4. Build the installer:

   ```bat
   build-installer.bat "C:\SCS\scs_sdk_1_15"
   ```

   This command runs the tests and native build as part of the full packaging flow, prepares installer staging, and compiles the setup with Inno Setup 6.

5. Verify the versioned output folder `build\vVERSION\`. It should contain:

   - `NLSI-Telemetry-Setup-vVERSION.exe`
   - `build-info.json`
   - the generated `NLSI-Telemetry-vVERSION.iss`

   Replace `VERSION` with the value from `version.json` (for example, `0.1.0`). Check the installer exists and has a nonzero size. Do not add the generated `build\` or `installer\staging\` files to the source commit.

## Create the GitHub release

After reviewing the source changes and confirming the installer:

1. Create a Git commit for the release source:

   ```bat
   git add -A
   git commit -m "Prepare NLSI Telemetry v0.1.0 release"
   ```

   Substitute the current version in the commit message. Generated output, local telemetry data, secrets, and local build dependencies are excluded by `.gitignore`.

2. Create an annotated version tag:

   ```bat
   git tag -a v0.1.0 -m "NLSI Telemetry v0.1.0"
   ```

   Substitute the current version for `0.1.0`. Push the release commit and tag to the GitHub repository before creating the release.

3. On GitHub, create a Release from the matching `vVERSION` tag and attach the installer from `build\vVERSION\`. Use this asset filename:

   ```text
   NLSI-Telemetry-Setup-vVERSION.exe
   ```

No GitHub Release is created automatically by the local build process.
