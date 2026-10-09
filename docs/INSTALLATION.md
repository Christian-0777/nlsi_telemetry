# Installation

## Native v1.4.2-beta

- Windows x64; no Python runtime is required for the native desktop application.
- A fresh Alpha or Beta install defaults to `C:\Program Files (x86)\NLSI Exclusive Logbook`; the Stable installer defaults to `C:\Program Files\NLSI Exclusive Logbook`. An existing installation or user-selected path is retained during upgrades.
- Setup displays whether it detected an existing installation and, when available, its installed version before updating to v1.4.2-beta. The publisher is Nabski Logistics and Solutions Inc.
- Updates replace application files in place and retain existing configuration and logs. Existing Start Menu/Desktop shortcut options and Terms and Conditions/Privacy Policy acceptance remain available.
- Setup automatically installs the hash-verified official TruckSim GPS x64/x86 plugin into matching architecture folders for detected Steam ETS2/ATS installations. The separate TruckSim GPS Telemetry Server GUI is not bundled, opened, or required. Different or modified existing plugin DLLs are preserved.
- Installer-managed plugin DLLs are tracked by path and SHA-256. When replacing an unchanged installer-managed plugin, setup keeps a uniquely named `.bak` recovery copy beside the game plugin. Uninstall removes only unchanged managed copies; user-modified plugins and backup files are preserved.
- TruckSim GPS and SCS SDK MIT license/copyright notices are installed under the application's `licenses` folder; the GPL server implementation is not distributed.
- The app reads the plugin's `Local\TSGPSTelemetry` 32 KiB, revision-13 map directly. Missing mappings, unsupported revisions, inactive SDK status, and stale samples are reported in Settings → Providers. Live ETS2/ATS behavior has not been tested for this release.
- The native application stores TXT and `.nlsi` logs, daily raw telemetry files, events, sessions, completed/cancelled job records, and a local pending-sync queue under `%LOCALAPPDATA%\NLSI\Exclusive Logbook`. Existing install-directory `logs` and `session_logs` are copied there only when the destination file does not already exist; original files are not deleted. These per-user files are retained during updates and uninstalls.
- The Beta has no authenticated account, backend, or upload endpoint. Synchronization remains local and Pending; the application does not send or mark records synchronized. The GitHub release checker only reads public release metadata.
- The current TruckSim GPS revision-13 source does not provide verified toll fees or ferry/train crossings; those history values remain unavailable unless a future provider persists explicit records.
- Setup removes the legacy `C:\nlsi-tem` application directory only when its known `config`, `data`, `logs`, and `app\test\output` user-data folders are absent; otherwise it leaves the legacy directory intact. Setup does not recreate that directory.
- Uninstall removes the installed application and its managed shortcuts. User data stored outside the application installation directory is not targeted.
- The installer includes the Qt and Microsoft Visual C++ runtime dependencies.

## Legacy Python telemetry installer (earlier releases)

The instructions below apply only to the earlier Python/game-plugin installer, not the native v1.4.2-beta desktop installer.

The release history proves that the native v1.3.8 Alpha and v1.3.9 Beta
installer definitions used `{autopf}` in 64-bit install mode, while the
separate legacy Python installer explicitly used `C:\Program Files`. It does
not establish an older Alpha/Beta `Program Files (x86)` behavior or identify
which configuration was intended as the transition point. Current native
Alpha/Beta and Stable defaults are selected explicitly by release channel as
documented above; `UsePreviousAppDir` and the shared AppId preserve an existing
installation path during upgrades.

## Requirements

- Windows x64.
- ETS2 and/or ATS for live game telemetry.
- For the installed launcher, Python 3.10 or newer and the Windows Python Launcher (`pyw`) unless a bundled runtime is provided.
- Administrator approval for installation under Program Files and game plugin folders.
- The installer uses 64-bit install mode for the native x64 app and requests administrator privileges so it can write to Program Files and supported game plugin folders.

The legacy Python telemetry installer and its game-plugin procedures below describe the earlier installer; the native v1.4.2-beta installer does not bundle that legacy agent.

## Fresh installation

1. Download `NLSI-Telemetry-Setup-v1.0.0.exe` from the [GitHub Release](https://github.com/Christian-0777/nlsi_telemetry/releases).
2. Close ETS2 and ATS before setup.
3. Run the installer and approve its administrator prompt.
4. Keep or select the application install directory (the default is `C:\nlsi-tem`).
5. Setup installs the agent and Tkinter GUI, canonical DLL at `bin\nlsi_telemetry.dll`, application logo, launcher BAT, version information, and the installer helper.
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

## Upgrade from an existing install

Run the v1.0.0 installer over the existing install; a manual uninstall is not required. It uses the existing Inno Setup application identity and default install path, so the update can append to the existing uninstall log and update the installed files and shortcuts.

The upgrade does not remove the entire application directory. Existing `config`, `data`, `logs`, and agent event output under `app\test\output` are retained. The example configuration is installed only if it does not already exist. Setup also removes the v0.1.0 startup shortcut and launcher BAT from the common Startup folder so the agent is not started automatically at Windows sign-in.

Close ETS2 and ATS before updating so their loaded plugin copies can be replaced.

## Launcher and shortcuts

The installed `NLSI-Telemetry.bat` resolves its application directory relative to itself. It uses `<install directory>\runtime\pythonw.exe` if present; otherwise it launches with `pyw -3` to open the GUI without a persistent CMD dashboard. If the window does not start, `py -3 agent.py` from the `app` directory provides console diagnostics. No manual BAT or shortcut creation is required.

The Start Menu and Desktop shortcuts point to the installed BAT. The uninstaller removes those Inno Setup-managed shortcuts. It also attempts to remove only game plugin DLL copies recorded by NLSI and whose contents still match the installed canonical DLL; changed files are left in place. A locked plugin copy that cannot be removed is recorded in the persistent installer log.

## Developer/manual installation

For local development, install Python and the SCS SDK, build the DLL, then manually copy it to the selected game's `bin\win_x64\plugins` directory and run `py -3 agent.py`. Build commands and prerequisites are in [Development](DEVELOPMENT.md).
