# NLSI local telemetry proof of concept

This standalone Windows proof of concept connects Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS) to a local console agent through the official SCS Telemetry SDK. It does not modify the NLSI website, call a server, or send telemetry over the network.

## Architecture

The official SCS Telemetry SDK is a native plugin ABI. The game loads a plugin DLL and calls its exported `scs_telemetry_init` and `scs_telemetry_shutdown` entry points. During initialization, the plugin identifies the game using SCS-provided metadata and registers channel and event callbacks. The plugin reports the game's SCS telemetry API version from those initialization parameters; it is not the game patch version. The SCS SDK contract is version 1.01, distinct from the SDK release and each game's telemetry API version.

SCS lists Windows, Linux, and macOS as supported SDK platforms. This proof of concept currently targets Windows x64 only: it builds a DLL and uses Winsock loopback UDP plus Python.

The plugin runs its callbacks on the game's thread. It serializes data into JSON and sends it through UDP to `127.0.0.1:28745`. The separate Python agent receives those packets, presents a live console dashboard, and appends important events to `test/output/events.jsonl`. Raw telemetry remains available in the agent's debug view. There is no external listener or server component.

Continuous channel samples are limited to four per second rather than stored every frame. Configuration callbacks update truck, trailer, and job data only when those values change. Gameplay callbacks are recorded as discrete events. The plugin also sends a lightweight local heartbeat so the agent can detect loss of the game/plugin connection, including when gameplay telemetry is paused.

## SDK and plugin installation

