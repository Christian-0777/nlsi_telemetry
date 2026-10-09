# Telemetry mapping and availability

## Native Qt application v1.4.3 Beta

The native app reads the official TruckSim GPS plugin's 32 KiB `Local\TSGPSTelemetry` map (revision 13) directly; it does not use the separate TruckSim GPS Telemetry Server. TruckSim GPS is the only active native game telemetry provider. The reader normalizes game identity, pause/driving state, speed, RPM, gear, throttle/brake, retarder, cruise control, fuel/range, odometer, route distance/time, job metadata, and the map's delivery/cancellation events into the existing telemetry/session/history model.

The decoder rejects unknown map revisions. Cruise-control enabled state and set speed, and retarder status and level, are displayed only when their verified fields are available. Retarder level is decoded as a `uint32` at byte offset 108; Dashboard derives active status only from level > 0 and reports unavailable for stale or disconnected telemetry. The map does not expose a separate retarder-active boolean. Plugin presence, SDK activity, source timestamp freshness, storage and synchronization states, and shared-memory errors are shown under Settings → Providers. The official plugin binaries are MIT-licensed and packaged with the plugin and SCS license notices. The GPL-3.0 server application and its code are not redistributed. The revision-13 fields documented below contain no verified current coordinates or country, so the Current Position card does not show a guessed or cached location. The current provider does not persist toll-gate, ferry, or train events or verified fees; the History view now accepts only exact `player.tollgate.paid`, `player.use.ferry`, and `player.use.train` event records from sources that provide them. Live ETS2/ATS telemetry is NOT TESTED for v1.4.3-beta.

### Revision-13 raw field inventory for the active native decoder

The source of truth for the named offsets currently consumed is the native
revision-13 decoder in `native/providers/TruckSimGpsProvider.cpp`, paired with
the official TruckSim GPS plugin x64/x86 payload at
`TruckSim-GPS/trucksim-gps-plugin` commit
`ab79d819229740978bb22fb338a2b12bf5f623bf`. The plugin's complete named
revision-13 structure definition is not present in the upstream binary payload
or this checkout. Therefore this inventory is explicitly every named field
NLSI can verify/read today, not a claim that the private map has no other
fields. All 32 KiB are retained in each v2 raw record so opaque or future fields
are not discarded or guessed.

Raw map values are captured before unit conversion/display formatting. “Present”
means the field slot exists in the validated revision-13 map. Semantic
availability is false where the decoder cannot safely interpret the value;
raw bytes remain preserved. Timestamp columns in this inventory are plugin
clock counters, not UTC wall-clock timestamps. Every sample also gets a local
UTC capture timestamp. This map has no documented latitude/longitude, GPS
coordinates, account identity, game version, or authenticated-user session.

