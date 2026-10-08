#!/usr/bin/env python3
"""Stage the native Qt runtime and compile the Windows installer with Inno Setup."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_METADATA = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
VERSION = str(VERSION_METADATA["version"])
CHANNEL = str(VERSION_METADATA["channel"]).lower()
RELEASE_TAG = f"v{VERSION}-{CHANNEL}"
RELEASE_DIR = ROOT / "build" / "releases" / RELEASE_TAG
APP_EXE = "NLSI-Exclusive-Logbook.exe"
PAYLOAD_DIR = ROOT / "build" / "intermediate" / f"installer-payload-{RELEASE_TAG}"
ISS_FILE = ROOT / "installer" / "NLSI-Exclusive-Logbook.iss"
ASSET_SCRIPT = ROOT / "installer" / "prepare_native_installer_assets.ps1"
OUTPUT_EXE = RELEASE_DIR / f"NLSI-Exclusive-Logbook-{RELEASE_TAG}-Setup.exe"

REQUIRED_RUNTIME_FILES = (
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
)


def fail(message: str) -> int:
    print(f"ERROR: {message}", file=sys.stderr)
    return 1


def verify_version() -> None:
    payload = json.loads((ROOT / "version.json").read_text(encoding="utf-8"))
    if payload.get("version") != VERSION or str(payload.get("channel", "")).lower() != CHANNEL:
        raise ValueError(f"version.json must describe {VERSION} {CHANNEL}.")


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
    expected = f"{VERSION}.0|{VERSION} Alpha"
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
    cache = ROOT / "build" / "cmake" / "CMakeCache.txt"
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
    subprocess.run([str(compiler), str(ISS_FILE)], cwd=ROOT, check=True)
    if not OUTPUT_EXE.is_file() or OUTPUT_EXE.stat().st_size == 0:
        raise FileNotFoundError(f"Inno Setup did not produce {OUTPUT_EXE}")


def main() -> int:
    try:
        verify_version()
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