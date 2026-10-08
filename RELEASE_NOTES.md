# NLSI Exclusive Logbook v1.3.4 Alpha

The v1.3.4 Alpha release refines the native Qt 6 Widgets layout and updates the Windows installer.

## What's new

- Extended the active navigation background across the full sidebar while keeping the horizontal NLSI branding and pink/white theme.
- Made page scrolling conditional on that page's content exceeding the available window space; removed scrollbars caused by inactive pages.
- Tested responsive layouts at 900×600, normal, large, and maximized window sizes without changing the 900×600 minimum or default.
- Updated the installer and executable metadata to v1.3.4-alpha while preserving update detection, user data, branding, and installer choices.
- Preserved telemetry, providers, SCS SDK integration, sessions, jobs, logging, and updater behavior.

## Validation notes

- The installer target is `build/releases/v1.3.4-alpha/NLSI-Exclusive-Logbook-v1.3.4-alpha-Setup.exe`.

---

# NLSI Exclusive Logbook v1.3.3 Alpha

The v1.3.3 Alpha release updates the native Qt 6 Widgets application and its Windows installer.

## What's new

- Reorganized the shared sidebar into compact WORKSPACE and SYSTEM sections with a horizontal NLSI logo-and-brand header, icons, clear active-page state, and a bottom Close action.
- Kept all navigation connected to the existing stacked-page system and retained the 900×600 minimum and default window size.
- Added explicit existing-install detection and an update summary showing installed and new versions.
- Preserved configuration and log files during in-place updates; legacy `C:\nlsi-tem` cleanup now leaves known user-data folders untouched.
- Made installer packaging reject an executable whose Windows version metadata is not v1.3.3 Alpha.
- Kept the release marked Alpha and retained the existing branding, terms/privacy acceptance, shortcuts, and Qt/MSVC runtime packaging.

## Validation notes

- The installer target is `build/releases/v1.3.3-alpha/NLSI-Exclusive-Logbook-v1.3.3-alpha-Setup.exe`.
- The native RenCloud provider remains a stub and does not ingest RenCloud telemetry.

---

# NLSI Exclusive Logbook v1.3.2 Alpha

The v1.3.2 Alpha source refines the native Qt 6 Widgets interface for the existing C++ telemetry core. The Python implementation remains available as a reference and fallback; this source update is not a GitHub release.

## What's new

- Moved detailed NLSI and RenCloud provider information from the Dashboard into Settings → Providers; Dashboard shows only overall connection status.
- Opened the application at the existing 900×600 minimum window size without changing that minimum.
- Improved the Dashboard layout while preserving the Qt 6 interface and official NLSI pink/white branding.
- Kept UI refresh change-only and telemetry processing separate from the GUI.
- Updated application, Windows resource, and installer versions to v1.3.2-alpha.
- Did not modify telemetry/backend behavior, the SCS plugin, or any RenCloud DLL.

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
