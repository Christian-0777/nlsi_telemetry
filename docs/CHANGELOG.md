# Changelog

Release history is based on the repository's tagged v0.1.0 source and the confirmed v0.2.0/v0.3.0 release changes.

## v1.5.1-beta

- Aligns the application and installer metadata to the 1.5.1-beta release while preserving the existing SCS SDK plugin, historical job data model, and user-data paths.
- Keeps the completed-job capture, fuel calculations, travel/expense summaries, and installer validation behaviors from the 1.5.0-beta release without introducing fabricated values.

## v1.5.0-beta

- Persists available, non-stale final job snapshot fields with terminal job records; missing fields stay N/A without substituting current telemetry.
- Shows only Delivered/Cancelled persisted job cards on the Jobs page and removes the duplicate Completed Jobs History tab without changing stored history or PDF export behavior.
- Makes completed-job cards responsive with wrapping values and a single-column narrow layout; preserves each record's independent PDF export.
- Replaces the Events table with a bounded, wrapped event log that presents only each event's timestamp, source, type, and data and refreshes through the existing GUI-thread history polling.
- Uses a single-line Asia/Manila page header and consistent 11/12/14 px typography across the application.
- Updates application, installer, release paths, tests, and release documentation while preserving installation and user-data directories.

## v1.4.9-beta

- Replaces the Jobs navigation page with Completed Jobs cards backed only by persisted Delivered/Cancelled records; pending and active jobs remain hidden.
- Adds per-job PDF export, clear N/A values for unavailable event fields, and newly detected terminal-job desktop notifications deduplicated by the existing stable job ID.
- Renames Dashboard Connection to Game Config and limits it to game, version, vehicle, and trailer fields without changing current-job behavior or provider diagnostics.
- Removes shared page-header subtitles, adds a responsive Manila date/time header, and standardizes required UI labels, buttons, and table headers to uppercase.
- Updates application and installer metadata to v1.4.9-beta while preserving installation and user-data locations and existing history formats.

## v1.4.8-beta

- Replaces the generic Dashboard card grid with the specified two-column current-job, driving-telemetry, and connection sections.
- Uses fresh validated cruise-control state for identical `A` markers beside throttle, brake, and retarder; hides them when inactive, stale, unavailable, or disconnected.
- Formats navigation ETA as a remaining-duration `HH:MM:SS`, rejects invalid values, and keeps unavailable game version, vehicle identity, and ping as N/A.
- Applies responsive 8–14 px Dashboard text sizing and keeps the 900×600 minimum window, existing pages, telemetry providers, and local formats.
- Validates registered installation path/version before upgrades, uses `C:\Program Files\NLSI Exclusive Logbook` as the default, and leaves legacy and per-user data untouched.
- Aligns application and installer release metadata to v1.4.8-beta; SDK and shared-memory versions are unchanged.

## v1.4.7-beta

- Initializes ETS2/ATS `game.log.txt` files when available, parses the current session once, and incrementally monitors appended lines with replacement, truncation, rotation, restart, partial-line, and UTF-8 handling.
- Adds per-game Reinitialize controls with asynchronous parsing and explicit initializing, monitoring, unavailable, read-error, and completion states.
- Separates subscribed, mounted, active Workshop, and active local evidence; ambiguous log boundaries are reported instead of confirming stale entries.
- Adds generic archive deduplication/grouping for ProMods packages and responsive text wrapping, table elision, and path handling.
- Updates version metadata while preserving existing providers, schemas, branding, installation identity, user data, and prior release artifacts.

## v1.4.5-beta

- Stops telemetry producers before requesting a writer drain and reports draining, completed, timed-out, and failed shutdown states; slow work remains cancellable from the UI without terminating its worker.
- Persists queued telemetry to atomic recovery files before appending it, then reconciles interrupted writes by stable record ID on restart.
- Uses the IANA `Asia/Manila` zone for local timestamps, display conversion, and telemetry day grouping while retaining UTC machine-readable telemetry timestamps.
- Keeps the v1 application `.nlsi`, v2 telemetry `.nlsi`, and UTF-8 TXT formats distinct and updates their recovery/time-zone documentation.
- Preserves application identity, user-data location, installer policy, and prior release artifacts.

