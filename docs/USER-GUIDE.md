# NLSI Exclusive Logbook user guide

## Installation and launch

The native v1.3.2 Alpha application is built for Windows x64 with Qt 6.12 and MSVC 2022. It opens at the existing 900×600 minimum window size and can start without a game; live values remain unavailable until the SCS plugin sends telemetry. The retained Python implementation is a separate reference/fallback and has its own legacy UI.

## Navigation

- **Dashboard** — overall connection status, vehicle values, current job, route progress, and session state.
- **Live Drive** — current vehicle, navigation, and session details.
- **Jobs** — current job identity and progress, plus the completed-jobs view.
- **History** — session/trip and completed-job history views.
- **Events** — application and telemetry event view.
- **Settings** — Application, Providers, Telemetry, and Active Mods subsections. Detailed NLSI and RenCloud provider information is under **Providers**.
- **About** — product and application version information.

The native application refreshes Qt controls on a timer from normalized state; the telemetry worker does not directly modify widgets. Missing or stale values are not treated as live values. Job metadata and progress are presented separately.

The native C++ build currently does not persist completed-job, session-history, or event-history records, and it does not ingest RenCloud telemetry. These views report unavailable data rather than displaying invented records or provider connectivity. Active mod detection is not available from the supported telemetry interface.

## Time and versions

Real time uses the system clock formatted for UTC+08:00 Asia/Manila. Navigation ETA is calculated only when both navigation distance and positive vehicle speed are available. The current SCS plugin does not send game simulation time, so game time, in-game elapsed time, and game ETA are not fabricated and remain unavailable.

The native NLSI application version is **1.3.2 Alpha**. The SCS telemetry API version is separate and shown only when supplied by telemetry; the GUI does not substitute the application version.

## Configuration and privacy

The native application uses local telemetry and does not send it to a website or cloud service. Configuration options are limited to those exposed by the existing native application.

## Troubleshooting

### Telemetry says disconnected

Confirm that the game loaded the SCS telemetry plugin. The native UI indicates provider state and does not claim a RenCloud fallback while native RenCloud ingestion is unavailable.

### Known limitations

The plugin determines which telemetry values are available. The native C++ RenCloud provider is not implemented and native historical persistence is not available. Live ETS2/ATS validation remains dependent on the installed game and plugin versions.
