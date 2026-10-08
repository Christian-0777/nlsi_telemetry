# Telemetry mapping and availability

This project relies on the official SCS Telemetry SDK 1.15 as the source of truth. The native plugin forwards telemetry to the local Python agent over UDP, the agent normalizes the packet into a stable Python structure, and the Tkinter GUI renders only values already known to be valid at runtime.

The mapping below is intentionally strict: values are labeled as `DIRECT`, `DERIVED`, `EVENT-DERIVED`, `UNAVAILABLE`, or `NOT RELIABLY EXPOSED`.

## Core mapping table

| Displayed field | Official SCS source | NLSI mapping | Transport | Python normalization | GUI status |
|---|---|---|---|---|---|
| Connection state | SDK lifecycle events, plugin init/shutdown, packet heartbeat | Plugin and UDP listener state, update timeouts | UDP packets + agent heartbeat | `TelemetryAgent.process_message()` plus timeout checks | DIRECT |
| Session ID | Agent-generated unique ID assigned on valid gameplay session start | Local session tracking in `TelemetryAgent` | Local agent state + JSONL event record | `session_id` created from UTC timestamp and persisted event history | DIRECT (local session tracking) |
| Game name/version | `game.id`, `game.name`, `game.version` | Passed through from plugin payload | UDP telemetry packet | `self.game` and `game['version']` | DIRECT |
| Real time (UTC + Asia/Manila) | OS wall-clock time | Not from SDK; formatted via Python timezone-aware `datetime` | Local UI rendering only | `format_utc_and_manila_time()` | DIRECT |
| Truck brand/model | Truck configuration block in plugin packet | `configurations.truck.brand`, `configurations.truck.name` | UDP telemetry packet | `configured_value()` and UI formatting | DIRECT |
| Truck plate | Truck configuration block in plugin packet | `configurations.truck.license_plate` | UDP telemetry packet | `configured_value()` | DIRECT |
| Speed | Vehicle speed from telemetry packet | `truck.speed_kmh` etc. | UDP telemetry packet | `TelemetryAgent.process_message()` + display formatting | DIRECT |
| RPM | Engine RPM from packet | `truck.rpm` | UDP telemetry packet | normalized through the packet model | DIRECT |
| Gear | Current gear from packet | `truck.gear` | UDP telemetry packet | converted to `N`, `6A`, etc. | DIRECT |
| Steering | Steering input / wheel angle | `truck.steering` | UDP telemetry packet | percentage formatting in the dashboard | DIRECT |
| Throttle | Throttle/pedal input | `truck.throttle` | UDP telemetry packet | scaled to percent for display | DIRECT |
| Brake | Brake input | `truck.brake` | UDP telemetry packet | scaled to percent for display | DIRECT |
| Retarder | Retarder state/level if exposed | `truck.retarder` / `retarder_level` | UDP telemetry packet | `display_retarder_value()` | DIRECT when SDK exposes value; otherwise `N/A` |
| Cruise control | SDK cruise speed or active state | `truck.cruise_control` and active flags | UDP telemetry packet | `display_cruise_value()` | DIRECT when exposed by SDK; otherwise `N/A` |
| Adaptive cruise | SDK ACC value/state, if exposed | `truck.adaptive_cruise` or `acc_*` | UDP telemetry packet | `display_adaptive_cruise_value()` | NOT RELIABLY EXPOSED |
| Fuel level | Truck fuel quantity from packet | `truck.fuel_liters` | UDP telemetry packet | normalized to liters and capacity display | DIRECT |
| Fuel capacity | Truck configuration | `configurations.truck.fuel_capacity` | UDP telemetry packet | `configured_value()` | DIRECT |
| Fuel range | Derived from current fuel and vehicle model state when available | `truck.fuel_range` or downstream derived state if present | UDP telemetry packet | UI formatting only | DERIVED |
| Odometer | Truck odometer in the packet | `truck.odometer_km` | UDP telemetry packet | `display_number()` formatting | DIRECT |
| Navigation distance | Navigation packet data | `truck.navigation_distance_m` | UDP telemetry packet | converted to kilometers for display | DIRECT |
| Navigation time | Navigation packet data | `truck.navigation_time_s` | UDP telemetry packet | converted to `HH:MM:SS` display | DIRECT |
| Route ETA | Navigation + vehicle state | derived from distance/time/speed when valid | UDP telemetry packet | real-time ETA calculation in UI | DERIVED |
| Game time / in-game elapsed / in-game ETA | Not currently exposed by the packet model in use | intentionally omitted | N/A | explicitly left unavailable | UNAVAILABLE |
| Job start / completion | Job configuration and gameplay events | `JOB_STARTED`, `job.delivered`, `job.cancelled` matches | UDP packets + JSONL persistence | session-aware event matching | EVENT-DERIVED |
| Cargo | Job configuration payload | `job.cargo` or `cargo_name` | UDP telemetry packet + job events | `job_snapshot()` merges job config + persisted events | DIRECT when present |
| Source / destination | Job configuration payload | `job.source`, `job.destination` | UDP telemetry packet + job events | persisted event snapshot logic | DIRECT when present |
| Job ID | Telemetry job id or NLSI local session identifier | agent-managed job correlation and fallback ID | UDP packet + event records | `job_id` / `NLSI ID` labeling only when supported | DIRECT if SDK exposes a real job ID; otherwise `NLSI ID: ...` |
| Job type | Job metadata if SDK exposes it | `job.job_type` or `type` | UDP telemetry packet | UI formatting | DIRECT when available; otherwise `N/A` |
| Deadline | Job deadline/time if exposed | `job.delivery_time` / `delivery_deadline` | UDP telemetry packet | displayed as timestamp only if valid | DIRECT when a trustworthy deadline exists; otherwise `N/A` |
| Trailer slots | Trailer configuration and connection state | `trailer`, `trailer.N`, `connected` flags | UDP telemetry packet | `display_trailer_value()` filters configured slots down to attached trailers | DIRECT for attached trailers only |
| Active mods | Official telemetry interface does not provide dependable active-mod enumeration | not surfaced as a reliable game state | N/A | UI intentionally warns the value is unavailable | NOT RELIABLY EXPOSED |
| Event history | Existing persisted JSONL records | `events.jsonl` + `jobs_from_events()` | local file | reconstructed from event stream | EVENT-DERIVED |

## Important production rules

- The SCS SDK remains the source of truth. The project does not attempt memory scanning, DLL injection, OCR, process hacking, or any other unsupported method.
- Real time and game time are intentionally separate concepts. Game time remains unavailable unless the official telemetry interface exposes it reliably.
- A field that is absent or not exposed by the active packet model must be rendered as `N/A`, `--`, or a clear unavailable label rather than inferred or guessed.
- JSONL event records are authoritative for finished job reconstruction. If a value was not recorded, the GUI keeps it unavailable instead of fabricating it.
- The application version and the SCS telemetry API version are intentionally separate values.

## Current SDK limitation status

The current packet model used in this project does not provide reliable in-game simulation time, game elapsed time, or in-game ETA; those values stay unavailable and are not derived from the wall-clock time. Active mod information is also not reliably exposed by the official SDK interface and is displayed as a clear limitation rather than a fabricated value.
