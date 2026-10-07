# Installation

## Requirements

- Windows x64.
- ETS2 and/or ATS for live game telemetry.
- For the installed launcher, Python 3.10 or newer and the Windows Python Launcher (`py`) unless a bundled `runtime\python.exe` is provided separately.
- Administrator approval for installation under the default `C:\nlsi-tem` path and game plugin folders.

The installer does not bundle a Python runtime in the current build. The native plugin is included in the installer; the SCS SDK and MSVC are build-time dependencies only.

## Fresh installation

1. Download `NLSI-Telemetry-Setup-v0.2.0.exe` from the [GitHub Release](https://github.com/Christian-0777/nlsi_telemetry/releases).
2. Close ETS2 and ATS before setup.
3. Run the installer and approve its administrator prompt.
4. Keep or select the application install directory (the default is `C:\nlsi-tem`).
5. Setup installs the agent, canonical DLL at `bin\nlsi_telemetry.dll`, launcher BAT, version information, and the installer helper.
6. Setup searches detected Steam libraries for ETS2 and ATS. For each installed game it creates the `bin\win_x64\plugins` directory if needed and copies the canonical DLL there.
7. Setup creates **NLSI Telemetry** shortcuts in the Start Menu and on the current user's Desktop.
8. On the final page, plugin detection results are shown. Optionally check **TikTok - @kape_073**. The default is unchecked; TikTok is opened only after Finish is clicked with the box checked.

If neither game is detected, setup still completes and records that result in the plugin status and installer log files. The application itself remains installed.

## Steam game detection

Detection checks Steam paths in the Windows registry and `steamapps\libraryfolders.vdf`, as well as common Steam/SteamLibrary folders at drive roots. For each game's detected library folder, the expected destination is:

```text
<Steam library>\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\nlsi_telemetry.dll
<Steam library>\steamapps\common\American Truck Simulator\bin\win_x64\plugins\nlsi_telemetry.dll
```

If a non-Steam installation is not detected, close the game, create its `bin\win_x64\plugins` directory if necessary, and copy the master DLL from `<install directory>\bin\nlsi_telemetry.dll` to that directory.

If a game is running or otherwise locks its plugin DLL, setup reports the plugin copy failure rather than treating it as successful. Close the affected game and rerun the installer.

## Upgrade from v0.1.0

Run the v0.2.0 installer over the existing install; a manual uninstall is not required. Both versions use the same Inno Setup application identity and default install path, so the update can append to the existing uninstall log and update the installed files and shortcuts.

The upgrade does not remove the entire application directory. Existing `config`, `data`, `logs`, and agent event output under `app\test\output` are retained. The example configuration is installed only if it does not already exist. Setup also removes the v0.1.0 startup shortcut and launcher BAT from the common Startup folder so the agent is not started automatically at Windows sign-in.

Close ETS2 and ATS before updating so their loaded plugin copies can be replaced.

## Launcher and shortcuts

The installed `NLSI-Telemetry.bat` resolves its application directory relative to itself. It uses `<install directory>\runtime\python.exe` if present; otherwise it launches the agent with `py -3`. No manual BAT or shortcut creation is required.

The Start Menu and Desktop shortcuts point to the installed BAT. The uninstaller removes those Inno Setup-managed shortcuts. It also attempts to remove only game plugin DLL copies recorded by NLSI and whose contents still match the installed canonical DLL; changed files are left in place. A locked plugin copy that cannot be removed is recorded in the persistent installer log.

## Developer/manual installation

For local development, install Python and the SCS SDK, build the DLL, then manually copy it to the selected game's `bin\win_x64\plugins` directory and run `py -3 agent.py`. Build commands and prerequisites are in [Development](DEVELOPMENT.md).
