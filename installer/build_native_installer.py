#!/usr/bin/env python3
"""Stage the native Qt runtime and compile the Windows installer with Inno Setup."""

from __future__ import annotations

import json
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_METADATA = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
VERSION = str(VERSION_METADATA["version"])
CHANNEL = str(VERSION_METADATA["channel"]).lower()
FILE_VERSION = f"{VERSION}.0"
INSTALL_CHANNEL = "stable" if CHANNEL in {"stable", "public"} else CHANNEL
RELEASE_TAG = f"v{VERSION}" if INSTALL_CHANNEL == "stable" else f"v{VERSION}-{INSTALL_CHANNEL}"
RELEASE_LABEL = f"{VERSION}-{INSTALL_CHANNEL}" if INSTALL_CHANNEL != "stable" else VERSION
RELEASE_DIR = ROOT / "build" / "releases" / RELEASE_TAG
APP_EXE = "NLSI-Exclusive-Logbook.exe"
DEFAULT_INSTALL_DIR = r"C:\Program Files\NLSI Exclusive Logbook"
TRUCKSIM_PLUGIN_FILES = (
    (
        ROOT / "includes" / "trucksim-gps-plugin" / "win_x64" / "plugins"
        / "trucksim-gps-telemetry.dll",
        "4F1A1DD5B879773161C23D657249775D60C9AA362CED171D74C74A16F1AB0F0A",
        Path("plugins/trucksim/win_x64/plugins/trucksim-gps-telemetry.dll"),
    ),
    (
        ROOT / "includes" / "trucksim-gps-plugin" / "win_x86" / "plugins"
        / "trucksim-gps-telemetry.dll",
        "01E5D1CD6AF7C239A7B9E80E9911DE447FC8E80F66EE8F415D859A89F4A403BF",
        Path("plugins/trucksim/win_x86/plugins/trucksim-gps-telemetry.dll"),
    ),
)
SCS_POSITION_PLUGIN_FILES = (
    (
        ROOT / "build" / "plugins" / RELEASE_TAG / "win_x64" / "nlsi.dll",
        Path("plugins/scs-position/win_x64/nlsi.dll"),
        0x8664,
    ),
    (
        ROOT / "build" / "plugins" / RELEASE_TAG / "win_x86" / "nlsi.dll",
        Path("plugins/scs-position/win_x86/nlsi.dll"),
        0x014C,
    ),
)
SCS_PLUGIN_CHANNEL_IDENTIFIERS = (
    b"truck.fuel.amount",
    b"truck.brake.parking",
    b"fuel.capacity",
)
PAYLOAD_DIR = ROOT / "build" / "intermediate" / f"installer-payload-{RELEASE_TAG}"
REDIST_DIR = ROOT / "build" / "intermediate" / f"installer-redist-{RELEASE_TAG}"
ISS_FILE = ROOT / "installer" / "NLSI-Exclusive-Logbook.iss"
ASSET_SCRIPT = ROOT / "installer" / "prepare_native_installer_assets.ps1"
OUTPUT_EXE = RELEASE_DIR / f"NLSI-Exclusive-Logbook-{RELEASE_TAG}-Setup.exe"

REQUIRED_RUNTIME_FILES = (
    "Qt6Core.dll",
    "Qt6Concurrent.dll",
    "Qt6Gui.dll",
    "Qt6Network.dll",
    "Qt6Svg.dll",
    "Qt6Widgets.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
)


def fail(message: str) -> int:
    print(f"ERROR: {message}", file=sys.stderr)
    return 1


def read_pe_machine(path: Path) -> int:
    with path.open("rb") as binary:
        if binary.read(2) != b"MZ":
            raise ValueError(f"Plugin is not a Windows PE file: {path}")
        binary.seek(0x3C)
        offset_data = binary.read(4)
        if len(offset_data) != 4:
            raise ValueError(f"Plugin has a truncated PE header: {path}")
        pe_offset = int.from_bytes(offset_data, "little")
        binary.seek(pe_offset)
        if binary.read(4) != b"PE\0\0":
            raise ValueError(f"Plugin has an invalid PE signature: {path}")
        machine_data = binary.read(2)
        if len(machine_data) != 2:
            raise ValueError(f"Plugin has a truncated COFF header: {path}")
        return int.from_bytes(machine_data, "little")


