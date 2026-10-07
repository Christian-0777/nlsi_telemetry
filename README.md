# NLSI Telemetry

NLSI Telemetry is a Windows x64 proof of concept that receives local telemetry from Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS) through an SCS telemetry plugin and displays it in a console dashboard.

## What it does

- Shows live truck, driving, position, navigation, job, session, and connection information.
- Records gameplay/session events to a local JSONL file.
- Keeps telemetry on the local computer; it does not upload data to a website or cloud service.

## Supported games

- Euro Truck Simulator 2 (ETS2)
- American Truck Simulator (ATS)

The installer detects Steam libraries and copies the plugin into the installed game's plugin directory. The project has not been validated in live sessions across all game versions.

## Current release

The current source version is **v0.3.0**, an update to v0.2.0. Release installers are published as GitHub Release assets:

**[Download NLSI Telemetry releases](https://github.com/Christian-0777/nlsi_telemetry/releases)**

## Installation

1. Download `NLSI-Telemetry-Setup-v0.3.0.exe` from the v0.3.0 GitHub Release.
2. Close ETS2 and ATS before installing or updating.
3. Run the installer. It installs the launcher and creates Desktop and Start Menu shortcuts. Existing configuration, data, and logs are retained during an upgrade.
4. On the final installer page, optionally select TikTok if you want the installer to open `https://www.tiktok.com/@kape_073` when you finish.

The installer does not bundle Python. If no bundled runtime is present, launching requires Python 3.10 or newer and the Windows Python Launcher (`py`).

## First run and using the dashboard

Start **NLSI Telemetry** from the Desktop or Start Menu before launching either game. The game plugin sends telemetry to the local agent over `127.0.0.1`; the dashboard updates as packets arrive.

- Press **D** to show or hide the latest raw telemetry packet.
- Press **E** to show recent events.
- Press **Q** to stop the agent cleanly.

Game telemetry is available only while the game loads the plugin. See the [user guide](docs/USER-GUIDE.md) and [installation guide](docs/INSTALLATION.md) for setup and troubleshooting.

## Telemetry data

The agent writes session and gameplay events to `C:\nlsi-tem\app\test\output\events.jsonl` in the default install. Telemetry and logs remain local. For the packet model and known limitations, see [technical documentation](docs/TECHNICAL.md).

## Documentation

- [User guide](docs/USER-GUIDE.md)
- [Installation](docs/INSTALLATION.md)
- [Technical overview](docs/TECHNICAL.md)
- [Development setup](docs/DEVELOPMENT.md)
- [Release process](docs/RELEASING.md)
- [Changelog](docs/CHANGELOG.md)
- [Original proof-of-concept README](docs/POC-README.md)

## Release history

- **v0.3.0** — improved dashboard, cruise control and adaptive cruise display, session-based telemetry tracking, completed-job PDF export with session-based naming, logo usage, and refreshed installer/build documentation.
- **v0.2.0** — installer upgrade, launcher and shortcut integration, Steam game plugin installation, and optional final-page TikTok link.
- **v0.1.0** — initial Windows x64 telemetry proof of concept.

See the [changelog](docs/CHANGELOG.md) for details. Potential future API integration is not part of the current application.

## Disclaimer

This is a proof of concept. It has not been validated in live sessions across every supported game version. Missing telemetry values depend on the game and SDK; unavailable values are represented as `null`.

## NLSI

NLSI Telemetry is a local telemetry proof of concept. ETS2, ATS, and the SCS Telemetry SDK are products and technologies of their respective owners.
