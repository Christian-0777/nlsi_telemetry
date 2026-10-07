#!/usr/bin/env python3
"""Read the authoritative application version from the project root JSON file."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    version_path = root / "version.json"

    if not version_path.exists():
        raise FileNotFoundError(f"Missing version file: {version_path}")

    payload = json.loads(version_path.read_text(encoding="utf-8"))
    version = payload.get("version") or payload.get("app_version")

    if not isinstance(version, str) or not version.strip():
        raise ValueError(f"version.json does not contain a valid version string: {version_path}")

    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError(f"Version must use semantic versioning (X.Y.Z): {version!r}")

    print(version)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - CLI error path
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
