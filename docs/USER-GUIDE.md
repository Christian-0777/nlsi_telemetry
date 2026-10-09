# NLSI Exclusive Logbook user guide

## Installation and launch

The native v1.3.9-beta application is built for Windows x64 with Qt 6.12 and MSVC 2022. It opens at the existing 900×600 minimum window size and can start without a game; live values remain unavailable until TruckSim GPS telemetry is available. The retained Python implementation is a separate legacy reference and is not bundled in the native application.

## Navigation

- **Dashboard** — overall connection status, vehicle values, current job, route progress, and session state.
- **Live Drive** — current vehicle, navigation, and session details.
- **Jobs** — current job identity and progress, plus the completed-jobs view.
- **History** — session/trip and completed-job history views.
- **Events** — application and telemetry event view.
- **Settings** — Application, Providers, Telemetry, and Active Mods subsections. TruckSim GPS mapping and connection diagnostics are under **Providers**.
- **About** — company identity, current application version, clickable community and creator links, and TruckSim GPS/SCS attribution and license notices.

The native application refreshes Qt controls on a timer from normalized state; the telemetry worker does not directly modify widgets. Missing or stale values are not treated as live values. Job metadata and progress are presented separately. The shared top header is the single source of page title and description.

TruckSim GPS is the only active game telemetry provider. Settings → Providers shows its shared-memory mapping name, mapping/view status, supported layout, failure stage, Win32 error, last successful read, plugin timestamps, and data age. The reader accepts revision 13 of the official 32 KiB `Local\TSGPSTelemetry` map; unsupported revisions remain unavailable. Cruise control and retarder status/levels are shown only when their verified telemetry fields are available. Cruise set speed is displayed in km/h.

Events, History, and the completed-jobs views show locally persisted provider events, telemetry-driven sessions, and delivered/cancelled jobs. Empty views indicate that no matching records have been recorded; storage errors are displayed rather than replaced with fabricated data. Job completion is recorded only when a provider explicitly reports delivery or cancellation. Active mod detection is not available from the supported telemetry interface.

## Time and versions

The shared page header displays Asia/Manila date and live time to millisecond precision. Ping is shown as N/A because the supported local shared-memory telemetry does not provide a measured ping. Navigation ETA is calculated only when both navigation distance and positive vehicle speed are available. The current plugin does not send game simulation time, so game time, in-game elapsed time, and game ETA remain unavailable.

Dates use `MM/DD/YY`, decimal values use thousands separators and two decimal places, and integer values use thousands separators. Completed jobs can be exported to a paginated PDF; exports retain available earnings, route, dates, and recorded statistics without changing job history. TXT logs continue alongside versioned `.nlsi` JSON Lines logs. See [NLSI log format](NLSI-LOG-FORMAT.md).

The current application version, **v1.3.9-beta**, is shown on the About page and comes from the build's version metadata. The SCS telemetry API version is separate and shown only when supplied by telemetry; the GUI does not substitute the application version.

## Configuration and privacy

Telemetry, event/session/job history, TXT logs, and `.nlsi` logs are handled locally under `%LOCALAPPDATA%\NLSI\Exclusive Logbook`. The application does not send gameplay telemetry to a website or cloud service. The updater may contact GitHub to check for releases.

## Troubleshooting

### Telemetry says disconnected

Confirm the TruckSim GPS telemetry plugin is installed in the game's matching architecture plugin folder and that the game is running. Setup attempts this automatically for detected Steam game folders and records installation results under the application directory's `logs\trucksim-plugin-status.txt`. Check Settings → Providers for the exact shared-memory mapping, revision, freshness result, and Windows error. A missing map can mean that the plugin is not installed or the game/plugin has not initialized; unsupported revisions are rejected rather than decoded with guessed offsets. Live ETS2/ATS behavior has not been tested for v1.3.9-beta.

### Known limitations

The installer detects Steam libraries; other game installation locations may require manual plugin installation. TruckSim GPS layouts other than revision 13 are rejected rather than decoded using guessed offsets. The TruckSim GPS Server GUI is not included or required. Live ETS2/ATS validation remains dependent on the installed game and plugin versions.