## v1.4.4-beta

- Adds the position-only SCS SDK 1.15 `nlsi.dll` plugin for x64 and x86; the plugin subscribes only to `truck.world.placement`.
- Transfers XYZ world coordinates through the dedicated versioned `Local\\NLSI.SCS.Position.v1` interface without changing TruckSim GPS telemetry.
- Adds a separate Qt position provider, explicit stale/disconnected state, and current world coordinates on Dashboard; city and country are not inferred without compatible local map data.
- Adds separate ETS2/ATS plugin installation with backup and safe managed-file restoration/removal.
- Uses version-isolated v1.4.4-beta application, plugin, and installer output directories.
- Live gameplay was not validated; offline IPC and installer tests cover the new path.

## v1.4.3-beta

- Reflows every Dashboard card into responsive one-, two-, or three-column layouts, enables consistent wrapping, and adds Current Position with active-job destination state.
- Reads the ETS2 and ATS Documents `game.log.txt` files for active Workshop entries only, with validated source links, optional Steam preview thumbnails, and explicit missing/stale log status.
- Restricts trip history to `player.use.ferry`, `player.use.train`, and `player.tollgate.paid`; duplicate records are collapsed and absent fees remain unavailable.
- Preserves the light NLSI theme, 900×600 minimum window, application identity, namespaces, telemetry, and local data formats.
- Updates the native application and installer metadata and isolates all build outputs under v1.4.3-beta.
- Current revision-13 telemetry contains no documented current coordinates or country value; the position card reports location as unavailable rather than guessing or retaining stale data.

## v1.4.2-beta

- Refined Dashboard as the unified driving workspace and removed the obsolete Live Drive navigation item.
- Added locally bundled Lucide SVG navigation icons with consistent NLSI-pink and selected-white colors.
- Added persisted stable NLSI job IDs for active deliveries and new job-history entries without rewriting past records.
- Added a toll/transport history view that renders only explicit recorded event fields and explains the current provider limitation.
- Added a close confirmation with bounded local-write flushing, per-user single-instance activation, and asynchronous published-release checks.
- Updated application, installer, and version-specific build metadata to v1.4.2-beta / Beta without changing the SCS SDK or TruckSim mapping versions.

## v1.4.1-alpha

- Consolidated live driving telemetry and current-job details in Dashboard and removed the separate Live Drive destination.
- Made retarder display status derive from the revision-13 uint32 level at offset 108, with stale/disconnected values shown as unavailable.
- Added stable, collision-checked `JOB-NLSI-####` IDs to newly persisted completed-job records without changing legacy or game-provided identifiers.
- Kept active mods explicitly unavailable because supported telemetry cannot verify a complete mod list.
- Updated application, Windows resource, installer, CMake project, and current release documentation metadata to v1.4.1-alpha / Alpha.

## v1.4.0-alpha

- Added asynchronous daily v2 `.nlsi` capture for changed TruckSim GPS revision-13 source samples, including the exact compressed 32 KiB map, named raw fields, source timestamps, validity, normalized values, stable record IDs, sequence numbers, and driving-session association.
- Added crash-tail preservation/recovery, 128 MiB non-destructive file rotation, size/schema validation, a durable local pending-sync queue, and an explicit Settings → Providers sync state.
- Added the revision-13 field inventory, v2 format guide, and MySQL schema design migration.
- Kept the prior v1 `.nlsi` application log, TXT logs, local history, and user data unchanged.
- Online upload is not implemented: there is no authenticated account, backend, endpoint, or server migration deployment.
- Updated native, installer, About/runtime, and release metadata to v1.4.0-alpha.

## v1.3.9-beta

- Rebranded the sidebar as NABSKI / Logistics Solutions Inc. and identified Nabski Logistics and Solutions Inc. in the About page and installer publisher metadata.
- Removed the version badge from the shared page header while preserving its current title, subtitle, date/time, and ping information.
- Updated application, installer, and release version metadata to v1.3.9-beta while retaining the version on the About page.
- Preserved the application name, Program Files installation location, `%LOCALAPPDATA%\NLSI\Exclusive Logbook` user data, telemetry, and third-party attribution.

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
