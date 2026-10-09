# NLSI Exclusive Logbook

NLSI Exclusive Logbook is a native Windows desktop companion for ETS2 and ATS telemetry. The v1.3.8 Alpha C++ application uses Qt 6 Widgets and retains the existing Python implementation as reference-only code.

## What it does

- Presents live vehicle, navigation, job, session, and provider information supplied by the native telemetry core.
- Uses TruckSim GPS as its only active game telemetry provider and persists its events, telemetry-driven sessions, and completed/cancelled jobs locally; unsupported telemetry fields remain explicit rather than being fabricated.
- Keeps telemetry on the local computer; it does not upload data to a website or cloud service.
- Leaves the existing Python reference implementation and its event/job logging behavior in place.

## Supported games

- Euro Truck Simulator 2 (ETS2)
- American Truck Simulator (ATS)

The native app reads the official TruckSim GPS SCS plugin's revision-13 shared-memory map directly. The installer automatically installs the official plugin into matching x64/x86 folders for detected Steam ETS2/ATS installations; the separate TruckSim GPS Telemetry Server GUI is not included or required. Different existing game plugins are preserved, and a replaced installer-managed plugin is retained as a recovery backup.

## Current release

The current source version is **Alpha v1.3.8**. The native application can be configured and built with CMake and Qt 6.12; its Debug and Release output locations are documented in [development](docs/DEVELOPMENT.md). Release installers are published as GitHub Release assets when available:

**[Download NLSI release assets](https://github.com/Christian-0777/nlsi_telemetry/releases)**

## Installation

1. Download the latest published installer from the GitHub Release assets.
2. Close ETS2 and ATS before installing or updating.
3. Run the installer. It installs the application under Program Files, automatically installs the game plugin for detected supported Steam game folders, and creates Desktop and Start Menu shortcuts. Existing configuration, data, and logs are retained during an upgrade.

The CMake-built native application does not require a Python runtime. The reference-only Python code may require Python 3.10 or newer and the Windows Python Launcher (`py`).

## First run and using the application

Start **NLSI Exclusive Logbook** before launching either game. The plugin provides telemetry locally; the Qt UI refreshes from normalized application state at a controlled rate. The seven sidebar destinations are Dashboard, Live Drive, Jobs, History, Events, Settings, and About. Settings contains Application, Providers, Telemetry, and Active Mods subsections.

Dashboard shows overall connection status and current vehicle/session information; TruckSim mapping, layout, freshness, and Windows error status is available under Settings → Providers. Live Drive includes cruise-control and retarder values when present. Completed jobs can be exported to PDF without modifying the source records. History and Events display persisted local records with clear empty/error states. The native TruckSim GPS reader consumes `Local\TSGPSTelemetry`. Active mod detection is unavailable from the supported telemetry interface.

Game telemetry is available only while the game loads the plugin. See the [user guide](docs/USER-GUIDE.md) and [installation guide](docs/INSTALLATION.md) for setup and troubleshooting.

## Telemetry data

The native C++ application stores the existing TXT logs, versioned `.nlsi` logs, event records, session history, and completed/cancelled job history under `%LOCALAPPDATA%\NLSI\Exclusive Logbook`. The installer and uninstaller leave that per-user data intact. Application version 1.3.8 is separate from the SCS telemetry API value reported at runtime. See the [native log format](docs/NLSI-LOG-FORMAT.md), [technical documentation](docs/TECHNICAL.md), and [telemetry mapping](docs/TELEMETRY-MAPPING.md).

## Documentation

- [User guide](docs/USER-GUIDE.md)
- [Installation](docs/INSTALLATION.md)
- [Technical overview](docs/TECHNICAL.md)
- [Development setup](docs/DEVELOPMENT.md)
- [Release process](docs/RELEASING.md)
- [Changelog](docs/CHANGELOG.md)
- [Original proof-of-concept README](docs/POC-README.md)

## Release history

- **v1.0.0** — Baseline stability and production-readiness release for the official NLSI telemetry application. The project preserves the existing SCS SDK data flow, validates session lifecycle behavior, clarifies unavailable telemetry states, and keeps the application version separate from the SCS telemetry API version.
- **v0.3.4** — Correction release for trailer display, time formatting, gameplay-session timing, job and cruise telemetry mappings, and installer/runtime dependency validation while preserving the existing desktop interface and SCS telemetry contract.
- **v0.3.3** — Stability fixes for gameplay/session lifecycle, reconnect handling, and version metadata while retaining the desktop interface and telemetry engine.
- **v0.3.2** — Tkinter desktop UI with seven tabs, background telemetry worker, persisted event-derived job history, and manual PDF export; SCS telemetry handling retained.
- **v0.3.1** — telemetry correction pass for cruise control, adaptive cruise, retarder, throttle, brake, gear, fuel, ETA, and version labeling; keeps the official SCS telemetry API version separate from the NLSI application version.
- **v0.2.0** — installer upgrade, launcher and shortcut integration, Steam game plugin installation, and optional final-page TikTok link.
- **v0.1.0** — initial Windows x64 telemetry proof of concept.

See the [changelog](docs/CHANGELOG.md) for details. Potential future API integration is not part of the current application.

## Disclaimer

This is a proof of concept. It has not been validated in live sessions across every supported game version. Missing telemetry values depend on the game and SDK; unavailable values are represented as `null`.

## NLSI

NLSI Telemetry is a local telemetry proof of concept. ETS2, ATS, and the SCS Telemetry SDK are products and technologies of their respective owners.
