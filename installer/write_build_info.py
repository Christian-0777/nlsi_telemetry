#!/usr/bin/env python3
"""Generate the build manifest for the Windows installer."""

from __future__ import annotations

import argparse
import json
from datetime import datetime, timezone
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate a build-info.json file for NLSI Telemetry.")
    parser.add_argument("--version", help="Semantic version to embed in the manifest.")
    parser.add_argument("--output", help="Path to the output build-info.json file.")
    parser.add_argument("--product", default="NLSI Telemetry", help="Human-readable product name.")
    parser.add_argument("--sdk-root", default="", help="Optional SDK root used during build.")
    return parser.parse_args()


def read_version(version: str | None) -> str:
    if version:
        return version

    version_file = Path(__file__).resolve().parents[1] / "version.json"
    payload = json.loads(version_file.read_text(encoding="utf-8"))
    version_value = payload.get("version")
    if not isinstance(version_value, str) or not version_value.strip():
        raise ValueError(f"{version_file} does not define a valid version.")
    return version_value


def main() -> int:
    args = parse_args()
    version = read_version(args.version)
    output_path = Path(args.output) if args.output else Path(__file__).resolve().parents[1] / "build" / f"v{version}" / "build-info.json"
    output_path.parent.mkdir(parents=True, exist_ok=True)

    manifest = {
        "product": args.product,
        "version": version,
        "build_timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "sdk_root": args.sdk_root,
        "platform": "windows-x64",
        "installer": {
            "name": "NLSI-Telemetry-Setup",
            "file_name": f"NLSI-Telemetry-Setup-v{version}.exe",
        },
    }

    output_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(str(output_path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
