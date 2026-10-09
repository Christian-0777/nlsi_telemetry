# NLSI Exclusive Logbook v1.3.8 Alpha

The v1.3.8 Alpha update makes TruckSim GPS the native application's only active
game telemetry provider and adds structured logs, formatted job history, PDF
export, and an automatically installed game plugin.

## What's new

- Use the verified official TruckSim GPS revision-13 shared-memory layout exclusively in the native application; the separate TruckSim GPS Server GUI is not required or packaged. RenCloud and the native NLSI UDP provider are no longer active or packaged.
- Add versioned UTF-8 `.nlsi` JSON Lines logging alongside the existing TXT log format, with strict schema and incomplete-record handling; retain prior user data during upgrades.
- Add Asia/Manila live date/time with millisecond precision, actual ping only when available (N/A for the current local telemetry interface), thousands separators, and two-place decimal formatting.
- Show cruise-control status/set speed and retarder status/level only when verified TruckSim fields are available.
- Add completed-job PDF export with recorded route, earnings, dates, and available statistics. Export leaves the original records unchanged and paginates long content.
- Add dynamic About version display, NLSI Discord/creator links, and TruckSim GPS/SCS attribution.
- Automatically install the hash-verified official x64/x86 plugin into matching architecture folders of detected Steam ETS2/ATS installations. Preserve conflicting DLLs and back up a previously managed DLL before replacing it.
- Keep the 900×600 minimum/default window size, app files in Program Files, and all logs/history under `%LOCALAPPDATA%\NLSI\Exclusive Logbook`.
- Package plugin/SCS license notices and omit GPL server implementation and GUI files.

## Validation notes

- Build, test, and installer compilation results will be recorded after running the release checks for this version.
- **NOT TESTED:** Installation into actual ETS2/ATS game folders and live ETS2/ATS gameplay telemetry.
- The installer target is `build/releases/v1.3.8-alpha/NLSI-Exclusive-Logbook-v1.3.8-alpha-Setup.exe`.

---

# NLSI Exclusive Logbook v1.3.6 Alpha

The v1.3.6 Alpha update removes duplicate page headings, improves job-history
reliability, and makes RenCloud mapping diagnostics explicit.

## What's new

- Kept the shared top header as the single source of page title and subtitle on every page; preserved the existing sidebar, NLSI branding, pink/white theme, and 900×600 minimum/default size.
- Used fresh driving evidence to classify an active job when its loaded flag is unavailable, persisted only identified terminal-job events, and made job-ID event persistence idempotent.
- Distinguished an open/mapped RenCloud shared-memory object from decoded telemetry. Unsupported revisions show unavailable freshness instead of being labeled as connected or stale.
- Verified the bundled RenCloud DLL's imported runtime dependencies and stage `MSVCP140.dll` and `VCRUNTIME140.dll` beside the optional game plugin. Plugin and dependency files remain hash-protected during install and uninstall.
- Updated the native application and installer to v1.3.6-alpha.

## Validation notes

- Revision 539 was not decoded: no running game/mapping or authoritative revision-539 layout definition was available to verify its offsets and field semantics. Compatibility remains NOT TESTED.
- Live ETS2/ATS telemetry and optional installation into actual Steam game folders require runtime validation.
- The installer target is `build/releases/v1.3.6-alpha/NLSI-Exclusive-Logbook-v1.3.6-alpha-Setup.exe`.

---

# NLSI Exclusive Logbook v1.3.5 Alpha

The v1.3.5 Alpha release restores local logging and history, implements
RenCloud shared-memory telemetry, and improves provider and page diagnostics.

## What's new

- Added a Windows shared-memory reader for the verified RenCloud SDK revision-12 layout, with mapping, layout, freshness, source-timestamp, and Win32 error diagnostics under Settings → Providers.
- Kept NLSI first in field-level selection and use fresh RenCloud data only as fallback. The installer can optionally place the genuine, MIT-licensed RenCloud plugin in detected ETS2/ATS Steam plugin folders without replacing a different DLL.
- Initialized per-user `logs` and `session_logs`, restored UTF-8 TXT logging, and persisted NLSI provider events, session lifecycles, and reliable delivered/cancelled job events.
- Populated Events, Sessions & trips, and Completed jobs views from persisted records and added explicit empty/error messages.
- Classified job status from available loaded/driving evidence even when optional job identity fields are absent.
- Added a distinct title and description for every top-level page while retaining the existing pink/white sidebar and 900×600 default/minimum window size.

## Validation notes

- RenCloud live mapping availability, current plugin revision, game-driven values, and installation behavior require ETS2/ATS runtime validation; consult Settings → Providers for diagnostics.
- The installer target is `build/releases/v1.3.5-alpha/NLSI-Exclusive-Logbook-v1.3.5-alpha-Setup.exe`.

---

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