| Source field (offset) | Source type | Source unit / meaning | Normalized application field | Validity / nullability | Timestamp behavior | Captured before v1.4.0 | v1.4.0 capture |
|---|---|---|---|---|---|---|---|
| SDK active (0) | bool | SDK/plugin active flag | `session_active` | Slot present; true/false | Source sample timestamp | Yes, bool | Raw bool + validity |
| Paused (4) | bool | Game pause flag | `paused`; `driving` is derived | Slot present; true/false | Source sample timestamp | Yes, bool | Raw bool + validity |
| Sample timestamp (8) | uint64 | Plugin-defined counter; unit not established here | Provider freshness metadata | Zero means no valid sample timestamp | Source counter, serialized as decimal text | Diagnostics only | Raw timestamp + validity |
| Simulation timestamp (16) | uint64 | Plugin-defined simulation counter; unit not established here | None | Slot present; zero retained | Same source sample | Diagnostics only | Raw timestamp |
| Render timestamp (24) | uint64 | Plugin-defined render counter; unit not established here | None | Slot present; zero retained | Same source sample | Diagnostics only | Raw timestamp |
| Plugin revision (40) | uint32 | Revision number | Provider metadata | Must equal 13 to decode | Static per map revision | Yes, status only | Raw revision |
| Game (52) | uint32 enum | 1=ETS2, 2=ATS | `game_id`, `game_name` | Other values unavailable | Source sample timestamp | Yes, mapped strings | Raw enum + mapped values |
| Planned distance (100) | uint32 | km (as used by current decoder/history) | `planned_distance` | Zero unavailable in normalized model | Source sample timestamp | Yes, decimal string if >0 | Raw integer + validity |
| Retarder level (108) | uint32 | Discrete level | `retarder_level`; active is derived as level > 0 | Slot present | Source sample timestamp | Yes | Raw integer + derived bool |
| Selected gear (504) | int32 | Gear value | `gear` | Slot present | Source sample timestamp | Yes | Raw integer |
| Speed (948) | float32 | m/s | `speed_kmh` (×3.6); `driving` is derived | Finite and >=0 | Source sample timestamp | Yes, converted only | Raw m/s and normalized km/h |
| Engine RPM (952) | float32 | rpm | `rpm` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Input throttle (960) | float32 | Source fraction/value; range not clamped | `input_throttle` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Input brake (964) | float32 | Source fraction/value; range not clamped | `input_brake` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Effective throttle (976) | float32 | Source fraction/value; range not clamped | `effective_throttle` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Effective brake (980) | float32 | Source fraction/value; range not clamped | `effective_brake` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Cruise-control speed (988) | float32 | m/s | `cruise_control_speed` (×3.6) | Finite and >=0 | Source sample timestamp | Yes, converted only | Raw m/s and normalized km/h |
| Fuel (1000) | float32 | liters | `fuel_liters` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Fuel range (1008) | float32 | km | `fuel_range_km` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Odometer (1056) | float32 | km | `odometer_km` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Navigation distance (1060) | float32 | m | `navigation_distance_m`; km is derived | Finite and >=0 | Source sample timestamp | Yes, m and derived km | Raw m, normalized m/km |
| Navigation time (1064) | float32 | seconds | `navigation_time_s` | Finite and >=0 | Source sample timestamp | Yes | Raw and normalized |
| Job loaded (1564) | bool | Loaded flag | `loaded` | Slot present | Source sample timestamp | Yes | Raw bool + validity |
| Special job (1565) | bool | Special-job flag | `special_job` | Slot present | Source sample timestamp | Yes | Raw bool + validity |
| Cargo ID (2556, 64 bytes) | UTF-8 NUL-terminated bytes | Identifier text | `cargo_id` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Cargo name (2620, 64 bytes) | UTF-8 NUL-terminated bytes | Name text | `cargo_name` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Destination city (2748, 64 bytes) | UTF-8 NUL-terminated bytes | City text | `destination_city` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Destination company (2876, 64 bytes) | UTF-8 NUL-terminated bytes | Company text | `destination_company` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Source city (3004, 64 bytes) | UTF-8 NUL-terminated bytes | City text | `source_city` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Source company (3132, 64 bytes) | UTF-8 NUL-terminated bytes | Company text | `source_company` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Job market (3404, 32 bytes) | UTF-8 NUL-terminated bytes | Market text | `market` | Non-empty, valid UTF-8 | Source sample timestamp | Yes, decoded text only | Raw decoded text + availability + map bytes |
| Income (4000) | uint64 | Integer game amount; currency semantics not asserted | `income` | Zero unavailable in normalized model | Source sample timestamp | Yes, decimal string if >0 | Raw decimal integer + validity |
| On job (4300) | bool | Active-job flag | `has_job` | Slot present | Source sample timestamp | Yes | Raw bool + validity |
| Job cancelled (4302) | bool | Terminal event flag | `job.cancelled` event on rising edge | Slot present; persisted event only when active SDK and baseline exists | Event timestamp is NLSI UTC capture time | Event only | Raw bool on every changed sample and event |
| Job delivered (4303) | bool | Terminal event flag | `job.delivered` event on rising edge | Slot present; persisted event only when active SDK and baseline exists | Event timestamp is NLSI UTC capture time | Event only | Raw bool on every changed sample and event |

Application-derived fields (not separate source fields) include driving state,
retarder-active state, navigation distance in km, and ETA. `delivery_time` has
no revision-13 source offset in the active decoder and remains unavailable.
Previous `.nlsi` application logs did not contain telemetry samples; v1.4.0
does not change their schema. The new v2 daily files are additive.

## Database migration and field mapping

Only after recording the above inventory, the versioned MySQL 8 schema design
was added at `db/migrations/001_telemetry_sync.sql`. It defines driving
sessions, telemetry samples, job records, and durable synchronization state;
it intentionally defines no users/accounts table because this application
contains no authenticated account system. `source_fields`,
`source_availability`, and `normalized_fields` are separate queryable JSON
columns; exact mapping bytes are stored as a binary payload. The v2 local
`record_id`, session ID, sequence, UTC timestamp, provider/revision, fields,
and validity map correspond to the similarly named columns. Job records map
from local job event IDs/details; session/game/start/end fields map from
`sessions.jsonl`.

The migration is a design artifact only: there is no server, authentication
provider, database deployment, API endpoint, migration runner, or remote
upload implementation in this repository. Apply it only when a backend and
its authorization/data-retention policy exist. See
`docs/SQL-SCHEMA.md` for column mapping, migration checks, and rollback
instructions. Until then records remain local and queue state is Pending.

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
