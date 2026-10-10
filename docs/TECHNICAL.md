# Technical overview

## Native application v1.4.9 Beta

The native Qt application keeps the existing TruckSim GPS shared-memory reader as its game telemetry provider. It reads `Local\TSGPSTelemetry` directly using the verified official 32 KiB plugin revision-13 layout and normalizes values into the existing telemetry, event, job, and session pipeline. On observed plugin timestamp changes, a separate background writer appends complete raw maps and named raw/normalized fields to daily v2 `.nlsi` files and a durable local Pending queue. A separate SCS position provider reads only `Local\NLSI.SCS.Position.v1`; it does not share, reinterpret, or write the TruckSim mapping or its records.

The SCS SDK 1.15 `nlsi.dll` plugin registers only `truck.world.placement` as `dplacement` and exports the documented telemetry initialization/shutdown entry points using the SDK calling convention. It publishes XYZ world coordinates to the dedicated version-1 interface. Current position remains available on the existing detail/history surfaces but is not part of the replacement Dashboard. No city or country lookup is bundled because compatible local map data is not available. Existing v1 application `.nlsi`, TXT, event, job, and session history streams remain intact. The NLSI UDP listener and RenCloud provider are not started or included in the native application. The separate GPL-3.0 TruckSim GPS Telemetry Server is not bundled, linked, launched, or required.

The installer independently places the MIT-licensed official TruckSim GPS plugin and the NLSI `nlsi.dll` position plugin into architecture-compatible directories for detected Steam ETS2/ATS installations. Existing `nlsi.dll` files are backed up before replacement; uninstall restores backups or removes only unchanged managed copies. The app reports missing maps, unsupported plugin revisions, inactive SDK state, and samples that stop changing; stale data is not presented as connected. Plugin delivery/cancellation events flow into the existing local event, completed-job, session, TXT-log, and `.nlsi` log storage. Completed jobs can be exported to PDF without changing source records.

The revision-13 decoder, position IPC decoder/staleness checks, installer backup/restore, raw record validation/recovery, local queue restart recovery, formatter, v1 log reader/writer, PDF export, and Windows mapping lifecycle are covered by offline tests. Dashboard retains its current-job logic and exposes only game/configuration fields in Game Config; version, vehicle, and trailer data not provided by the native telemetry model remain unavailable rather than inferred. The Manila wall clock uses the existing one-second timer. Completed Jobs renders only persisted delivered/cancelled terminal records, with per-job PDF exports; newly persisted terminal events queue one desktop notification per deduplicated job event and do not notify on history load. The underlying terminal-event payload currently provides only job, cargo, route, income, planned distance, market, special-job state, odometer, remaining navigation distance, and fuel at event time; other card/report fields are explicitly unavailable. Active Mods reads the two game `game.log.txt` files under the current user's Documents directory, parses an existing session once, then reads appended lines incrementally; it distinguishes subscription, mounted-package, active Workshop, and active local evidence. When a session boundary cannot be established, entries are reported as uncertain rather than confirmed current-session mods. It does not inspect settings or scan processes. Optional preview requests use the corresponding Steam Workshop page and an HTTPS Steam image host. Trip history accepts only the three exact supported player events and records no fee unless a fee was explicitly persisted. The application checks GitHub Releases metadata asynchronously and does not download or install updates. Single-instance enforcement uses a per-user lock plus a local activation socket. Live game telemetry, live GitHub checks, live Steam previews, and installation into actual game folders have not been validated for this release. The separate legacy UDP SCS SDK plugin build remains documented below and is not the native position plugin. There is no authenticated account, gameplay web API, remote database, or telemetry upload implementation; the migration under `db/migrations` is a server-side design only.

## Architecture

NLSI Telemetry has three local layers:

1. A Windows x64 native plugin built against the official SCS Telemetry SDK 1.15. ETS2 or ATS loads it through the SCS telemetry plugin ABI.
2. The Python telemetry agent that listens for the plugin's UDP packets and owns session/event processing.
3. A Tkinter GUI that displays agent snapshots and persisted event history.

The plugin implements the SCS telemetry ABI contract 1.01 and identifies ETS2/ATS from SCS game metadata. SDK release version, ABI version, and each game's telemetry API version are distinct. The SDK 1.15 headers describe ETS2 telemetry API 1.20 and ATS API 1.07; the game-reported API version is provided at runtime.

## Telemetry flow

The plugin registers SDK callbacks for telemetry channels, configuration changes, gameplay events, pause/resume, and frame lifecycle events. It sends JSON datagrams to `127.0.0.1:28745`. Continuous channel samples are limited to one packet per 250 ms. A lightweight plugin heartbeat is sent at most once per second so that the agent can notice the game/plugin even while telemetry is paused.

UDP is local and best-effort. Packets are not queued if the agent is not listening. There is no website/API upload, cloud storage, authentication, or remote listener in this project.

## Telemetry values

The native plugin reports speed (m/s and derived km/h), RPM, selected gear, steering, throttle, brake, fuel, fuel range, average fuel consumption, fuel warning, odometer, navigation distance/time, engine/transmission/cabin/chassis/wheel wear, AdBlue warning, and truck world placement/orientation. Values not supplied by the game are encoded as JSON `null`.

SCS configuration callbacks are forwarded as named attributes for truck, trailer, and job-like configurations. Gameplay callbacks are forwarded with their event IDs and provided attributes. Fields vary with game version and state; the plugin does not synthesize missing SDK values.

## Agent model and records

`agent.py` validates received JSON objects and supports `plugin_init`, `plugin_heartbeat`, `plugin_shutdown`, lifecycle, configuration, gameplay-event, and telemetry messages. `gui_app.py` runs the UDP receiver and agent on a worker thread. Immutable-by-copy snapshots are sent to the Tk main thread using a queue; widgets are updated only through Tkinter's `after()` loop. The GUI does not process SDK telemetry itself.

Gameplay and session records are appended as JSON Lines to `test\output\events.jsonl` relative to the running agent. In the default installer layout this is `C:\nlsi-tem\app\test\output\events.jsonl`. Events is the source of truth for the GUI's persisted session/event views and for the job history derived from job-start, delivery, and cancellation records. The record types include:

- `SESSION_STARTED`, `SESSION_HEARTBEAT`, and `SESSION_ENDED`
- `DRIVING_PAUSED` and `DRIVING_RESUMED`
- SDK gameplay events such as job delivery, cancellation, fines, tolls, or ferry/train use when reported
- `JOB_STARTED`, derived only after a known empty-to-active job configuration transition

Session duration and driving time use a monotonic clock. Distance is estimated from nondecreasing odometer samples; negative reset/wrap deltas are ignored. Job count increments on delivered job events. These are agent-derived metrics, not universal authoritative counters from the SDK. Missing IDs and values are not synthesized by the GUI.

PDF output is generated only by an explicit export command from the GUI. The user chooses the destination using the Windows save-file dialog. Delivery events do not write PDF files.

## Limitations

- Live gameplay behavior has not been validated across all ETS2/ATS versions.
- UDP is best-effort and loopback-only; missing packets are not recovered.
- SDK/game states may omit channels or configuration fields.
- The current plugin does not send game simulation time or reliable active-mod data.
- There is no dedicated universal job-start, refuel, or session-distance SDK event/counter in this implementation.
- The installer only auto-detects Steam libraries; non-Steam game locations may need manual plugin copying.
- Remote API integration, authentication, durable network retries, and Driver Portal integration are not implemented.

The detailed original technical proof-of-concept notes are preserved in [POC-README.md](POC-README.md).
