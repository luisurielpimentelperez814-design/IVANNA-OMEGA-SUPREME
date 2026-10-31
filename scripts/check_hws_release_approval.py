#!/usr/bin/env python3
"""Fail-closed production release gate for Hardware Sovereignty phases 01–04.

Approved evidence must be a non-empty, phase-scoped file under evidence/hws-NN/.
Evidence paths are unique across phases and cannot escape the repository root.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

PHASES = ("HWS-01", "HWS-02", "HWS-03", "HWS-04")


def validate(data: object, repo_root: Path) -> list[str]:
    """Return every manifest defect; an empty list means production approval passes."""
    failures: list[str] = []
    if not isinstance(data, dict) or type(data.get("schema_version")) is not int or data["schema_version"] != 1:
        return ["unsupported or missing approval manifest schema_version (expected integer 1)"]
    phases = data.get("phases")
    if not isinstance(phases, dict):
        return ["manifest must contain an object named 'phases'"]

    root = repo_root.resolve()
    used_evidence: set[Path] = set()
    for phase in PHASES:
        entry = phases.get(phase)
        if not isinstance(entry, dict):
            failures.append(f"{phase}: missing phase record")
            continue
        status = entry.get("status")
        if status != "APPROVED":
            failures.append(f"{phase}: status={status!r}, required 'APPROVED'")
            continue

        evidence = entry.get("evidence")
        if not isinstance(evidence, list) or not evidence:
            failures.append(f"{phase}: approved phase requires non-empty phase-specific evidence")
            continue
        expected_dir = f"evidence/{phase.lower()}/"
        for item in evidence:
            if not isinstance(item, str) or not item.strip():
                failures.append(f"{phase}: evidence contains an empty/non-string reference")
                continue
            rel = Path(item)
            normalized = rel.as_posix()
            if rel.is_absolute() or ".." in rel.parts or not normalized.startswith(expected_dir):
                failures.append(f"{phase}: evidence must be inside {expected_dir}: {item!r}")
                continue
            target = (root / rel).resolve()
            try:
                target.relative_to(root)
            except ValueError:
                failures.append(f"{phase}: evidence resolves outside repository root: {item!r}")
                continue
            if target in used_evidence:
                failures.append(f"{phase}: evidence file resolves to evidence already used by another phase: {item!r}")
                continue
            used_evidence.add(target)
            if not target.is_file() or target.stat().st_size <= 0:
                failures.append(f"{phase}: evidence file missing or empty: {item!r}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", help="approval JSON manifest")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="repository root (defaults to the checkout containing this script)")
    args = parser.parse_args()
    try:
        data = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"BLOCKED: cannot read approval manifest: {exc}", file=sys.stderr)
        return 2
    failures = validate(data, args.root)
    if failures:
        print("BLOCKED: Hardware Sovereignty production approval incomplete:")
        for failure in failures:
            print(f" - {failure}")
        return 1
    print("PASS: HWS-01 through HWS-04 approved with unique phase-scoped evidence.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