1. Download the official [SCS Telemetry SDK 1.15](https://download.eurotrucksimulator2.com/scs_sdk_1_15.zip) and extract it.
2. Build the 64-bit plugin using the instructions below. The resulting file is `build\nlsi_telemetry.dll`.
3. Create a `plugins` directory under the game's 64-bit binary directory if it does not exist.
4. Copy `build\nlsi_telemetry.dll` to that `plugins` directory. The standard Steam locations are:
   - ETS2: `<Steam Library>\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins`
   - ATS: `<Steam Library>\steamapps\common\American Truck Simulator\bin\win_x64\plugins`
5. If the game is installed somewhere else, use its own `bin\win_x64\plugins` directory. The game must match the DLL architecture. This POC builds an x64 plugin only.

The SCS SDK also documents registry-based plugin discovery. This POC uses the game's binary-directory plugin folder and does not edit the Windows registry.

## Build

Requirements:

- Windows x64
- Python 3.10 or newer (the agent uses only the standard library)
- Visual Studio Build Tools with the **Desktop development with C++** workload / MSVC x64 tools
- The extracted official SCS SDK 1.15 archive

From this directory, run:

```bat
build.bat "C:\path\to\extracted\scs_sdk_1_15"
```

The SDK path must contain `include\scssdk_telemetry.h`. The build script locates the installed MSVC compiler through `vswhere` and builds only the DLL; it does not copy SDK headers or libraries into this project.

## Installation and releases

For a prebuilt Windows installer, download `NLSI-Telemetry-Setup-vX.Y.Z.exe` from the [GitHub Releases](https://github.com/Christian-0777/nlsi_telemetry/releases) page when a release is available. The installer is a release asset and is not committed to the source repository. To build it locally, see [RELEASING.md](RELEASING.md).

## Run

Start the local receiver before launching either game:

```bat
python agent.py
```

Then launch ETS2 or ATS. The agent updates an in-place dashboard approximately four times per second as telemetry arrives.

- Press **D** to toggle the latest raw telemetry packet (pretty-printed JSON); press **D** again to return to the dashboard.
- Press **E** to view recent events; press **E** again to return to the dashboard.
- Press **Q** to stop the agent cleanly. Ctrl+C is also supported. A running session is closed with a `SESSION_ENDED` event.

The dashboard and views are presentation-only. The receiver's packet handling, telemetry model, and JSONL event recording are unchanged.

The receiver binds only to `127.0.0.1`. The plugin drops UDP telemetry if the receiver is not listening; it does not persist or transmit data on its own.

## Data model and SDK coverage

### Continuous telemetry

The plugin subscribes to the following documented channels:

- Truck world placement: coordinates and orientation.
- Speed in meters per second and a derived meters-per-second-to-kilometers-per-hour conversion.
- Engine RPM and selected engine gear.
- Input steering, throttle, and brake.
- Fuel in liters, fuel range in kilometers, average consumption, and fuel warning.
- Odometer in kilometers.
- Navigation distance in meters and estimated navigation time in seconds.
- Truck engine, transmission, cabin, chassis, and wheel wear.
- AdBlue warning, when supplied by the game.

Channel values not provided by the game/API are output as JSON `null`. The SDK supplies odometer but does not provide a universal authoritative “distance driven this session” counter; the agent estimates distance from nondecreasing odometer samples and ignores negative reset/wrap deltas.

### Configuration and semi-dynamic data

The plugin forwards all attributes supplied in SDK truck, trailer, job, car-job, bus-job, or other configuration callbacks. These may include truck brand/model and license plate, trailer information, cargo, source/destination, income, delivery time, and planned distance. The exact attributes depend on the game, current job, and SDK API version. An empty job configuration means the SDK reports no active job.

### Events and sessions

The SDK gameplay event callback can report events such as `job.delivered`, `job.cancelled`, `car_job.delivered`, `player.fined`, tollgate payments, and ferry/train use, including their documented attributes when available. The plugin passes event objects through rather than populating guessed fields. It also subscribes to SDK frame-start/frame-end, pause/resume, and configuration-change callbacks. ETS2's and ATS's game telemetry API versions are supplied independently by SCS; SDK 1.15 headers describe ETS2 API 1.20 and ATS API 1.07, but those are not game patch numbers.

The SDK does not define a dedicated job-start or refuel gameplay event. The agent labels an empty-to-active job configuration transition as `JOB_STARTED` and records that it was derived from configuration changes. Fuel changes are telemetry samples, not a confirmed refuel event.

The agent records:

- `SESSION_STARTED` when the SDK plugin initializes in a game.
- `SESSION_HEARTBEAT` every 15 seconds while telemetry packets continue.
- `SESSION_ENDED` when the plugin shuts down, heartbeat is lost, or the agent is stopped.
- `DRIVING_PAUSED` and `DRIVING_RESUMED` for SDK pause/start transitions.
- SDK gameplay events and the derived `JOB_STARTED` event.

`SESSION_ENDED` includes elapsed session time, driving time, estimated distance, and completed job count. A session starts when the game loads the plugin, not necessarily at OS process creation; orderly game/plugin shutdown is the preferred end signal.

## Test

Run the local agent model tests without a game:

```bat
python -m unittest discover -s test -v
```

These tests validate event JSONL output, session metrics, distance accumulation, job-start inference, timeout behavior, and preservation of unavailable values as `null`. They do not simulate or claim real game telemetry.

### ETS2

1. Build and install the x64 plugin in ETS2's `bin\win_x64\plugins` folder.
2. Start `python agent.py`.
3. Launch ETS2 and load a profile.
4. Confirm the console identifies `ets2`, then inspect truck channels and job/trailer configuration as the game supplies them.
5. Drive, pause, resume, and complete a job to observe telemetry and documented gameplay events.
6. Exit the game and confirm `SESSION_ENDED`.

### ATS

Repeat the same process using ATS's `bin\win_x64\plugins` folder. The plugin identifies ATS from SCS's `game_id`; both games use the same documented callback and local-agent path.

## Limitations and unavailable data

- This POC has not been validated with a live ETS2/ATS game session. Channel registration and real-game output must be verified in each installed game.
- The installer must provide the MSVC x64 tools. The SDK archive itself contains headers and examples, not a Windows compiler.
- A separate console process cannot connect directly to SCS telemetry without a game-loaded native plugin or a compatible SDK bridge.
- There is no public, stable dedicated refuel event, universal session distance counter, or dedicated job-start event in the SDK contract used here.
- Some game values are absent depending on version/state. Bus-job configuration is not fully documented by SCS.
- SCS notes that some pressures, temperatures, and voltages are approximations; this POC does not subscribe to undocumented `dev.*` channels.
- Multiplayer, map scale, game timer resets, and odometer resets can affect interpretation of derived duration/distance.
- UDP is local, best-effort transport; packets sent while the agent is not running are not queued.
- Telemetry remains local. The future NLSI API, authentication, server-side storage, and Driver Portal integration are explicitly out of scope.

## Next step for future API integration

After the game-loaded plugin, local UDP receiver, and real ETS2/ATS telemetry have been validated, define and secure the NLSI PHP `/api/telemetry/` contract (authentication, allowed event/sample schema, and rate limits). Then add an explicit HTTPS uploader and durable retry queue to the local agent. Neither is part of this proof of concept.

## References

- [Official SCS Telemetry SDK documentation](https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry)
- [Official SCS SDK 1.15 archive](https://download.eurotrucksimulator2.com/scs_sdk_1_15.zip)
- [TruckTel](https://github.com/jvanstraten/TruckTel) — plugin/server architecture reference.
- [TruckSim-Telemetry](https://github.com/kniffen/TruckSim-Telemetry) — shared-memory consumer reference using an older third-party SCS plugin.
