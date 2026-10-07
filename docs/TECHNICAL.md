# Technical overview

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