def default_install_dir_for_channel(channel: str) -> str:
    normalized = channel.strip().lower()
    if normalized not in {"alpha", "beta", "stable", "public"}:
        raise ValueError(f"Unsupported release channel: {channel!r}.")
    return DEFAULT_INSTALL_DIR


def verify_version() -> None:
    payload = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    if payload.get("version") != VERSION or str(payload.get("channel", "")).lower() != CHANNEL:
        raise ValueError(f"version.json must describe {VERSION} {CHANNEL}.")
    if not re.fullmatch(r"\d+\.\d+\.\d+", VERSION):
        raise ValueError("version.json must contain a numeric X.Y.Z release version.")
    if INSTALL_CHANNEL not in {"alpha", "beta", "stable"}:
        raise ValueError(f"Unsupported release channel in version.json: {CHANNEL!r}.")

def verify_installer_policy() -> None:
    installer_text = ISS_FILE.read_text(encoding="utf-8")
    plugin_installer = ROOT / "installer" / "InstallTruckSimPlugin.ps1"
    script_text = plugin_installer.read_text(encoding="utf-8")
    if "InstallTruckSimPlugin.ps1" not in installer_text or "Tasks: trucksimplugin" in installer_text:
        raise ValueError("TruckSim GPS plugin installation must run automatically from the installer.")
    if "InstallRenCloudPlugin.ps1" in installer_text or "RenCloud" in installer_text:
        raise ValueError("The native installer must not contain RenCloud integration.")
    if ".bak" not in script_text or "[System.IO.File]::Replace($temporary, $destination, $backup)" not in script_text:
        raise ValueError("Managed plugin replacement must retain a recovery backup.")
    if "win_x64" not in script_text or "win_x86" not in script_text:
        raise ValueError("The installer must handle both supported plugin architectures.")
    if "SCSSdkClient.Demo.exe" in script_text or "TruckSim GPS Telemetry Server" in installer_text:
        raise ValueError("The TruckSim GPS server GUI must not be included or launched.")
    position_installer = ROOT / "installer" / "InstallScsPositionPlugin.ps1"
    position_script = position_installer.read_text(encoding="utf-8")
    if "InstallScsPositionPlugin.ps1" not in installer_text or "-RestoreManagedPlugin" not in installer_text:
        raise ValueError("The SCS position plugin must install and restore independently of TruckSim GPS.")
    if "BackupDirectory" not in position_script or "installed_sha256" not in position_script:
        raise ValueError("The SCS position plugin installer must back up and track managed copies.")
    if "TSGPSTelemetry" in position_script:
        raise ValueError("The SCS position plugin installer must remain separate from TruckSim GPS.")
    required_installer_policy = (
        "DefaultDirName={#DefaultApplicationDir}",
        'DefaultApplicationDir "C:\\Program Files\\NLSI Exclusive Logbook"',
        "DefaultInstallPath = 'C:\\Program Files\\NLSI Exclusive Logbook'",
        "UsePreviousAppDir=yes",
        "AppId=NLSI Exclusive Logbook",
        "ArchitecturesInstallIn64BitMode=x64compatible",
        "PrivilegesRequired={#InstallPrivileges}",
        '#define InstallPrivileges "admin"',
        "VersionInfoProductVersion={#AppFileVersion}",
        "VersionInfoVersion={#AppFileVersion}",
        "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NLSI Exclusive Logbook_is1",
        "QueryUninstallValue('InstallLocation'",
        "ReadManifestVersion(ExistingInstallDir",
        "ExistingInstallRecovery",
        "function DirectoryHasEntries",
        "DirectoryHasEntries(ExistingInstallDir)",
        "ReadManifestVersion(ExistingInstallDir, ManifestInstallVersion)",
        "CompareText(ManifestProduct, 'NLSI Exclusive Logbook')",
        "CompareReleaseVersions('{#ReleaseLabel}', ExistingInstallVersion)",
        "WizardForm.DirEdit.Text := ExistingInstallDir",
        "ExistingInstallError",
    )
    if any(rule not in installer_text for rule in required_installer_policy):
        raise ValueError("The native installer path, identity, or metadata policy is incomplete.")
    if "DelTree(" in installer_text or "LocalAppData" in installer_text:
        raise ValueError("The native installer must not delete legacy or per-user application data.")
    for channel in ("alpha", "beta", "stable"):
        if default_install_dir_for_channel(channel) != DEFAULT_INSTALL_DIR:
            raise ValueError(f"The native installer has an invalid {channel} destination.")


