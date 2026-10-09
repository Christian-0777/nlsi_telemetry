
# NLSI Exclusive Logbook

NLSI Exclusive Logbook is a native Windows desktop telemetry and logging application for Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS). The v1.4.0-alpha native application is built with C++ and Qt 6 Widgets. The existing Python implementation is retained as reference-only code and is not the active native telemetry pipeline.

## What It Does

- Displays vehicle, navigation, job, driving-session, and telemetry-provider information supported by the active native telemetry decoder.
- Reads the official TruckSim GPS plugin's revision-13 shared-memory map directly.
- Records telemetry samples, source metadata, normalized values, and the complete shared-memory mapping in separate versioned v2 `.nlsi` telemetry files.
- Preserves application logs, events, session history, and job history locally.
- Represents unsupported or invalid telemetry values as unavailable instead of inventing data.
- Supports offline recording without requiring a remote server or an internet connection.
- Provides local history, event inspection, and PDF export for completed jobs.

## Supported Games

- Euro Truck Simulator 2 (ETS2)
- American Truck Simulator (ATS)

The native application reads the TruckSim GPS plugin's `Local\TSGPTelemetry` shared-memory mapping and validates its revision before decoding it. The supported mapping revision is 13.

TruckSim GPS is the only active game telemetry provider in the native application. The separate TruckSim GPS Telemetry Server GUI is not required and is not included.

The installer installs the official TruckSim GPS plugin into compatible x64/x86 folders for detected supported Steam game installations. Existing game plugins are handled according to the installer’s backup and replacement logic; a replaced installer-managed plugin is retained as a recovery backup.

The official plugin binaries are distributed with their applicable notices. The SCS SDK and other third-party components remain subject to their respective licenses. The separate GPL-3.0 TruckSim GPS Telemetry Server application and its code are not redistributed.

## Current Release

The current source version is **v1.4.0-alpha**.

The native application can be configured and built with CMake and Qt 6.12. Debug and Release build locations are documented in [Development Setup](docs/DEVELOPMENT.md).

Published installers and other available release assets can be found here:

