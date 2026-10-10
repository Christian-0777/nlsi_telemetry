# NLSI Exclusive Logbook v1.4.8-beta

This Beta release replaces Dashboard's generic card grid with the specified
two-column job, driving telemetry, and connection layout. It preserves the
existing telemetry pipeline and local data formats.

## What's changed

- Displays the current job, supported cargo/route/income/distance/progress/ETA
  fields, and a special-job badge only while its verified flag is true and fresh.
- Shows fuel, RPM/gear, throttle, brake, cruise control, and retarder. A matching
  `A` marker appears beside throttle, brake, and retarder only while fresh
  telemetry confirms cruise control is active.
- Limits Dashboard typography to responsive 8–14 px and formats remaining ETA
  as `HH:MM:SS`; unsupported game version, vehicle, and ping values remain N/A.
- Detects registered installations by uninstall metadata and validates the
  installed root/version before upgrading. The default is
  `C:\Program Files\NLSI Exclusive Logbook`; legacy and per-user data are not
  deleted by setup.
- Aligns application and installer metadata to v1.4.8-beta without changing
  the SCS SDK or shared-memory revision.

ETS2/ATS live gameplay and an in-place upgrade against a real installation were
not validated during this build.

Expected installer target:
`build/releases/v1.4.8-beta/NLSI-Exclusive-Logbook-v1.4.8-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.7-beta

This Beta release improves ETS2/ATS game-log initialization and monitoring,
adds explicit reinitialization and evidence labels for mod detection, and
reduces text clipping in resizable views. Existing telemetry providers,
formats, branding, application identity, and per-user data locations remain
unchanged.

## What's changed

- Initializes each game's `game.log.txt` when it becomes available, parses
  existing content once, and incrementally reads appended complete lines.
- Detects truncation, replacement, rotation, timestamp resets, partial UTF-8
  lines, and uncertain session boundaries; the Active Mods page exposes an
  accessible Reinitialize button with explicit status feedback.
- Separates subscription, mounted-package, active Workshop, and active local
  evidence; groups related ProMods archives without treating subscriptions as
  proof that a mod loaded.
- Wraps long descriptions and mod details, and uses wrapping/elision/scrolling
  for tabular and path-like content without changing the 900x600 minimum.
- Keeps the ETS2/ATS game logs read-only and preserves all historical session
  data and user-data paths.

ETS2/ATS gameplay was not available for live validation. Offline fixtures cover
the parser and monitor lifecycle.

Expected installer target:
`build/releases/v1.4.7-beta/NLSI-Exclusive-Logbook-v1.4.7-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.6-beta

This Beta release reduces local telemetry persistence latency while preserving
the existing telemetry and application-log formats, user-data paths, and
provider integrations.

## What's changed

- Batches accepted telemetry records for up to 250 ms or 128 records and writes
  them in order through persistent session append handles.
- Bounds queued and in-flight telemetry to 2,048 records and 64 MiB; new
  submissions that exceed capacity are explicitly rejected rather than silently
  dropped.
- Recovers accepted batches atomically after interruption, remains compatible
  with legacy single-record recovery files, and deduplicates records by stable
  ID.
- Reduces time spent holding the telemetry state mutex by preparing samples
  after taking the required state snapshot.
- Keeps the v2 telemetry JSONL, v1 application `.nlsi`, and UTF-8 TXT formats,
  timestamp meaning, TruckSim GPS mapping, SCS SDK position integration,
  application identity, and per-user data location unchanged.

In a synthetic 60-second, 16-record/second workload, both baseline and updated
builds persisted all 960 accepted records. The updated run reduced peak queued
records from 403 to 16 and shutdown drain time from 44.27 seconds to 0.158
seconds. This rate-limited test demonstrates backlog and drain behavior, not
maximum storage throughput. ETS2/ATS gameplay and hardware/provider integration
still require manual validation.

A second 60-second run at 32 records/second persisted all 1,920 accepted
records, with 31.93 records/second end-to-end throughput, a peak queue of 22
records (about 43 KiB of queued JSON), and a 0.165-second drain.

Expected installer target:
`build/releases/v1.4.6-beta/NLSI-Exclusive-Logbook-v1.4.6-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.5-beta

This maintenance release strengthens local telemetry shutdown and recovery,
standardizes local date/time handling on the IANA `Asia/Manila` zone, and keeps
the existing application-log and telemetry schemas compatible.

## Verified changes

- Stops telemetry producers before draining accepted writes; displays shutdown
  progress and does not forcibly terminate a slow writer.
- Stores queued samples in atomic local recovery files and deduplicates them
  against telemetry records during restart recovery.
- Converts explicitly zoned timestamps to Asia/Manila for display and daily
  telemetry file grouping; stored `timestamp_utc` values remain UTC.
- Preserves the separate v1 application `.nlsi`, v2 telemetry `.nlsi`, and
  UTF-8 TXT purposes and schemas.
- Keeps the existing application identity, per-user data location, and
  installer/uninstall data policy.

ETS2/ATS gameplay and hardware/provider integration still require manual
validation. Previous release artifacts are not modified.

Expected installer target:
`build/releases/v1.4.5-beta/NLSI-Exclusive-Logbook-v1.4.5-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.4-beta

