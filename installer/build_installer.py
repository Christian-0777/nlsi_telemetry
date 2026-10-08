#!/usr/bin/env python3
"""Build the Windows installer for the NLSI Telemetry agent."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SDK_HEADER = "include\\scssdk_telemetry.h"
ISS_TEMPLATE = ROOT / "installer" / "NLSI-Telemetry.iss"


def fail(message: str, exit_code: int = 1) -> int:
    print(message, file=sys.stderr)
    return exit_code


def read_version() -> str:
    version_path = ROOT / "version.json"
    if not version_path.exists():
        raise FileNotFoundError(f"Missing version file: {version_path}")

    payload = json.loads(version_path.read_text(encoding="utf-8"))
    version = payload.get("version")
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError(f"version.json must define a semantic version string in X.Y.Z format, got {version!r}.")
    return version


def run_command(args: list[str], cwd: Path | None = None, description: str | None = None) -> None:
    if description:
        print(description)
    result = subprocess.run(args, cwd=str(cwd or ROOT), shell=False)
    if result.returncode != 0:
        raise RuntimeError(f"Command failed ({result.returncode}): {' '.join(args)}")


def versioned_build_path(version: str) -> Path:
    return ROOT / "build" / f"v{version}"


def prepare_staging(version: str, sdk_root: Path) -> Path:
    stage_root = ROOT / "installer" / "staging"
    if stage_root.exists():
        for child in sorted(stage_root.iterdir(), reverse=True):
            if child.is_dir():
                shutil.rmtree(child)
            else:
                child.unlink()

    dirs = [
        stage_root / "app",
        stage_root / "bin",
        stage_root / "config",
        stage_root / "data",
        stage_root / "logs",
        stage_root / "runtime",
        stage_root / "tools",
        stage_root / "app" / "img",
    ]
    for directory in dirs:
        directory.mkdir(parents=True, exist_ok=True)

    dll_source = versioned_build_path(version) / "nlsi_telemetry.dll"
    if not dll_source.exists():
        raise FileNotFoundError(f"Missing DLL for version {version}: {dll_source}")

    shutil.copy2(ROOT / "agent.py", stage_root / "app" / "agent.py")
    shutil.copy2(ROOT / "gui_app.py", stage_root / "app" / "gui_app.py")
    shutil.copy2(ROOT / "README.md", stage_root / "app" / "README.md")
    shutil.copy2(ROOT / "img" / "logo.ico", stage_root / "app" / "img" / "logo.ico")
    shutil.copy2(ROOT / "version.json", stage_root / "version.json")
    shutil.copy2(ROOT / ".env.example", stage_root / "config" / ".env.example")
    shutil.copy2(dll_source, stage_root / "bin" / "nlsi_telemetry.dll")

    launcher_source = ROOT / "installer" / "NLSI-Telemetry-Launcher.bat"
    shutil.copy2(launcher_source, stage_root / "NLSI-Telemetry.bat")

    install_helper_source = ROOT / "installer" / "InstallGamePlugins.ps1"
    shutil.copy2(install_helper_source, stage_root / "tools" / "InstallGamePlugins.ps1")

    # Keep the package layout explicit for a first professional installer.
    return stage_root


def write_build_info(version: str, sdk_root: Path, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "product": "NLSI Exclusive Logbook",
        "version": version,
        "platform": "windows-x64",
        "sdk_root": str(sdk_root),
        "build_timestamp_utc": __import__("datetime").datetime.now(__import__("datetime").timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "installer": {
            "name": "NLSI-Telemetry-Setup",
            "file_name": f"NLSI-Telemetry-Setup-v{version}.exe",
        },
    }
    output_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def locate_iscc() -> str:
    python_search = shutil.which("iscc")
    if python_search:
        return python_search

    checked = [
        Path("C:/Program Files (x86)/Inno Setup 6/ISCC.exe"),
        Path("C:/Program Files/Inno Setup 6/ISCC.exe"),
        Path.home() / "AppData/Local/Programs/Inno Setup 6/ISCC.exe",
    ]
    for candidate in checked:
        if candidate.exists():
            return str(candidate)

    raise FileNotFoundError("Inno Setup 6 was not found on this system. Install it and ensure iscc.exe is in PATH.")


def build_iss_file(version: str, stage_root: Path, version_build_root: Path) -> Path:
    template = ISS_TEMPLATE.read_text(encoding="utf-8")
    compiled = template.replace('#define AppVersion "__APP_VERSION__"', f'#define AppVersion "{version}"')
    setup_icon = (stage_root / "app" / "img" / "logo.ico").resolve().as_posix()
    compiled = compiled.replace("SetupIconFile=__SETUP_ICON__", f"SetupIconFile={setup_icon}")
    compiled = compiled.replace("OutputDir=..\\build\\v{#AppVersion}", f"OutputDir={version_build_root.as_posix()}")
    compiled = compiled.replace("SourceDir=staging", f"SourceDir={stage_root.as_posix()}")

    generated = version_build_root / f"NLSI-Telemetry-v{version}.iss"
    generated.write_text(compiled, encoding="utf-8")
    return generated


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"Usage: {Path(argv[0]).name} <path-to-extracted-scs-sdk-1.15>", file=sys.stderr)
        return 2

    sdk_root = Path(argv[1]).expanduser().resolve()
    sdk_header = sdk_root / SDK_HEADER
    if not sdk_header.exists():
        print(f"The SDK root must contain {SDK_HEADER}.", file=sys.stderr)
        return 2

    try:
        version = read_version()
    except Exception as exc:
        return fail(f"Unable to read version.json: {exc}")

    repo_build_root = ROOT / "build"
    repo_build_root.mkdir(exist_ok=True)
    version_build_root = repo_build_root / f"v{version}"
    version_build_root.mkdir(exist_ok=True)

    print("[1/5] Running Python unit tests...")
    try:
        run_command([sys.executable, "-m", "unittest", "discover", "-s", "test", "-v"], cwd=ROOT)
    except Exception as exc:
        return fail(f"Unit tests failed: {exc}")

    print("[2/5] Building native telemetry DLL...")
    try:
        run_command(["cmd", "/c", str(ROOT / "build.bat"), str(sdk_root)], cwd=ROOT)
    except Exception as exc:
        return fail(f"Native DLL build failed: {exc}")

    print("[3/5] Preparing clean installer staging...")
    try:
        prepare_staging(version, sdk_root)
    except Exception as exc:
        return fail(f"Failed to prepare installer staging: {exc}")

    build_info_path = version_build_root / "build-info.json"
    try:
        write_build_info(version, sdk_root, build_info_path)
    except Exception as exc:
        return fail(f"Failed to generate build-info.json: {exc}")

    print("[4/5] Compiling installer package...")
    try:
        iscc_exe = locate_iscc()
    except Exception as exc:
        return fail(str(exc))

    stage_root = ROOT / "installer" / "staging"
    try:
        generated_iss = build_iss_file(version, stage_root, version_build_root)
        subprocess.run([iscc_exe, str(generated_iss)], check=True, cwd=str(ROOT))
    except subprocess.CalledProcessError as exc:
        return fail(f"Inno Setup compile failed with exit code {exc.returncode}.")

    output_exe = version_build_root / f"NLSI-Telemetry-Setup-v{version}.exe"
    print(f"[5/5] Installer created in {version_build_root}")
    if output_exe.exists():
        print(output_exe)
    else:
        print(f"Expected installer was not produced: {output_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
