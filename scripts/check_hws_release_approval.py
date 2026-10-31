#!/usr/bin/env python3
"""Fail-closed production release gate for Hardware Sovereignty phases 01–04."""
from __future__ import annotations
import json
import sys
from pathlib import Path

PHASES = ("HWS-01", "HWS-02", "HWS-03", "HWS-04")

def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check_hws_release_approval.py MANIFEST", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"BLOCKED: cannot read approval manifest: {exc}", file=sys.stderr)
        return 2
    phases = data.get("phases") if isinstance(data, dict) else None
    if not isinstance(phases, dict):
        print("BLOCKED: manifest must contain an object named 'phases'", file=sys.stderr)
        return 1
    failures = []
    for phase in PHASES:
        entry = phases.get(phase)
        if not isinstance(entry, dict):
            failures.append(f"{phase}: missing phase record")
            continue
        status = entry.get("status")
        evidence = entry.get("evidence")
        if status != "APPROVED":
            failures.append(f"{phase}: status={status!r}, required 'APPROVED'")
        if not isinstance(evidence, list) or not evidence or any(
            not isinstance(item, str) or not item.strip() for item in evidence
        ):
            failures.append(f"{phase}: evidence must be a non-empty list of references")
    if failures:
        print("BLOCKED: Hardware Sovereignty production approval incomplete:")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print("PASS: HWS-01 through HWS-04 approved with evidence.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