This Beta maintenance release adds current in-game world coordinates through a
dedicated position-only SCS Telemetry SDK plugin. Existing TruckSim GPS mapping,
revision-13 fields, provider behavior, installation, and job/session pipeline
remain unchanged.

## What's changed

- Builds the SCS SDK 1.15 `nlsi.dll` plugin for x64 and x86 using the documented `__stdcall` telemetry ABI and undecorated SDK exports.
- Registers only the player's `truck.world.placement` (`dplacement`) channel and publishes its XYZ world coordinates over `Local\\NLSI.SCS.Position.v1`.
- Adds a separate Qt position provider with consistent IPC decoding, explicit disconnected/stale reporting, and live coordinate display. No city/country name is fabricated; available local data does not provide a compatible world-coordinate lookup.
- Installs `nlsi.dll` only into detected ETS2/ATS architecture-specific plugin folders, backs up preexisting files, and restores/removes only unchanged installer-managed copies.
- Keeps application, plugin, installer-payload, and setup outputs under the version-specific v1.4.4-beta paths.

Live ETS2/ATS gameplay validation has not been performed. Position IPC,
stale-data handling, installer backup/restore, missing plugin, and missing game
cases are covered by offline tests.

Expected installer target:
`build/releases/v1.4.4-beta/NLSI-Exclusive-Logbook-v1.4.4-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.3-beta

This Beta release adds active Workshop mod inspection, responsive dashboard
cards, a Current Position card, and more precise telemetry event history while
preserving existing app identity, local data, and telemetry compatibility.

## What's changed

- Reflows all Dashboard cards at narrow, medium, and wide page sizes; long and unavailable values wrap consistently. The minimum window remains 900×600.
- Adds Current Position with active-job destination display and no stale location fallback. The current TruckSim GPS revision-13 map exposes no documented coordinates/country, so the location itself is shown as unavailable until a verified source exists.
- Reads only ETS2 and ATS `game.log.txt` files under the current user's Documents directory. The Active Mods page displays active Workshop IDs, available names/version/authors, source links, and Steam preview images when obtainable; missing, unreadable, changing, and old logs are reported.
- Limits trip history to `player.use.ferry`, `player.use.train`, and `player.tollgate.paid`; startup text, traffic train counts, trigger-like names, duplicate event records, and absent fees are not treated as completed trips or inferred charges.
- Preserves the existing Qt6/C++ architecture, release identity, namespaces, data paths, telemetry mapping, and v1.4.2-beta artifacts.

Steam Workshop preview thumbnails are optional; their retrieval contacts Steam
using the active Workshop ID. No game telemetry is sent. Live ETS2/ATS gameplay
and live Steam thumbnail delivery have not been tested for this release.

Expected installer target:
`build/releases/v1.4.3-beta/NLSI-Exclusive-Logbook-v1.4.3-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.2-beta

This Beta maintenance release refines the unified driving workspace and
history UI while keeping existing telemetry, event, session, job, and local
queue records in place.

## What's changed

- Consolidates driving telemetry in Dashboard and removes the Live Drive navigation destination.
- Adds a locally bundled Lucide SVG navigation icon set, consistent selected colors, and fixed two-column About/Settings rows.
- Shows stable NLSI job IDs during active deliveries and carries the same IDs into newly recorded job history.
- Adds a trip-event history view that displays only explicit recorded toll/ferry/train event values; the current revision-13 provider does not expose those events or toll amounts, so the UI reports them unavailable.
- Requires confirmation to exit, waits up to five seconds for accepted telemetry writes, and cancels exit if local writes have not drained.
- Prevents a second application process with a per-user interprocess lock and local activation message.
- Checks published GitHub releases asynchronously at startup and on demand in About, with semantic-version comparison and cached results.
- Updates current application metadata to **v1.4.2-beta / Beta** and places all release build outputs in version-specific directories.

GitHub update checks have not been live-tested, and live ETS2/ATS gameplay has
not been tested for this release. The update checker never downloads or installs
software. Existing v1.4.1-alpha release artifacts and historical records remain
unchanged.

