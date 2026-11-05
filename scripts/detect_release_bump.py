#!/usr/bin/env python3
"""Compare the current release manifest with the last published release tag."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


_SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def parse_properties(path: str | Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in Path(path).read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#") and "=" in line:
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


def version_tuple(value: str) -> tuple[int, int, int]:
    match = _SEMVER.fullmatch(value)
    if not match:
        raise ValueError(f"versionName inválido: {value!r}; se espera MAJOR.MINOR.PATCH")
    return tuple(int(part) for part in match.groups())


def should_publish(current: dict[str, str], baseline: dict[str, str] | None) -> bool:
    current_name = current.get("versionName", "")
    current_code = int(current.get("versionCode", "-1"))
    current_semver = version_tuple(current_name)
    if current_code < 1:
        raise ValueError("versionCode debe ser un entero positivo")
    if baseline is None:
        return True

    baseline_name = baseline.get("versionName", "")
    baseline_code = int(baseline.get("versionCode", "-1"))
    baseline_semver = version_tuple(baseline_name)
    if baseline_code < 1:
        raise ValueError("versionCode de baseline inválido")

    if current_code < baseline_code:
        raise ValueError(f"rollback de versionCode: {current_code} < {baseline_code}")
    if current_code == baseline_code and current_name == baseline_name:
        return False
    if current_code == baseline_code:
        raise ValueError("versionName cambió sin incrementar versionCode")
    if current_semver <= baseline_semver:
        raise ValueError("versionCode incrementó sin avanzar versionName")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--current", required=True)
    parser.add_argument("--baseline")
    args = parser.parse_args()
    current = parse_properties(args.current)
    baseline = parse_properties(args.baseline) if args.baseline else None
    print("true" if should_publish(current, baseline) else "false")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
