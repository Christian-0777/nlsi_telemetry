# Changelog

Release history is based on the repository's tagged v0.1.0 source and the confirmed v0.2.0/v0.3.0 release changes.

## v1.3.8 — Alpha

- Made TruckSim GPS the native app's sole active game telemetry provider; removed RenCloud and the NLSI UDP receiver from the native runtime/installer payload.
- Added versioned `.nlsi` logs alongside compatible TXT logs and documented the schema and strict read/write behavior.
- Added the Asia/Manila millisecond clock, consistent date/number formatting, and TruckSim cruise/retarder state where supplied.
- Added PDF export for completed jobs with recorded statistics and paginated long records.
- Added dynamic About version, requested social links, and plugin/SCS attribution.
- Made verified official TruckSim plugin installation automatic for detected supported Steam game architectures with collision protection and recovery backups.
- Retained local user data and the existing 900×600 default/minimum window size. Live gameplay and actual game-folder installation remain unverified.

## v1.3.7 — Alpha

- Replaced active RenCloud ingestion with a direct reader for TruckSim GPS plugin revision 13 at `Local\TSGPSTelemetry`; the separate TruckSim GPS Telemetry Server client is not needed.
- Added a guarded optional installer task for the official x64/x86 TruckSim GPS plugins with hash verification and plugin/SCS SDK MIT notices.
- Kept NLSI first in per-field selection, wired TruckSim GPS job delivery/cancellation events into the existing event and completed-job stores, and retained existing session/TXT logging.
- Reported missing plugin maps, unsupported layouts, inactive SDK state, and stale telemetry explicitly; disabled RenCloud in the provider UI while preserving its source and configuration.
- Updated native app and installer version metadata to v1.3.7-alpha; kept the shared header, theme, sidebar, and 900×600 window size.
- Live ETS2/ATS telemetry has not been tested for this release.

## v1.3.6 — Alpha

- Removed repeated titles and subtitles from all page bodies; the shared header remains the single title/subtitle source.
- Classified active jobs as in transit when fresh driving telemetry is available without a separate loaded flag, and only persisted identified terminal-job events.
- Reported mapped-but-unsupported RenCloud revisions without claiming their layout or freshness; revision 539 remains unsupported pending layout verification.
- Staged the genuine hash-verified RenCloud plugin and its imported MSVC runtime dependencies for the optional game-plugin install.
- Updated native app, installer, and documentation version metadata to v1.3.6-alpha.

## v1.3.5 — Alpha

- Implemented a RenCloud shared-memory reader for the verified revision-12 layout, with explicit mapping/access/layout/staleness diagnostics and field-level NLSI-first fallback.
- Added a hash-verified, optional RenCloud installer task with MIT notice and guarded update/uninstall behavior.
- Restored UTF-8 TXT logs and per-user `logs`/`session_logs`, preserved/migrated install-directory log files, and persisted provider events, sessions, and explicit delivery/cancellation jobs.
- Populated Events, Sessions & trips, and Completed jobs views from stored records with test coverage for details and tab switching.
- Classified job status from available loaded/driving evidence when optional identity fields are absent.
- Added page-specific header subtitles and updated application, installer, and release metadata.

## v1.3.4 — Alpha

- Extended the active sidebar navigation background across its full width without changing the NLSI pink/white horizontal brand header.
- Made each page scroll only when its own content exceeds the available space, preventing inactive pages from causing unnecessary scrollbars.
- Validated the existing 900×600 minimum/default, normal and large window sizes, and maximized layout.
- Updated application and installer metadata to v1.3.4-alpha without changing telemetry or backend behavior.

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
