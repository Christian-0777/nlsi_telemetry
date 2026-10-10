┌────────────────────────────────────────────────────────────────────────┐
│ JOB ID: NLSI-XXXXXXXX                           STATUS: DELIVERED       │
├───────────────────────────────────┬────────────────────────────────────┤
│ CARGO                             │ WEIGHT                             │
│ [Cargo name]                      │ [Cargo weight]                     │
│                                   │                                    │
│ FROM                              │ TO                                 │
│ [Source city]                     │ [Destination city]                 │
│                                   │                                    │
│ FROM COMPANY                      │ TO COMPANY                         │
│ [Pickup company]                  │ [Delivery company]                 │
│                                   │                                    │
│ PLANNED DISTANCE                  │ DRIVEN DISTANCE                    │
│ [Planned distance]                │ [Actual distance driven]           │
│                                   │                                    │
│ INCOME                            │ OFFENCES                           │
│ [Job income]                      │ [Recorded offences]                │
│                                   │                                    │
│ XP                                │ DAMAGE                             │
│ [Experience earned]               │ [Recorded damage]                  │
│                                   │                                    │
│ TIME TAKEN (REAL)                 │ MAX SPEED                          │
│ [HH:MM:SS]                        │ [Maximum recorded speed]           │
├───────────────────────────────────┴────────────────────────────────────┤
│ VEHICLE AND FUEL                                                        │
├───────────────────────────────────┬────────────────────────────────────┤
│ TRUCK USED                        │ TRAILER USED                       │
│ [Truck make/model]                │ [Trailer details]                  │
│                                   │                                    │
│ TRUCK LICENCE PLATE               │ TRAILER LICENCE PLATE              │
│ [Truck plate]                     │ [Trailer plate]                   │
│                                   │                                    │
│ FUEL USAGE                        │ REFUELED                           │
│ [Fuel consumed]                   │ [Fuel added]                       │
│                                   │                                    │
│ REFUEL COST                       │ AVG CONSUMPTION                    │
│ [Total refueling cost]            │ [Average fuel consumption]         │
└───────────────────────────────────┴────────────────────────────────────┘

## Completed-job data sources and historical limits

The completed-job card is populated from the persisted terminal-job record.
The authoritative capture point is the provider's `job.delivered` or
`job.cancelled` event; final-job fields from the `JobSnapshot` are persisted only when its cargo
ID matches the terminal event and the fields are available and not stale.
Values missing from a historical record are
shown as `N/A`. No current Dashboard telemetry is used to backfill a job.

| Card field | Authoritative source and availability | Persisted/model coverage and missing-data reason |
|---|---|---|
| NLSI Job ID | App-generated when the terminal event is recorded | Persisted in the job record and stable ID map; existing IDs are retained |
| Game Job ID | `job_id` / `job.id` in terminal event data | Persisted with event data; `N/A` when the provider did not include it |
| Status | Terminal event type: `job.delivered` or `job.cancelled` | Persisted as `Delivered` or `Cancelled`; other statuses are not listed as completed |
| Recorded at | Timestamp on the terminal event | Persisted with the job record |
| Cargo | Final `JobSnapshot.cargo`, otherwise terminal event cargo | Persisted in the job record and details when captured; `N/A` if neither source supplied it |
| Weight | Explicit `weight` / `cargo_weight` event data | Event details are retained, but there is no weight in `JobSnapshot`; `N/A` if absent |
| From city | Final `JobSnapshot.source_city`, otherwise terminal event `source_city` / `source.city` | Now persisted from the final snapshot when available; older combined route text cannot reliably be split into city/company |
| To city | Final `JobSnapshot.destination_city`, otherwise terminal event `destination_city` / `destination.city` | Now persisted from the final snapshot when available; older combined route text cannot reliably be split into city/company |
| From company | Final `JobSnapshot.source_company`, otherwise terminal event `source_company` / `source.company` | Now persisted from the final snapshot when available; older combined route text cannot reliably be split into city/company |
| To company | Final `JobSnapshot.destination_company`, otherwise terminal event `destination_company` / `destination.company` | Now persisted from the final snapshot when available; older combined route text cannot reliably be split into city/company |
| Planned distance | Final `JobSnapshot.planned_distance`, otherwise terminal event `planned_distance_km` / `planned_distance` | Terminal event details retain this provider value; the final snapshot is now persisted as a fallback |
| Driven distance | Explicit terminal event driven-distance field | No driven-distance field exists in `JobSnapshot`; `odometer_km` is not a job distance and is not substituted |
| Income | Final `JobSnapshot.income`, otherwise terminal event `income` | Terminal event details retain it; final snapshot value is now persisted as a fallback |
| Offences | Explicit terminal event `offences` / `offenses` | Not modeled by the app; `N/A` if not present in terminal event data |
| XP | Explicit terminal event `xp` / `experience` | Not modeled by the app; `N/A` if not present in terminal event data |
| Damage | Explicit terminal event `damage` / `damage_percent` | Not modeled by the app; `N/A` if not present in terminal event data |
| Time taken (real) | Explicit terminal event `real_elapsed_time` / `elapsed_time` | Not modeled or derived from session duration; `N/A` if not present in terminal event data |
| Max speed | Explicit terminal event `max_speed_kmh` / `maximum_speed_kmh` | Not tracked per job; current speed is not substituted; `N/A` if absent |
| Truck used | Explicit terminal event truck/vehicle name | Not in `JobSnapshot` or the terminal event currently emitted; `N/A` |
| Trailer used | Explicit terminal event trailer details | Not in `JobSnapshot` or the terminal event currently emitted; `N/A` |
| Truck licence plate | Explicit terminal event truck plate | Not in `JobSnapshot` or the terminal event currently emitted; `N/A` |
| Trailer licence plate | Explicit terminal event trailer plate | Not in `JobSnapshot` or the terminal event currently emitted; `N/A` |
| Fuel usage | Explicit per-job consumed-fuel event field | Not tracked per job; the current fuel gauge is not fuel consumed; `N/A` if absent |
| Refueled | Explicit per-job refueled-fuel event field | Not tracked per job; `N/A` if absent |
| Refuel cost | Explicit per-job refueling cost event field | Not tracked per job; `N/A` if absent |
| Average consumption | Explicit per-job average-consumption event field | Not tracked per job; `N/A` if absent |

The current TruckSim GPS terminal event provides cargo, route, income, planned
distance, and current odometer/navigation/fuel readings. It does not provide
weight, actual job-driven distance, offences, XP, damage, real elapsed job
time, maximum speed, vehicle/trailer names or plates, or per-job fuel
consumption/refueling/cost/average. Current odometer, navigation, and fuel
readings are deliberately not represented as those historical job fields.