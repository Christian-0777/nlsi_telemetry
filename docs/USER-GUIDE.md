# NLSI Telemetry user guide

## Installation and launch

Install the v0.3.2 package from the [GitHub Releases page](https://github.com/Christian-0777/nlsi_telemetry/releases). Close ETS2 and ATS before installing or updating. Start **NLSI Telemetry Agent** from its Desktop or Start Menu shortcut before opening the game. The GUI can also start without a game; live values remain unavailable until the SCS plugin sends packets.

The launcher uses a bundled `runtime\pythonw.exe` when available, otherwise `pyw -3`. If the window does not open, launch `py -3 agent.py` from the installation's `app` folder to see startup errors. Python 3.10 or newer and the Windows Python Launcher are required when no runtime is bundled.

## Tabs

- **Main** — live game/plugin connection, session, truck, job, and real-time information. Missing values display as `--`. Position is retained in telemetry internally but is not shown here.
- **Finished Jobs** — job records derived from persisted job-start, delivery, and cancellation events. Filter by All, Delivered, Pending, or Cancelled and select a row to inspect its recorded details. A field absent from the source event is not inferred.
- **PDF Export** — choose a delivered job, click **Export PDF**, then select a destination in the Windows save dialog. No report is generated automatically when a job ends.
- **Debug** — application/API versions, connection and packet diagnostics, last errors, and a redacted view of the latest telemetry packet. `.env` secrets are not included.
- **Events** — persisted session and gameplay records from the JSONL event log.
- **Active Mods** — mod detection is unavailable through the official telemetry interface used by this application; no unsupported detection is attempted.
- **About Us** — Nabski Logistics and Solutions Inc., NLSI, Kamote Hauling, version, and any configured social links.

## Time and versions

Real time uses the system clock formatted for UTC+08:00 Asia/Manila. Navigation ETA is calculated only when both navigation distance and positive vehicle speed are available. The current SCS plugin does not send game simulation time, so game time, in-game elapsed time, and game ETA are not fabricated and remain unavailable.

The NLSI application version is **0.3.2**. The SCS telemetry API version is a separate value reported by telemetry (currently 1.01); the GUI does not substitute the application version.

## Configuration and privacy

Optional social links are read from `.env` (the source tree root during development or the installed `config\.env`). Copy `.env.example` as a starting point. Only configured HTTP(S) social/website URLs are shown in About Us. API keys, tokens, and passwords are not displayed or sent anywhere. Telemetry and event history remain local.

## Troubleshooting

### The GUI does not start

Run `py -3 agent.py` from the installed `app` directory to view errors. Confirm Python 3.10+ and the Windows Python Launcher are installed. The app requires Tkinter, which is included with the standard Windows Python distribution.

### Telemetry says disconnected

Confirm the game was started after plugin installation and that `nlsi_telemetry.dll` exists in the game's `bin\win_x64\plugins` directory. The agent listens only on `127.0.0.1:28745`. Debug shows a bind error if another process already uses that port.

### History is empty

Finished Jobs and Events use `app\test\output\events.jsonl` in the default installation (`C:\nlsi-tem\app\test\output\events.jsonl`). Job fields only appear when the game/plugin included them in persisted events.

### Known limitations

The current plugin does not provide game-time samples or reliable active-mod information. Job history is derived from the existing event stream and therefore cannot show unrecorded details. Live ETS2/ATS validation remains necessary.
