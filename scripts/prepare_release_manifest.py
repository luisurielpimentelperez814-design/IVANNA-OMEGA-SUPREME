#!/usr/bin/env python3
"""Generate a candidate Magisk updater manifest from validated release metadata.

The candidate is included in the staged ZIP, but the public main-branch manifest
is committed only after the canonical release assets have been uploaded and
verified. This prevents clients seeing a version whose ZIP still returns 404.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path


def read_properties(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def prepare_manifest(
    module_prop: Path,
    version_properties: Path,
    update_json: Path,
    repository: str,
) -> dict[str, object]:
    module = read_properties(module_prop)
    props = read_properties(version_properties)
    tag = module.get("version", "")
    code = module.get("versionCode", "")
    if not tag.startswith("v") or tag[1:] != props.get("versionName"):
        raise ValueError("module.prop and version.properties version mismatch")
    if not code.isdigit() or code != props.get("versionCode"):
        raise ValueError("module.prop and version.properties versionCode mismatch")
    if not repository or "/" not in repository:
        raise ValueError("repository must be owner/name")

    data = json.loads(update_json.read_text(encoding="utf-8"))
    data["version"] = tag
    data["versionCode"] = int(code)
    data["zipUrl"] = (
        f"https://github.com/{repository}/releases/download/{tag}/"
        f"ivanna_omega_supreme_{tag}.zip"
    )
    data["changelog"] = (
        f"https://raw.githubusercontent.com/{repository}/main/CHANGELOG.md"
    )
    return data


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--module-prop", type=Path, default=Path("magisk_module/module.prop"))
    parser.add_argument("--version-properties", type=Path, default=Path("version.properties"))
    parser.add_argument("--update-json", type=Path, default=Path("magisk_module/update.json"))
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    data = prepare_manifest(
        args.module_prop, args.version_properties, args.update_json, args.repository
    )
    rendered = json.dumps(data, indent=2, ensure_ascii=False) + "\n"
    if args.write:
        args.update_json.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
