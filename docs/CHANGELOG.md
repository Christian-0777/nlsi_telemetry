# Changelog

Release history is based on the repository's tagged v0.1.0 source and the confirmed v0.2.0 release changes.

## v0.2.0 — update release

### Implemented

- Updated the authoritative version in `version.json`.
- Added a unified `build-release.bat` command that validates the SDK, builds and verifies the native DLL, runs tests, invokes the existing installer builder, and verifies the release artifacts.
- Updated the Inno Setup package to install the launcher BAT and create Desktop and Start Menu shortcuts.
- Added Steam library detection using registry entries, `libraryfolders.vdf`, and common drive-root Steam folders, then installs a copy of the canonical DLL for detected ETS2/ATS installations.
- Added per-game plugin status and persistent installer logging; a locked plugin copy is reported as a failure.
- Kept the v0.1.0 application identity for in-place upgrades and retained configuration/data/log directories and agent event output.
- Added an optional final installer page with a TikTok checkbox. The browser opens only when the checkbox is selected and the user completes setup.
- Reorganized the README and added user, installation, technical, development, and release documentation while preserving the original technical README as `docs/POC-README.md`.

### Planned / not included

- Validation in live ETS2 and ATS sessions across supported game versions remains outstanding.
- NLSI API upload, authentication, cloud storage, and Driver Portal integration are not implemented.

## v0.1.0 — initial Windows x64 release

The repository contains the `v0.1.0` tag. That release introduced the SCS SDK native plugin and local Python UDP agent for ETS2/ATS, a console telemetry dashboard, JSONL event/session recording, and the first Inno Setup package.