def verify_executable_version(app_exe: Path) -> None:
    escaped_path = str(app_exe).replace("'", "''")
    command = (
        f"$version = (Get-Item -LiteralPath '{escaped_path}').VersionInfo; "
        'Write-Output "$($version.FileVersion)|$($version.ProductVersion)"'
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", command],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    expected = f"{VERSION}.0|{VERSION}-{CHANNEL}"
    actual = result.stdout.strip()
    if actual != expected:
        raise ValueError(
            f"The native executable has version metadata {actual!r}; "
            f"expected {expected!r}. Refusing to package a stale executable."
        )


def locate_iscc() -> Path:
    candidates = [
        shutil.which("ISCC.exe"),
        Path(r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe"),
        Path(r"C:\Program Files\Inno Setup 6\ISCC.exe"),
        Path.home() / "AppData/Local/Programs/Inno Setup 6/ISCC.exe",
    ]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return Path(candidate)
    raise FileNotFoundError("Inno Setup 6 ISCC.exe was not found.")


def locate_windeployqt() -> Path:
    cache = ROOT / "build" / f"cmake-{RELEASE_TAG}" / "CMakeCache.txt"
    if not cache.is_file():
        raise FileNotFoundError(f"Missing configured native CMake build: {cache}")
    match = re.search(
        r"^Qt6_DIR(?::[^=]+)?=(.+)$",
        cache.read_text(encoding="utf-8", errors="replace"),
        re.MULTILINE,
    )
    if not match:
        raise ValueError(f"Qt6_DIR was not recorded in {cache}.")
    qt_module_directory = Path(match.group(1).strip().replace("/", "\\"))
    executable = qt_module_directory.parents[2] / "bin" / "windeployqt.exe"
    if not executable.is_file():
        raise FileNotFoundError(f"Qt deployment tool is missing: {executable}")
    return executable


def copy_runtime_payload(app_exe: Path) -> None:
    if PAYLOAD_DIR.exists():
        shutil.rmtree(PAYLOAD_DIR)
    PAYLOAD_DIR.mkdir(parents=True)

    for source in RELEASE_DIR.rglob("*"):
        if not source.is_file() or source.suffix.lower() != ".dll":
            continue
        relative = source.relative_to(RELEASE_DIR)
        destination = PAYLOAD_DIR / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    shutil.copy2(app_exe, PAYLOAD_DIR / APP_EXE)

    logo_directory = PAYLOAD_DIR / "img"
    logo_directory.mkdir(exist_ok=True)
    shutil.copy2(ROOT / "img" / "logo.ico", logo_directory / "logo.ico")
    shutil.copy2(ROOT / "img" / "logo.png", logo_directory / "logo.png")
    shutil.copy2(ROOT / "version.json", PAYLOAD_DIR / "version.json")
    docs_payload = PAYLOAD_DIR / "docs"
    docs_payload.mkdir(parents=True, exist_ok=True)
    for relative in (
        Path("RELEASE_NOTES.md"),
        Path("docs/NLSI-LOG-FORMAT.md"),
        Path("docs/TELEMETRY-MAPPING.md"),
        Path("docs/SQL-SCHEMA.md"),
        Path("db/migrations/001_telemetry_sync.sql"),
    ):
        source = ROOT / relative
        destination = docs_payload / relative.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    shutil.copy2(ROOT / "RELEASE_NOTES.md", RELEASE_DIR / "RELEASE_NOTES.md")

    for source, expected_hash, relative_destination in TRUCKSIM_PLUGIN_FILES:
        if not source.is_file():
            raise FileNotFoundError(f"The official TruckSim GPS plugin is missing: {source}")
        actual_hash = hashlib.sha256(source.read_bytes()).hexdigest().upper()
        if actual_hash != expected_hash:
            raise ValueError(
                f"TruckSim GPS plugin hash mismatch for {source}: "
                f"expected {expected_hash}, got {actual_hash}."
            )
        destination = PAYLOAD_DIR / relative_destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        if hashlib.sha256(destination.read_bytes()).hexdigest().upper() != expected_hash:
            raise ValueError(f"The staged TruckSim GPS plugin failed hash verification: {destination}")

    for source, relative_destination, expected_machine in SCS_POSITION_PLUGIN_FILES:
        if not source.is_file():
            raise FileNotFoundError(f"The SCS position plugin is missing: {source}")
        if read_pe_machine(source) != expected_machine:
            architecture = "x64" if expected_machine == 0x8664 else "x86"
            raise ValueError(
                f"The SCS position plugin for {architecture} has the wrong PE architecture: {source}"
            )
        binary = source.read_bytes()
        missing_channels = [
            identifier.decode("ascii")
            for identifier in SCS_PLUGIN_CHANNEL_IDENTIFIERS
            if identifier not in binary
        ]
        if missing_channels:
            raise ValueError(
                f"The SCS position plugin does not contain the v1.5.3 telemetry "
                f"identifiers {', '.join(missing_channels)}: {source}. Refusing to package "
                "a stale or incorrectly built DLL."
            )
        destination = PAYLOAD_DIR / relative_destination
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        if read_pe_machine(destination) != expected_machine:
            raise ValueError(f"The staged SCS position plugin has the wrong architecture: {destination}")
        staged_binary = destination.read_bytes()
        if any(identifier not in staged_binary for identifier in SCS_PLUGIN_CHANNEL_IDENTIFIERS):
            raise ValueError(f"The staged SCS position plugin lost required SDK identifiers: {destination}")

    license_payload = PAYLOAD_DIR / "licenses"
    license_payload.mkdir(parents=True, exist_ok=True)
    shutil.copy2(
        ROOT / "includes" / "trucksim-gps-plugin" / "LICENSE.txt",
        license_payload / "TruckSim-GPS-MIT.txt",
    )
    shutil.copy2(
        ROOT / "includes" / "trucksim-gps-plugin" / "SCS-SDK-LICENSE.txt",
        license_payload / "SCS-SDK-MIT.txt",
    )
    shutil.copy2(
        ROOT / "includes" / "scs_sdk_1_15" / "sdk_license.txt",
        license_payload / "SCS-SDK-1.15-MIT.txt",
    )
    shutil.copy2(
        ROOT / "assets" / "icons" / "LICENSE.txt",
        license_payload / "Lucide-ISC.txt",
    )
    tools_payload = PAYLOAD_DIR / "tools"
    tools_payload.mkdir(parents=True, exist_ok=True)
    shutil.copy2(
        ROOT / "installer" / "InstallTruckSimPlugin.ps1",
        tools_payload / "InstallTruckSimPlugin.ps1",
    )
    shutil.copy2(
        ROOT / "installer" / "InstallScsPositionPlugin.ps1",
        tools_payload / "InstallScsPositionPlugin.ps1",
    )


def copy_msvc_runtime() -> None:
    redist_roots = [
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"),
        Path(r"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Shared\VC\Redist\MSVC"),
    ]
    runtime_directories = sorted(
        (
            directory
            for root in redist_roots
            if root.is_dir()
            for directory in root.glob("*/x64/Microsoft.VC143.CRT")
            if directory.is_dir()
        ),
        key=lambda directory: tuple(
            int(part) if part.isdigit() else part
            for part in directory.parent.parent.name.split(".")
        ),
        reverse=True,
    )
    if not runtime_directories:
        raise FileNotFoundError(
            "The x64 Microsoft Visual C++ 14.3 redistributable runtime was not found."
        )

    runtime_directory = runtime_directories[0]
    print(f"Copying x64 MSVC runtime files from {runtime_directory}")
    for runtime_dll in runtime_directory.glob("*.dll"):
        shutil.copy2(runtime_dll, PAYLOAD_DIR / runtime_dll.name)

    runtime_root = runtime_directory.parents[1]
    REDIST_DIR.mkdir(parents=True, exist_ok=True)
    for architecture in ("x64", "x86"):
        installer = runtime_root / f"vc_redist.{architecture}.exe"
        if not installer.is_file():
            raise FileNotFoundError(
                f"The {architecture} Microsoft Visual C++ 14.3 redistributable "
                f"installer was not found: {installer}"
            )
        destination = REDIST_DIR / installer.name
        shutil.copy2(installer, destination)
        if destination.stat().st_size != installer.stat().st_size:
            raise OSError(f"The {architecture} Visual C++ redistributable staged incompletely.")
        print(f"Staged {architecture} MSVC redistributable: {destination}")


def prepare_runtime(app_exe: Path) -> None:
    deploy_tool = locate_windeployqt()
    copy_runtime_payload(app_exe)
    command = [
        str(deploy_tool),
        "--no-translations",
        "--compiler-runtime",
        "--dir",
        str(PAYLOAD_DIR),
        str(app_exe),
    ]
    print("Deploying Qt plugins and the MSVC runtime into the installer payload...")
    subprocess.run(command, cwd=ROOT, check=True)
    concurrent_runtime = deploy_tool.parent / "Qt6Concurrent.dll"
    if not concurrent_runtime.is_file():
        raise FileNotFoundError(
            f"The Qt Concurrent runtime is missing: {concurrent_runtime}"
        )
    shutil.copy2(concurrent_runtime, PAYLOAD_DIR / concurrent_runtime.name)
    copy_msvc_runtime()
    missing = [
        name
        for name in REQUIRED_RUNTIME_FILES
        if not (PAYLOAD_DIR / name).is_file()
    ]
    if missing:
        raise FileNotFoundError(
            "The staged application is missing required runtime DLLs: " + ", ".join(missing)
        )
    if not (PAYLOAD_DIR / "platforms" / "qwindows.dll").is_file():
        raise FileNotFoundError("The staged Qt Windows platform plugin is missing.")
    if not any((PAYLOAD_DIR / "tls" / name).is_file()
               for name in ("qschannelbackend.dll", "qopensslbackend.dll")):
        raise FileNotFoundError("The staged Qt TLS backend required for HTTPS is missing.")


def prepare_wizard_assets() -> None:
    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(ASSET_SCRIPT),
        "-BackgroundPath",
        str(ROOT / "img" / "9fkt.jpg"),
        "-LogoPath",
        str(ROOT / "img" / "logo.png"),
        "-OutputDirectory",
        str(PAYLOAD_DIR),
    ]
    subprocess.run(command, cwd=ROOT, check=True)


def compile_installer() -> None:
    compiler = locate_iscc()
    OUTPUT_EXE.parent.mkdir(parents=True, exist_ok=True)
    print(f"Compiling {ISS_FILE.name} with Inno Setup...")
    subprocess.run(
        [
            str(compiler),
            f'/DAppChannel="{INSTALL_CHANNEL}"',
            f"/DReleaseLabel={RELEASE_LABEL}",
            f"/DReleaseTag={RELEASE_TAG}",
            f"/DReleasePayload=build\\intermediate\\installer-payload-{RELEASE_TAG}",
            f"/DAppFileVersion={FILE_VERSION}",
            str(ISS_FILE),
        ],
        cwd=ROOT,
        check=True,
    )
    if not OUTPUT_EXE.is_file() or OUTPUT_EXE.stat().st_size == 0:
        raise FileNotFoundError(f"Inno Setup did not produce {OUTPUT_EXE}")


def main() -> int:
    try:
        verify_version()
        verify_installer_policy()
        app_exe = RELEASE_DIR / APP_EXE
        if not app_exe.is_file():
            raise FileNotFoundError(f"Build the native Release application first: {app_exe}")
        verify_executable_version(app_exe)
        if not (ROOT / "installer" / "licenses" / "TermsAndConditions.txt").stat().st_size:
            raise ValueError("Terms and Conditions file is empty.")
        if not (ROOT / "installer" / "licenses" / "PrivacyPolicy.txt").stat().st_size:
            raise ValueError("Privacy Policy file is empty.")
        prepare_runtime(app_exe)
        prepare_wizard_assets()
        compile_installer()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        return fail(str(exc))

    print(f"Installer created: {OUTPUT_EXE}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())