**[Download NLSI release assets](https://github.com/Christian-0777/nlsi_telemetry/releases)**

This is an alpha release. Live ETS2/ATS gameplay telemetry has **not been validated for v1.4.0-alpha**. Successful compilation or installation does not establish that every telemetry field works correctly in a live game session.

## Installation

1. Download the appropriate installer from the GitHub Releases page.
2. Close ETS2 and ATS before installing or updating.
3. Run the installer and follow the setup instructions.
4. Launch NLSI Exclusive Logbook and verify the provider status under Settings → Providers.
5. Launch a supported game with the required telemetry plugin installed.

The installer places application files under Program Files and creates Desktop and Start Menu shortcuts. Existing user configuration, logs, telemetry files, and history are retained during upgrades. Per-user data is stored separately under:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook`

The native C++ application does not require a Python runtime. The reference-only Python implementation may require Python 3.10 or newer and the Windows Python Launcher (`py`).

See the [Installation Guide](docs/INSTALLATION.md) for detailed instructions and troubleshooting.

## First Run and Application Navigation

Start NLSI Exclusive Logbook before launching ETS2 or ATS.

The application provides seven sidebar destinations:

- Dashboard
- Live Drive
- Jobs
- History
- Events
- Settings
- About

Settings includes Application, Providers, Telemetry, and Active Mods subsections.

### Dashboard

Displays connection status and current vehicle and driving-session information based on the values available from the active provider.

### Live Drive

Displays supported live telemetry, including speed, RPM, gear, throttle, brake, fuel, navigation information, cruise-control values, and retarder information when their source values are available and valid.

Retarder-active status is derived from a positive SDK retarder level because the documented revision-13 mapping does not provide a separate active boolean. Cruise-control enabled state must not be inferred solely from a set speed.

### Jobs, History, and Events

Displays locally recorded job information, completed or cancelled job history, driving sessions, and persisted events.

Delivery and cancellation events are generated from the documented source flags and event-detection rules. Job fields that are absent or invalid remain unavailable rather than being fabricated.

Completed jobs can be exported to PDF without modifying the original source records.

### Settings → Providers

Displays provider status, shared-memory mapping and revision information, source freshness, and applicable storage, synchronization, and shared-memory errors.

Active mod detection is unavailable through the supported telemetry interface and is not presented as verified game telemetry.

See the [User Guide](docs/USER-GUIDE.md) for operating instructions.

## Telemetry and Local Data

All application data is stored locally under:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook`

The application maintains separate formats for application logs and telemetry samples.

### Application Logs — `.txt` and `.nlsi` Schema Version 1

The existing UTF-8 TXT log and versioned `.nlsi` application log continue to record human-readable application messages.

The v1 `.nlsi` format uses newline-delimited JSON (JSON Lines). It begins with a format header followed by timestamped application entries.

The existing v1 reader does not reinterpret v2 telemetry records as application logs. Invalid or unknown v1 records are reported instead of being silently replaced.

### Telemetry Logs — `.nlsi` Schema Version 2

Telemetry is written to separate daily files in:

`%LOCALAPPDATA%\NLSI\Exclusive Logbook\telemetry`

Files use UTC dates in their names and rotate at 128 MiB without automatically deleting older records.

Each telemetry record includes:

- A stable record ID and local sequence number.
- The associated driving-session ID, when available.
- A UTC capture timestamp.
- Provider and mapping-revision metadata.
- Source timestamps, preserved without assuming undocumented counter units.
- Named raw source fields and their availability.
- A compressed, Base64-encoded copy of the complete 32 KiB shared-memory mapping.
- Normalized application fields and their availability and freshness metadata.

The raw mapping preserves bytes beyond the named fields currently understood by NLSI. The named-field inventory describes what the current decoder can verify; it does not claim that the mapping contains no other fields.

The application writes telemetry through a background writer. Incomplete trailing records are handled through the documented recovery procedure, while malformed complete records and unsupported schemas are preserved and reported. Pending queue entries can be reconstructed from valid local records after an interrupted write sequence.

The existing application log format is not changed by telemetry capture. TXT logs, application logs, sessions, jobs, and telemetry files remain separate.

For the exact record structures, recovery rules, and inspection instructions, see [Native Log Formats](docs/NLSI-LOG-FORMAT.md) and [Telemetry Mapping](docs/TELEMETRY-MAPPING.md).

## Offline Operation and Synchronization Status

**Remote synchronization is not implemented in v1.4.0-alpha.**

The application does not upload telemetry to a website or cloud service, connect directly to MySQL, or mark records as remotely synchronized. Local queue entries remain pending until a future authenticated synchronization service is implemented.

The repository currently contains a proposed versioned SQL migration at `db/migrations/001_telemetry_sync.sql`. It is a database design artifact and has not been applied. There is no implemented remote backend, authentication provider, migration runner, or telemetry upload API.

A future synchronization service must use authenticated HTTPS, enforce record ownership and authorization, validate payloads, support idempotent uploads, and acknowledge records only after durable server-side acceptance.

See [SQL Schema Design](docs/SQL-SCHEMA.md) for the proposed database mapping, validation requirements, and rollback procedure.

## Telemetry Limitations

The native decoder rejects unsupported shared-memory revisions rather than guessing their layouts.

The documented revision-13 mapping does not provide verified latitude/longitude coordinates, authenticated-account identity, game version, or a delivery-time source field. These values remain unavailable unless a separate reliable source is implemented and verified.

Other limitations include:

- Adaptive cruise-control information is not reliably exposed by the documented active decoder.
- Active mod enumeration is unavailable through the supported telemetry interface.
- Retarder-active state is derived from retarder level rather than a separate source boolean.
- Source timestamp counters are preserved without assigning undocumented time units.
- Live ETS2/ATS telemetry has not been validated for v1.4.0-alpha.

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

- **v1.4.0-alpha** — Native Qt telemetry and logging release. Introduces the revision-13 TruckSim GPS shared-memory reader, separate v2 telemetry logs, raw mapping preservation, local pending synchronization records, and updated provider diagnostics. Live gameplay telemetry is not yet validated for this release.
- **v1.0.0** — Baseline stability and production-readiness milestone for the telemetry application, with clearer session lifecycle behavior and explicit unavailable telemetry states.
- **v0.3.4** — Corrections to trailer display, time formatting, gameplay-session timing, job and cruise telemetry mappings, and installer/runtime dependency validation.
- **v0.3.3** — Stability fixes for gameplay sessions, reconnect handling, and version metadata.
- **v0.3.2** — Tkinter desktop UI, background telemetry worker, event-derived job history, and manual PDF export.
- **v0.3.1** — Telemetry corrections for cruise control, adaptive cruise, retarder, throttle, brake, gear, fuel, ETA, and version labeling.
- **v0.2.0** — Installer, launcher, shortcut integration, Steam game plugin installation, and optional final-page TikTok link.
- **v0.1.0** — Initial Windows x64 telemetry proof of concept.

See the [Changelog](docs/CHANGELOG.md) for release-specific changes.

## Disclaimer

NLSI Exclusive Logbook is provided as an alpha release for testing and evaluation. It has not been validated in live gameplay across every supported game version. Telemetry availability depends on the game, plugin, mapping revision, and validity of source data.

Keep backups of important local records. The application does not currently provide remote synchronization or an automatic telemetry retention policy.

## Company and Third-Party Acknowledgements

Nabski Logistics and Solutions Inc. develops NLSI Exclusive Logbook.

Euro Truck Simulator 2, American Truck Simulator, the SCS Telemetry SDK, and TruckSim GPS remain the property of their respective owners and contributors. Third-party software is subject to its applicable licenses and notices.
