# NLSI Telemetry user guide

## Installation

Install the Windows x64 setup package from the [GitHub Releases page](https://github.com/Christian-0777/nlsi_telemetry/releases). Close ETS2 and ATS before installing or updating. The installer creates Desktop and Start Menu shortcuts and attempts to install the plugin for each detected Steam game. See [Installation](INSTALLATION.md) for requirements and upgrade details.

The installer currently does not bundle Python. If no bundled runtime is present, the launcher uses the Windows Python Launcher (`py -3`) and requires Python 3.10 or newer.

## Launching

Start **NLSI Telemetry** from the Desktop or Start Menu before opening ETS2 or ATS. The console agent listens on `127.0.0.1:28745`. Once the game loads the plugin, the dashboard identifies the game and displays available telemetry.

If the game was already running when the plugin was installed or updated, close and relaunch the game so it loads the updated plugin.

## Dashboard

The dashboard presents the live game and connection state, truck values, world position, navigation values, truck/job configuration, and session metrics. The exact values depend on what the game supplies. Missing or unavailable values display as `--`; this does not necessarily mean the plugin is malfunctioning.

## Controls

- **D** toggles the latest raw telemetry packet as formatted JSON.
- **E** toggles the recent-events view.
- **Q** stops the agent cleanly. Ctrl+C is also supported.

The events view shows recent session, driving-state, derived job-start, and game-reported gameplay events. Event records are appended to the local `events.jsonl` file.

## ETS2 and ATS

The same agent and native plugin support ETS2 and ATS. Start the agent first, then launch the game and load a profile. The plugin reports which game it detected. Some dashboard fields remain unavailable when the game or its telemetry API does not provide them.

## Troubleshooting

### Dashboard says disconnected

1. Confirm the NLSI Telemetry console is running.
2. Confirm the game was launched after the plugin was installed.
3. Check that `nlsi_telemetry.dll` exists in the game's `bin\win_x64\plugins` directory.
4. If the game was running during setup, close and relaunch it.

The agent listens only on the local loopback address. It does not receive telemetry while the game/plugin is not sending packets.

### Automatic game detection skipped a game

The installer detects registered Steam installations, Steam library paths from `steamapps\libraryfolders.vdf`, and common Steam folders at drive roots. For a non-Steam or otherwise unrecognized installation, close the game and copy `C:\nlsi-tem\bin\nlsi_telemetry.dll` into that game's `bin\win_x64\plugins` folder.

### Launcher reports that Python is missing

Install Python 3.10 or newer with the Windows Python Launcher enabled, then run the shortcut again. The launcher prefers `runtime\python.exe` if a bundled runtime is present.

### Data and logs

The agent writes gameplay/session events to `app\test\output\events.jsonl` below the install directory (by default `C:\nlsi-tem\app\test\output\events.jsonl`). Installer plugin detection and copy results are recorded in `logs\game-plugin-install.log` and `logs\game-plugin-status.txt`.