Expected installer target:
`build/releases/v1.4.2-beta/NLSI-Exclusive-Logbook-v1.4.2-beta-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.1-alpha

This Alpha maintenance update unifies live driving, current-job details, and
navigation progress in Dashboard while preserving the existing Qt architecture,
telemetry providers, local storage format, and user-data location.

## Maintenance changes

- Moves the existing driving telemetry and control displays from Live Drive into Dashboard; removes the obsolete navigation destination and page.
- Displays retarder state from the revision-13 uint32 level at offset 108 and treats stale or disconnected readings as unavailable.
- Adds stable, collision-checked `JOB-NLSI-####` identifiers to new completed-job history records while keeping game-provided identifiers separate and leaving historical records unchanged.
- Keeps Active Mods explicitly unavailable because the supported telemetry cannot verify a complete active-mod list.
- Sets the application and release channel to **1.4.1-alpha / Alpha**.

Live ETS2/ATS gameplay validation has not been performed for this update.
Expected installer target:
`build/releases/v1.4.1-alpha/NLSI-Exclusive-Logbook-v1.4.1-alpha-Setup.exe`.

---

# NLSI Exclusive Logbook v1.4.0-alpha

This alpha adds local raw telemetry capture and a durable local queue while
preserving existing TXT logs, v1 `.nlsi` logs, history, and user data.

## What's new

- Capture each observed TruckSim GPS revision-13 sample on source timestamp changes (250 ms polling) without blocking the provider/UI thread.
- Preserve the complete 32 KiB shared-memory map in compressed form and separately record named raw source fields, availability, provider/revision metadata, source clocks, UTC timestamp, stable record ID, sequence number, optional driving-session ID, and normalized values.
- Write daily schema-v2 `.nlsi` JSON Lines files with non-destructive 128 MiB rotation and recovery sidecars for incomplete trailing records.
- Add a local pending synchronization queue that is rebuilt from telemetry records after restart and surfaced in Settings → Providers.
- Document the current decoder's verified revision-13 field inventory and add a versioned MySQL schema design migration without adding a user/account table.
- Preserve the existing v1 application `.nlsi` stream and TXT output.
- Set the executable, About/runtime version, installer, and release notes to v1.4.0-alpha.

## Explicit limitations

- No authenticated account system, HTTPS API endpoint, backend, database deployment, or remote synchronization is present. Samples remain local and Pending; no record is marked Synced.
- No live ETS2/ATS test was possible from the desktop build. Only fields decoded by the active reader are named; the complete vendor layout is not published with the binary payload. The full 32 KiB map is retained to avoid losing opaque fields.
- Disk-space exhaustion and file errors are surfaced in Settings → Providers, but cannot recover samples which the operating system refuses to write.

## Data and installer

- User data remains under `%LOCALAPPDATA%\NLSI\Exclusive Logbook`; upgrades and uninstall do not remove these files.
- Release target: `build/releases/v1.4.0-alpha/NLSI-Exclusive-Logbook-v1.4.0-alpha-Setup.exe`.
- **Validated:** native MSVC Release application and both native test executables built; Python compatibility/packaging/schema checks passed (37 tests); Release CTest passed (2/2); v1.4.0.0 / v1.4.0-alpha executable metadata verified; installer staging confirmed Qt/MSVC runtimes, Qt platform plugin, official x64/x86 TruckSim plugin payloads, MIT notices, and docs.
- **Installer:** `build/releases/v1.4.0-alpha/NLSI-Exclusive-Logbook-v1.4.0-alpha-Setup.exe` (created).
- **Not validated:** live ETS2/ATS capture, game-folder plugin installation, applying the SQL migration to MySQL, authenticated API upload, offline-to-online retry, remote idempotency, and unauthorized API behavior. No backend/API is present.

---

# NLSI Exclusive Logbook v1.3.9-beta

The v1.3.9-beta update refreshes the company branding and removes application
version badges from the shared page header.

## What's new

- Show the shared header's current page title, existing subtitle, and existing Asia/Manila date/time and ping information without a version badge on every page.
- Apply the NABSKI / Logistics Solutions Inc. sidebar identity without changing the existing logo, app name, installation folder, or user-data location.
- Identify the company as Nabski Logistics and Solutions Inc. in the About page and installer metadata.
- Update the app, executable, installer, About page, and release information to v1.3.9-beta; the About page retains the current app version.
- Preserve telemetry, jobs, history, settings, existing installation paths, user data, and third-party notices.

## Validation notes

- Build, test, and installer compilation results will be recorded after running the release checks for this version.
- Live ETS2/ATS telemetry and actual game-folder plugin installation remain runtime validations.
- The installer target is `build/releases/v1.3.9-beta/NLSI-Exclusive-Logbook-v1.3.9-beta-Setup.exe`.

---

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
- Add dynamic About version display, company community/creator links, and TruckSim GPS/SCS attribution.
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
