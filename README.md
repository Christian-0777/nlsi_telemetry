# NLSI Exclusive Logbook

NLSI Exclusive Logbook is a native Windows desktop telemetry and logging application for Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS). Version 1.5.3-beta is built with C++ and Qt 6 Widgets. The existing Python implementation is retained as reference-only code and is not part of the active native telemetry pipeline.

## What It Does

- Displays vehicle, navigation, job, driving-session, and telemetry-provider information supported by the native telemetry decoder.
- Reads the TruckSim GPS plugin's revision-13 shared-memory mapping directly and supports a separate NLSI SCS Telemetry SDK 1.15 plugin for verified SDK channels.
- Provides a responsive Dashboard for driving telemetry, current-job information, and verified current-position availability.
- Calculates low-fuel percentage from valid SDK fuel amount and truck fuel capacity, and displays parking-brake status without replacing the service-brake reading.
- Supports Metric (default) and US customary presentation settings in supported dashboard, history, travel-summary, and PDF views.
- Reports recorded fines, tolls, ferry crossings, and train charges only when supported source event data is available.
- Reads active Workshop mods from ETS2/ATS game logs in Documents and links to valid Steam Workshop item pages.
- Records telemetry samples, source metadata, normalized values, and the complete shared-memory mapping in versioned v2 `.nlsi` telemetry files.
- Preserves application logs, events, session history, and job history locally.
- Uses explicit unavailable states for unsupported or invalid telemetry values.
- Supports offline recording without requiring a remote server or internet connection.
- Provides local history, event inspection, and PDF export for completed jobs.
- Checks GitHub releases for newer application versions when an internet connection is available.

## Supported Games

- Euro Truck Simulator 2 (ETS2)
- American Truck Simulator (ATS)

The native application reads the TruckSim GPS plugin's `Local\TSGPTelemetry` shared-memory mapping and validates revision 13 before decoding it. It also supports the separately built NLSI SCS Telemetry SDK 1.15 plugin for channels documented by the SDK, including truck fuel amount, truck fuel capacity from configuration, parking-brake state, and world placement. The two sources remain separate; unsupported or stale values are not substituted across sources.

The separate TruckSim GPS Telemetry Server GUI is not required and is not included. The installer packages the applicable x64/x86 game plugins and their required runtime components for the release. Existing game plugins are handled according to the installer's backup and replacement logic.

Plugin binaries and SDK components remain subject to their applicable licenses. The separate GPL-3.0 TruckSim GPS Telemetry Server application and its code are not redistributed.

## Current Release

- **Product:** NLSI Exclusive Logbook
- **Version:** v1.5.3-beta
- **Release channel:** Beta
- **Supported games:** ETS2 and ATS
- **Technology:** C++, Qt 6 Widgets, CMake

The native application can be configured and built with Qt 6.12 and CMake. Build locations and development requirements are documented in [Development Setup](docs/DEVELOPMENT.md).

Published installers and other available release assets can be found on GitHub:

**[Download NLSI release assets](https://github.com/Christian-0777/nlsi_telemetry/releases)**

This is a beta release intended for testing and evaluation. Compilation or installation alone does not establish that every telemetry field works correctly during live gameplay.

## Installation

1. Download the appropriate installer from the GitHub Releases page.
2. Close ETS2 and ATS before installing or updating.
3. Run the installer and follow the setup instructions.
4. Launch NLSI Exclusive Logbook and review the provider status under Settings → Providers.
5. Launch a supported game with the required telemetry plugin installed.

The installer places application files under Program Files and creates Desktop and Start Menu shortcuts. Existing user configuration, logs, telemetry files, and history are retained during upgrades.

Per-user application data is stored separately under:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook`

The native C++ application does not require a Python runtime. The reference-only Python implementation may require Python 3.10 or newer and the Windows Python Launcher (`py`).

See the [Installation Guide](docs/INSTALLATION.md) for detailed instructions and troubleshooting.

## First Run and Application Navigation

Start NLSI Exclusive Logbook before launching ETS2 or ATS.

The application provides the following sidebar destinations:

- **Dashboard** — Unified driving workspace for connection status, vehicle telemetry, navigation, and current-job information.
- **Jobs** — Recorded job information and available job actions.
- **History** — Locally recorded driving sessions and job history.
- **Events** — Persisted application and telemetry-related events.
- **Settings** — Application preferences, provider diagnostics, and telemetry-related settings.
- **About** — Product information, version details, and available update checks.

### Dashboard

The Dashboard combines the driving workspace and supported live telemetry into one view.

Depending on source availability, it displays:

- Connection, game, and telemetry-provider status.
- Vehicle speed, RPM, selected gear, throttle, service brake, fuel, and parking-brake state when supported by valid sources.
- A low-fuel warning at or below 20% when both SDK fuel amount and fuel capacity are valid and current; missing, invalid, or stale readings suppress the warning.
- Current-job identification, cargo, source, destination, and job status.
- Navigation distance and estimated travel time when valid source data is available.
- Cruise-control and retarder information when the corresponding source values are available and valid.

The application must not infer cruise-control activation solely from a configured speed. Retarder-active status is derived from a positive SDK retarder level because the documented revision-13 mapping does not provide a separate active boolean. Parking-brake state is reported independently from the service-brake value. Fuel percentage is calculated only from valid SDK fuel amount and configured tank capacity; raw values and historical records are not rewritten by display-unit conversion.

Disconnected, stale, unsupported, or invalid telemetry must not be presented as current driving data.

### Jobs, History, and Events

These sections display locally recorded job information, completed or cancelled job history, driving sessions, and persisted events.

Completed jobs can be exported to PDF without modifying their original source records.

Recorded fines, toll fees, and ferry or train charges are included in travel history and expense notifications only when supported source event data is available. Missing amounts are shown as unavailable rather than inferred. Display-unit preferences affect presentation, not stored raw values.

### Settings and Provider Diagnostics

Settings provides access to application preferences and provider and telemetry diagnostics.

Provider diagnostics include applicable shared-memory mapping and revision information, source freshness, and relevant storage or provider errors.

Active mod enumeration is not available through the supported telemetry interface and is not presented as verified telemetry.

See the [User Guide](docs/USER-GUIDE.md) for operating instructions.

## GitHub Update Checker

The application can check GitHub Releases for newer versions of NLSI Exclusive Logbook.

Repository:

https://github.com/Christian-0777/nlsi_telemetry

Release API:

https://api.github.com/repos/Christian-0777/nlsi_telemetry/releases

The update checker is intended to identify newer applicable releases, including prerelease versions where supported by the application.

- Update checks require an internet connection.
- Network failures and unavailable release information must not prevent offline application use.
- Release notes and the corresponding GitHub release page can be presented to the user.
- The update checker does not automatically download or install a new version.

Availability and behavior depend on the update-checking implementation included in the build.

## Telemetry and Local Data

All application data is stored locally under:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook`

The application maintains separate formats for application logs and telemetry samples.

### Application Logs — `.txt` and `.nlsi` Schema Version 1

The existing UTF-8 TXT log and versioned `.nlsi` application log record human-readable application messages.

The v1 `.nlsi` format uses newline-delimited JSON (JSON Lines). It begins with a format header followed by timestamped application entries.

The v1 application-log reader does not reinterpret v2 telemetry records as application logs. Invalid or unknown records are reported rather than silently replaced.

### Telemetry Logs — `.nlsi` Schema Version 2

Telemetry is written to separate daily files in:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook\telemetry`

Files use UTC dates in their names and rotate at 128 MiB without automatically deleting older records.

Each telemetry record includes:

- A stable record ID and local sequence number.
- The associated driving-session ID, when available.
- A UTC capture timestamp.
- Provider and mapping-revision metadata.
- Source timestamps preserved without assuming undocumented counter units.
- Named raw source fields and their availability.
- A compressed, Base64-encoded copy of the complete 32 KiB shared-memory mapping.
- Normalized application fields and their availability and freshness metadata.

The raw mapping preserves bytes beyond the named fields currently understood by NLSI. The named-field inventory describes what the current decoder can verify and does not claim that the mapping contains no other fields.

The application writes telemetry through a background writer. Incomplete trailing records are handled through the documented recovery procedure. Malformed complete records and unsupported schemas are preserved and reported. Pending queue entries can be reconstructed from valid local records after an interrupted write sequence.

The existing application-log format is not changed by telemetry capture. TXT logs, application logs, sessions, jobs, and telemetry files remain separate.

For record structures, recovery rules, and inspection instructions, see:

- [Native Log Formats](docs/NLSI-LOG-FORMAT.md)
- [Telemetry Mapping](docs/TELEMETRY-MAPPING.md)

## Offline Operation and Synchronization Status

**Remote synchronization is not implemented unless a separate, verified implementation is added to the application.**

The local application does not upload telemetry to a website or cloud service, connect directly to MySQL, or mark records as remotely synchronized merely because they are queued.

The repository contains a proposed SQL migration at `db/migrations/001_telemetry_sync.sql`. This is a database design artifact and has not been applied. A proposed schema does not constitute a working backend, authentication provider, migration runner, or telemetry upload API.

A future synchronization service must use authenticated HTTPS, enforce record ownership and authorization, validate payloads, support idempotent uploads, and acknowledge records only after durable server-side acceptance.

See [SQL Schema Design](docs/SQL-SCHEMA.md) for the proposed database mapping, validation requirements, and rollback procedure.

## Telemetry Limitations

The native decoder rejects unsupported shared-memory revisions rather than guessing their layouts. SCS SDK fuel and parking-brake fields are used only when the relevant SDK values are available, valid, and current.

The documented revision-13 mapping does not provide verified latitude/longitude coordinates, authenticated-account identity, game version, or a delivery-time source field. These values remain unavailable unless a separate reliable source is implemented and verified.

Other limitations include:

- The low-fuel warning is suppressed when fuel amount or truck fuel capacity is missing, invalid, unsupported, or stale.
- Expense amounts depend on actual event data supplied by the supported source; no missing charge is fabricated.
- Adaptive cruise-control information is not reliably exposed by the documented active decoder.
- Active mod enumeration is unavailable through the supported telemetry interface.
- Retarder-active state is derived from retarder level rather than a separate source boolean.
- Source timestamp counters are preserved without assigning undocumented time units.
- Toll fees and ferry/train crossing data are unavailable when the active source does not record them.
- Live gameplay behavior must be verified against the actual game, plugin, and application build.

The application version is separate from the telemetry API or mapping revision reported by the provider.

## Documentation

- [User Guide](docs/USER-GUIDE.md)
- [Installation Guide](docs/INSTALLATION.md)
- [Technical Overview](docs/TECHNICAL.md)
- [Telemetry Mapping](docs/TELEMETRY-MAPPING.md)
- [Native Log Formats](docs/NLSI-LOG-FORMAT.md)
- [SQL Schema Design](docs/SQL-SCHEMA.md)
- [Development Setup](docs/DEVELOPMENT.md)
- [Release Process](docs/RELEASING.md)
- [Changelog](docs/CHANGELOG.md)
- [Original Proof-of-Concept README](docs/POC-README.md)

## Release History

- **v1.5.3-beta** — Adds SDK-backed fuel-percentage warning and parking-brake status, Metric/US customary display preferences, supported travel expense reporting, and updated x64/x86 SCS plugin and runtime packaging. Automated validation passed (46 Python tests, 2/2 Release CTest targets, and 3 consecutive GUI test passes). Fresh install/upgrade and live ETS2/ATS gameplay remain unverified.
- **v1.5.2-beta** — Previous beta release; see its release assets and changelog for the exact changes.
- **v1.4.8-beta** — Dashboard layout, verified live driving fields, and installer metadata updates.
- **v1.4.5-beta** — Fixes local telemetry recovery and shutdown draining, applies IANA Asia/Manila time consistently, and preserves distinct legacy and telemetry log formats.
- **v1.4.3-beta** — Responsive Dashboard cards, Current Position/active-job destination, game-log-based active Workshop mods, and exact telemetry-event filtering. The revision-13 TruckSim map does not expose current coordinates; v1.4.4 adds a separate SCS SDK source.
- **v1.4.2-beta** — Beta release focused on the unified driving Dashboard, interface refinements, single-instance behavior, exit confirmation, and GitHub release checking. Confirm the included build and changelog for the exact implemented features.
- **v1.4.0-alpha** — Native Qt telemetry and logging release. Introduced the revision-13 TruckSim GPS shared-memory reader, separate v2 telemetry logs, raw mapping preservation, local pending synchronization records, and updated provider diagnostics. Live gameplay telemetry was not validated for this release.
- **v1.0.0** — Baseline stability and production-readiness milestone for the telemetry application, with clearer session lifecycle behavior and explicit unavailable telemetry states.
- **v0.3.4** — Corrections to trailer display, time formatting, gameplay-session timing, job and cruise telemetry mappings, and installer/runtime dependency validation.
- **v0.3.3** — Stability fixes for gameplay sessions, reconnect handling, and version metadata.
- **v0.3.2** — Tkinter desktop UI, background telemetry worker, event-derived job history, and manual PDF export.
- **v0.3.1** — Telemetry corrections for cruise control, adaptive cruise, retarder, throttle, brake, gear, fuel, ETA, and version labeling.
- **v0.2.0** — Installer, launcher, shortcut integration, Steam game plugin installation, and optional final-page TikTok link.
- **v0.1.0** — Initial Windows x64 telemetry proof of concept.

See the [Changelog](docs/CHANGELOG.md) for release-specific changes.

## Disclaimer

NLSI Exclusive Logbook is provided as a beta release for testing and evaluation. Telemetry availability depends on the game, plugin, mapping revision, and validity of source data.

Keep backups of important local records. The application does not currently provide a general-purpose remote synchronization service or an automatic telemetry retention policy.

## Company and Third-Party Acknowledgements

Nabski Logistics and Solutions Inc. develops NLSI Exclusive Logbook.

Euro Truck Simulator 2, American Truck Simulator, the SCS Telemetry SDK, and TruckSim GPS remain the property of their respective owners and contributors. Third-party software is subject to its applicable licenses and notices.