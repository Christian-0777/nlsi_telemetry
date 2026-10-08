# Changelog

Release history is based on the repository's tagged v0.1.0 source and the confirmed v0.2.0/v0.3.0 release changes.

## v1.3.3 — Alpha

- Reorganized the compact shared sidebar into WORKSPACE and SYSTEM navigation with a horizontal NLSI brand header, consistent icons, and a bottom Close action.
- Kept the existing page stack, active-page state, 900×600 minimum, and default window size.
- Added installer detection and version reporting for existing installations; updates preserve configuration and log data.
- Preserved known user data when cleaning up the legacy `C:\nlsi-tem` directory.
- Guarded installer packaging against stale executables using Windows version metadata.

## v1.3.2 — Alpha

- Moved detailed NLSI and RenCloud provider status from the Dashboard to Settings → Providers; Dashboard now reports only overall connection status.
- Made the native app open at its existing 900×600 minimum size and improved the Dashboard layout for that footprint.
- Updated the native application, Windows metadata, and x64 installer to v1.3.2-alpha.
- Preserved the existing provider/telemetry backend, privacy and terms acceptance, installation directory, shortcuts, and wizard assets.

## v1.3.1 — Alpha

- Updated the native application version and Windows file metadata to 1.3.1 Alpha.
- Reorganized the Qt sidebar into Dashboard, Live Drive, Jobs, History, Events, Settings, and About; diagnostic pages are grouped under Settings.
- Applied the official NLSI pink, white, and charcoal palette and embedded the supplied logo assets.
- Simplified the dashboard and kept unavailable native history/provider capabilities explicit.

## v1.3.0 — Alpha

- Replaced the native Win32 window with a Qt 6 Widgets dashboard and reusable telemetry pages.
- Added stable job identity and independent navigation-based progress and ETA presentation.
- Added provider, session, telemetry, and unsupported-value diagnostics without fabricating telemetry.
- Preserved the existing Python implementation as a reference and fallback.
- Set the native application version to 1.3.0 Alpha.

## v1.2.0 — Alpha release preparation

### Implemented

- Updated the canonical application metadata for the v1.2 Alpha channel and named the app `NLSI Exclusive Logbook`.
- Added explicit release-channel support for Alpha, Beta, and Public while keeping the current build on the Alpha channel.
- Updated the installer path and release metadata to align with the Program Files install target and the legacy-vs-new migration story.
- Kept the existing telemetry behavior and JSONL job/session log retention model intact while establishing the new v1.2 release identity.
- Added explicit release-note and update-check metadata consistent with the planned GitHub-based updater flow.

### Notes / limitations

- This is a version-identity and release-channel update for the current codebase; a full native C++ rewrite remains an architectural follow-up rather than an in-place drop-in change.
- Existing local event and job records remain the source of truth for persisted history and should be preserved during migration.

## v1.1.1 — version metadata correction

- Updated the canonical application version in `version.json` to `1.1.1`; the GUI and packaged agent read this value directly.
- Updated current release and installer naming documentation. Build scripts continue to derive versioned DLL and installer output paths from `version.json`.
- Kept the native plugin's SCS Telemetry API contract at `1.01`; it does not report an independent NLSI application version.

## v1.1.0 — telemetry correction and lifecycle update

### Implemented

- Corrected the effective throttle model to prefer the SCS `truck.effective.throttle` channel over raw accelerator input while preserving raw input for debugging.
- Added explicit retarder and cruise-control telemetry handling using the official SDK channels and derived state where the SDK does not provide a direct boolean flag.
- Kept brake telemetry separate from retarder and cruise-control behavior, and promoted the active-game/session state machine to reset stale job and telemetry state on disconnects or session boundaries.
- Clarified job ID and job type handling so NLSI-generated IDs remain distinct from any real SCS job identifier and the UI falls back to `UNKNOWN` when the SDK does not provide a reliable type.
- Hid unsupported game-time fields instead of fabricating values when the SDK does not surface them.

### Notes / limitations

- RenCloud is treated as a reference implementation only; the NLSI plugin remains self-contained and uses the official SCS SDK directly.
- Field availability still depends on the installed game, SDK version, and live telemetry data.

## v0.3.2 — desktop interface update

### Implemented

- Updated the NLSI application release version to `0.3.2`; the SCS telemetry API value remains independent.
- Replaced the default CMD dashboard with a Tkinter window containing Main, Finished Jobs, PDF Export, Debug, Events, Active Mods, and About Us tabs.
- Runs the existing UDP telemetry agent in a background worker and delivers snapshots to Tkinter using a thread-safe queue.
- Shows persisted job and event history from the existing JSONL event stream without inventing unavailable fields.
- Makes PDF creation an explicit user action and prompts for the output path.
- Includes the existing logo in the app package and shortcut/installer icon.

### Notes / limitations

- The current plugin does not supply game simulation time or reliable active-mod data; the corresponding tabs/fields explain that limitation.
- Existing event JSONL is the available persisted source for session and job views; this release does not add a second event manager.
- A live ETS2/ATS session is still required to validate real plugin traffic.

## v0.3.0 — telemetry improvements release

### Implemented

- Updated the authoritative version in `version.json` to `0.3.0`.
- Improved the console dashboard and kept the live telemetry model intact while adding explicit cruise-control indicators and session metadata.
- Added session IDs to session lifecycle records and preserved session data over the life of a run.
- Added a completed-job report mechanism.
- In v0.3.2 PDF creation is manual only and the user selects the output path.
- Used the project logo in the operational report and kept the export as a local report layer instead of a primary data store.
- Kept the install and upgrade flow compatible with the v0.2.0 app identity and data retention model.

### Notes / limitations

- Adaptive cruise control is displayed as `N/A` when the official SCS telemetry fields are not exposed in the currently used SDK.
- Live validation across all game versions remains dependent on the underlying game telemetry availability.

## v0.2.0 — update release

### Implemented

- Updated the authoritative version in `version.json`.
- Added a unified `build-release.bat` command that validates the SDK, builds and verifies the native DLL, runs tests, invokes the existing installer builder, and verifies the release artifacts.
- Updated the Inno Setup package to install the launcher BAT and create Desktop and Start Menu shortcuts.
- Added Steam library detection using registry entries, `libraryfolders.vdf`, and common drive-root Steam folders, then installs a copy of the canonical DLL for detected ETS2/ATS installations.
- Added per-game plugin status and persistent installer logging; a locked plugin copy is reported as a failure.
- Kept the v0.1.0 application identity for in-place upgrades and retained configuration/data/log directories and agent event output.
- Added an optional final installer page with a TikTok checkbox. The browser opens only when the checkbox is selected and the user completes setup.
- Reorganized the README and added user, installation, technical, development, and release documentation while preserving the original technical README as `docs/POC-README.md`.

### Planned / not included

- Validation in live ETS2 and ATS sessions across supported game versions remains outstanding.
- NLSI API upload, authentication, cloud storage, and Driver Portal integration are not implemented.

## v0.1.0 — initial Windows x64 release

The repository contains the `v0.1.0` tag. That release introduced the SCS SDK native plugin and local Python UDP agent for ETS2/ATS, a console telemetry dashboard, JSONL event/session recording, and the first Inno Setup package.
