# NLSI Exclusive Logbook v1.3.1 Alpha

The v1.3.1 Alpha source refines the native Qt 6 Widgets interface for the existing C++ telemetry core. The Python implementation remains available as a reference and fallback; this source update is not a GitHub release.

## What's new

- Updated the version label and Windows application metadata to 1.3.1 Alpha.
- Reorganized navigation into Dashboard, Live Drive, Jobs, History, Events, Settings, and About.
- Grouped Providers, Telemetry diagnostics, and Active Mods under Settings.
- Applied the supplied NLSI logo and pink/white/charcoal color palette.
- Kept the compact dashboard and change-only widget updates.
- Did not modify the SCS plugin or any RenCloud DLL.

## Native provider limitation

The current native C++ RenCloud provider is a stub and does not ingest RenCloud telemetry. The native UI reports it as disconnected instead of claiming fallback is active. The existing Python reference continues to contain the tested field-level provider fallback behavior.

---

## v1.2 Alpha notes

NLSI Exclusive Logbook v1.2 Alpha introduces the next major application version for ETS2 and ATS telemetry while keeping the existing telemetry behavior, local logging flow, and persisted event/job data model. The release updates the application identity to the Alpha channel and prepares the project for a native Windows application architecture without removing the existing local telemetry semantics.

## What's new

- Native Windows application identity and version/channel metadata for the v1.2 Alpha release.
- Updated app naming to `NLSI Exclusive Logbook` and a distinct `Alpha v1.2` release label.
- Alpha release-channel awareness, installer defaults, and release metadata that can be extended to Beta/Public later without rewriting the updater.
- Installer improvements for the new install path under `C:\Program Files\NLSI Exclusive Logbook` while preserving the existing job/event logging structure where practical.
- GitHub update-check awareness for newer compatible releases with safe failure behavior when GitHub is unavailable.
- Continued support for the existing local JSONL event/job history and retained telemetry behavior across the application lifecycle.
- Removal of the legacy `C:\nlsi-tem` installation path from the default installer story and migration guidance for user data that should be preserved.

## Limitations

- This Alpha release continues to rely on the existing telemetry pipeline and persisted event records rather than a full native rewrite.
- Live ETS2/ATS validation remains dependent on supported game versions and telemetry availability.
- The underlying SCS plugin still governs what fields are exposed; unavailable values remain unavailable instead of being invented.

## Install or upgrade

Download the latest v1.2 Alpha installer from the [NLSI Telemetry GitHub Releases page](https://github.com/Christian-0777/nlsi_telemetry/releases). Close ETS2 and ATS before installing or updating. The installer should migrate away from the legacy `C:\nlsi-tem` layout to `C:\Program Files\NLSI Exclusive Logbook` while keeping application logs and user-generated job/session data where safe and appropriate.

The installer is designed to fail safely if GitHub is unavailable and to keep telemetry independent from the updater. See the [user guide](docs/USER-GUIDE.md) and [installation guide](docs/INSTALLATION.md) for setup and troubleshooting.
