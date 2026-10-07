# NLSI Telemetry v0.2.0

**Windows x64 update release** for Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS).

## Highlights

- Native telemetry plugin built with the official SCS Telemetry SDK 1.15.
- Local Python console agent receives telemetry over loopback UDP.
- Live console dashboard for truck, driving, position, navigation, job, session, and connection status.
- Press **D** to inspect the latest raw telemetry packet as formatted JSON.
- Press **E** to review recent events.
- Press **Q** to stop the agent cleanly and record the session end.
- Records gameplay events and session metrics to local JSONL output.
- Installer upgrades existing v0.1.0 installs without a manual uninstall.
- Installer automatically copies `nlsi_telemetry.dll` into ETS2 and ATS plugin folders when the games are installed.
- Installs the launcher BAT and creates Start Menu and Desktop shortcuts automatically.

## Installation

Download `NLSI-Telemetry-Setup-v0.2.0.exe` from this GitHub Release and run the installer. Close ETS2 or ATS before installing or updating. The installer preserves existing user data, config, and logs while upgrading from v0.1.0.

## Requirements and scope

- Windows x64.
- ETS2 or ATS with the NLSI telemetry plugin installed in the game's `bin\win_x64\plugins` folder.
- Telemetry is processed locally; this release does not upload data to a website or cloud service.
- This proof of concept has not been validated with live sessions in every supported game version. See the project README for build instructions, setup details, and known limitations.
