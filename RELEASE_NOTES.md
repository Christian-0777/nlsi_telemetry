# NLSI Telemetry v0.3.2

NLSI Telemetry v0.3.2 replaces the continuously updating CMD dashboard with a Tkinter desktop interface while retaining the existing SCS plugin and Python telemetry/session engine. The NLSI application version is 0.3.2; the telemetry API version remains supplied by the plugin/game and is not the application version.

## What's new

- Added seven GUI tabs: Main, Finished Jobs, PDF Export, Debug, Events, Active Mods, and About Us.
- Moved UDP telemetry processing to a background worker; GUI updates are delivered through a thread-safe queue and Tkinter's event loop.
- Added persisted job-history views based on job and gameplay events already recorded in JSONL.
- Changed PDF reporting to manual export: a report is written only after the user selects a delivered job, presses Export PDF, and chooses a file path.
- Uses `img\logo.ico` for the application and installer shortcuts.
- Social URLs on About Us can be configured in `.env`; secret configuration is not displayed in Debug.
- Preserved telemetry fields including internal position data. Position coordinates are not displayed on the Main tab.

## Limitations

- The current SCS packet model does not provide game-time samples, so game time and in-game elapsed/ETA display as unavailable.
- The official telemetry interface used here does not provide reliable active-mod information.
- Finished job history is derived from persisted event records; values not recorded by the game/plugin remain unavailable.
- Live ETS2/ATS operation still requires validation in each supported game build.

## Install or upgrade

Download `NLSI-Telemetry-Setup-v0.3.2.exe` from the [NLSI Telemetry GitHub Releases page](https://github.com/Christian-0777/nlsi_telemetry/releases). Close ETS2 and ATS before installing or updating. Install over v0.3.1 without uninstalling so existing configuration, data, and logs are retained.

The launcher requires Python 3.10 or newer and the Windows Python Launcher unless a bundled runtime is provided. The current installer does not bundle Python.

See the [user guide](docs/USER-GUIDE.md) for the tabs, PDF export, and troubleshooting.
