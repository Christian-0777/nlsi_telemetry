# NLSI Telemetry v0.3.0

NLSI Telemetry v0.3.0 improves the local Windows telemetry workflow for Euro Truck Simulator 2 (ETS2) and American Truck Simulator (ATS). It keeps the existing live telemetry architecture and adds a more professional dashboard, explicit cruise-control indicators, session tracking, and a completed-delivery PDF report export.

## What's new

- Refined the live console dashboard to keep the existing structure while surfacing session, cruise-control, and delivery context in a clearer layout.
- Added explicit cruise-control and adaptive-cruise indicators when the supported telemetry fields are present; otherwise the dashboard shows `N/A` without guessing.
- Added session IDs and more structured session metadata.
- Recorded PDF export events (`PDF_EXPORT_STARTED`, `PDF_EXPORT_COMPLETED`, `PDF_EXPORT_FAILED`) and wrote session-based completed job reports under the local `data/sessions` folder.
- Ensured the delivery PDF filename is derived from the session ID and includes the source/destination and distance summary when available.
- Kept the v0.2.0 upgrade path intact and maintained local user data retention.

## Telemetry features

- Native telemetry plugin built with the SCS Telemetry SDK 1.15.
- Local Python agent receives telemetry over loopback UDP and displays a live console dashboard for vehicle, driving, position, navigation, job, session, and connection status.
- Press **D** to inspect the latest raw telemetry packet as formatted JSON, **E** to review recent events, and **Q** to stop the agent cleanly.
- Gameplay events, session metrics, and PDF export events are recorded locally as JSONL.
- Delivery PDFs are written locally and are not the primary telemetry store.

## Install or upgrade

Download `NLSI-Telemetry-Setup-v0.3.0.exe` from the [NLSI Telemetry GitHub Releases page](https://github.com/Christian-0777/nlsi_telemetry/releases). Close ETS2 and ATS before installing or updating. The default application directory remains `C:\nlsi-tem`.

Run the v0.3.0 installer over a v0.2.0 installation to upgrade; do not uninstall first. After setup, start NLSI Telemetry from its Desktop or Start Menu shortcut before launching the game.

## Requirements and limitations

- Windows x64 and ETS2 and/or ATS for live game telemetry.
- Python 3.10 or newer and the Windows Python Launcher (`py`) are required by the installed launcher unless a bundled `runtime\python.exe` is provided. The current installer does not bundle Python.
- Session and PDF records are stored locally under the project data directories. PDF generation uses the actual project logo and is a local export layer.
- Adaptive cruise control is shown as `N/A` when the underlying official telemetry fields are not exposed by the SDK version in use.
- Telemetry is processed locally. This release does not upload data to a website or cloud service; NLSI API upload, authentication, cloud storage, and Driver Portal integration are not included.

For setup details, controls, and troubleshooting, see the [user guide](docs/USER-GUIDE.md).
